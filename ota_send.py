import os, sys, socket, struct, zlib, pathlib, time, asyncio
from typing import Optional
from asyncio import CancelledError
# ---------- Optional BLE ----------
try:
    from bleak import BleakClient, BleakScanner
    from bleak.backends.device import BLEDevice
    BLE_AVAILABLE = True
except Exception:
    BLE_AVAILABLE = False
    BLEDevice = object

# ---------- Config ----------
ADDR   = os.getenv("LOPY_ADDR", "192.168.10.125")
PORT   = int(os.getenv("LOPY_PORT", "8080"))
TOKEN  = os.getenv("LOPY_TOKEN", "hunter2")
DEBUG_BLE = True

CHUNK  = 16 * 1024
PROGRESS_EVERY = 256 * 1024

WAIT_AFTER_REBOOT_S = 10
RECONNECT_TRIES     = 12
RECONNECT_DELAY_S   = 1.0

# We resolve by prefix, so we don't care about full UUID endian flips.
RX_PREFIX    = "efbe0100"
TX_PREFIX    = "efbe0200"
WIFI_PREFIX  = "efbe0300"
ERR_PREFIX   = "efbe0400"
ALERT_PREFIX = "efbe0500"

BLE_ADV_NAMES = [n.strip() for n in os.getenv("LOPY_BLE_NAMES", "LoPy4").split(",") if n.strip()]
BLE_ADDR = (os.getenv("LOPY_BLE_ADDR", "") or "").replace("-", ":").upper()

# --- Fast fallback tunables (no device power impact) ---
TCP_POST_OTA_CONNECT_TIMEOUT = float(os.getenv("LOPY_TCP_RETRY_TIMEOUT", "1.3"))
POST_OTA_TCP_ONLY_WINDOW_S = float(os.getenv("LOPY_TCP_ONLY_WINDOW_S", "3.0"))
BLE_QUICK_SCAN_S = float(os.getenv("LOPY_BLE_QUICK_SCAN_S", "1.8"))

# ---------- Helpers ----------
async def _stop_notify_quiet(client, *uuids):
    for u in uuids:
        if not u:
            continue
        try:
            await client.stop_notify(u)
        except CancelledError:
            # WinRT/bleak sometimes cancels stop_notify when tearing down
            pass
        except Exception:
            pass

def _is_wifi_ok(msg: str) -> bool:
    m = msg.strip().lower()
    if m == "none":         # ignore bare ERRSRC “NONE”
        return False
    m = " ".join(m.split())   # collapse weird spacing

    # Prefer an explicit banner; keep a few safe fallbacks
    return (
        m.startswith("wifi: got ip")     # our canonical line from firmware.
        or " ip=" in m                   # includes printed IP (v4 or v6).
        or "sta_got_ip" in m             # ESP-IDF event text (if it leaks).
        or "wifi_err=none" in m          # ERRSRC says “no error”.
    )

def _resolve_by_prefix(svcs) -> tuple[Optional[str], Optional[str], Optional[str], Optional[str], Optional[str]]:
    rx = tx = wifi = errc = alertc = None
    if not svcs:
        return rx, tx, wifi, errc, alertc
    try:
        for s in svcs:
            for c in (getattr(s, "characteristics", []) or []):
                u = str(getattr(c, "uuid", "")).lower()
                if not u:
                    continue
                if   u.startswith(RX_PREFIX):    rx = str(c.uuid)
                elif u.startswith(TX_PREFIX):    tx = str(c.uuid)
                elif u.startswith(WIFI_PREFIX):  wifi = str(c.uuid)
                elif u.startswith(ERR_PREFIX):   errc = str(c.uuid)
                elif u.startswith(ALERT_PREFIX): alertc = str(c.uuid)
            if rx and tx and wifi and errc and alertc:
                break
    except Exception:
        pass
    return rx, tx, wifi, errc, alertc

async def _quick_ble_seen(timeout: float) -> bool:
    if not BLE_AVAILABLE:
        return False
    try:
        devices = await BleakScanner.discover(timeout=timeout)
    except Exception:
        return False
    targets = {n.lower() for n in BLE_ADV_NAMES}
    for d in devices:
        name = (getattr(d, "name", "") or "").lower()
        addr = (getattr(d, "address", "") or "").upper()
        if BLE_ADDR and addr == BLE_ADDR:
            return True
        if name and name in targets:
            return True
    return False

# ---------- TCP ---------- #
def _recvline(sock: socket.socket, maxlen: int = 512, timeout: Optional[float] = 5.0) -> str:
    if timeout is not None:
        sock.settimeout(timeout)
    data = bytearray()
    while len(data) < maxlen:
        b = sock.recv(1)
        if not b or b == b'\n':
            break
        if b != b'\r':
            data += b
    return data.decode(errors="ignore").strip()

def send_line(sock: socket.socket, line: str, timeout: float = 5.0) -> str:
    sock.sendall((line + "\n").encode())
    try:
        reply = _recvline(sock, timeout=timeout)
    except socket.timeout:
        reply = ""
    if reply:
        print(f"[LoPy] {reply}")
    return reply

def connect_and_auth(print_banner: bool = True,
                     connect_timeout: float = 5.0,
                     recv_timeout: float = 2.0) -> Optional[socket.socket]:
    try:
        if print_banner:
            print(f"[TCP] Connecting to {ADDR}:{PORT} ...")
        s = socket.create_connection((ADDR, PORT), timeout=connect_timeout)
        s.settimeout(recv_timeout)
        send_line(s, f"AUTH {TOKEN}", timeout=recv_timeout)
        return s
    except OSError as e:
        if print_banner:
            print(f"[warn] TCP connect failed: {e}")
        return None

def try_reconnect(reason: str) -> Optional[socket.socket]:
    print(f"[warn] TCP error ({reason}) – attempting to reconnect...")

    # brief, fast TCP retries first
    for _ in range(RECONNECT_TRIES):
        s = connect_and_auth(print_banner=False,
                             connect_timeout=TCP_POST_OTA_CONNECT_TIMEOUT,
                             recv_timeout=1.5)
        if s:
            send_line(s, "version", timeout=1.5)
            return s
        time.sleep(RECONNECT_DELAY_S)

    # quick BLE sniff before committing to full BLE session.
    print("[err] Reconnect failed. Falling back to BLE...")
    if BLE_AVAILABLE and asyncio.run(_quick_ble_seen(BLE_QUICK_SCAN_S)):
        wifi_came_up = asyncio.run(ble_session())
        if wifi_came_up:
            s = connect_and_auth(print_banner=False)
            if s:
                print("[info] Reconnected via TCP after BLE recovery.")
            return s
        return None

    # no BLE presence detected; still try a full BLE fallback (best-effort).
    wifi_came_up = asyncio.run(ble_session())
    if wifi_came_up:
        s = connect_and_auth(print_banner=False)
        if s:
            print("[info] Reconnected via TCP after BLE recovery.")
        return s
    return None

# Short ping with tight timeout (used by idle keepalive).
def send_ping(sock: socket.socket) -> bool:
    try:
        sock.sendall(b"PING\n")
        reply = _recvline(sock, timeout=5.0)
        return reply.upper() == "PONG"
    except Exception:
        return False

# ---------- OTA over TCP ---------- #
def ota_tcp(sock: socket.socket, file_path: pathlib.Path) -> Optional[socket.socket]:
    data  = file_path.read_bytes()
    size  = len(data)
    crc32 = zlib.crc32(data) & 0xFFFFFFFF
    print(f"[OTA] {file_path.name}  {size} bytes  CRC 0x{crc32:08X}")

    try:
        sock.sendall(f"OTA {size} {crc32:08X}\n".encode())
        ack = _recvline(sock, timeout=10.0)
    except (OSError, socket.timeout) as e:
        print(f"[OTA] Header send/ack failed: {e}")
        try:
            sock.shutdown(socket.SHUT_RDWR)
        except Exception:
            pass
        try:
            sock.close()
        except Exception:
            pass
        return None

    print(f"[OTA] Received from device: {ack}")
    if "ACK" not in (ack or "").upper():
        print("[OTA] Header rejected – aborting.")
        try:
            sock.shutdown(socket.SHUT_RDWR)
        except Exception:
            pass
        try:
            sock.close()
        except Exception:
            pass
        return None

    print("[OTA] ACK confirmed – starting upload ...")
    start = time.time()
    sock.settimeout(None)

    sent = 0
    last = 0
    try:
        for off in range(0, size, CHUNK):
            chunk = data[off:off + CHUNK]
            sock.sendall(chunk)
            sent += len(chunk)
            if sent - last >= PROGRESS_EVERY or sent == size:
                pct = (sent * 100) // size if size else 100
                print(f"[OTA] {sent}/{size} bytes ({pct}%).")
                last = sent
        sock.sendall(struct.pack("<I", crc32))
        _ = _recvline(sock, timeout=30.0)
    except KeyboardInterrupt:
        print("\n[OTA] Interrupted by user. Device will reject any partial image via CRC.")
        return None
    except (socket.timeout, OSError) as e:
        print(f"[OTA] Socket error during upload: {e}")
        return None
    finally:
        try:
            sock.shutdown(socket.SHUT_RDWR)
        except Exception:
            pass
        try:
            sock.close()
        except Exception:
            pass

    duration = time.time() - start
    kbps = (size / 1024) / duration if duration > 0 else 0
    print(f"[OTA] Upload complete - {duration:.2f}s at {kbps:.1f} KB/s. Device rebooting...")

    # Try to establish a NEW session (this is what we return)
    deadline = time.time() + max(WAIT_AFTER_REBOOT_S, 12)
    first_ble_check_at = time.time() + POST_OTA_TCP_ONLY_WINDOW_S
    s_new: Optional[socket.socket] = None
    print("[OTA] Waiting for device to come back...")

    while time.time() < deadline:
        # 1) Quick TCP probe with a short timeout
        s_try = connect_and_auth(print_banner=False,
                                 connect_timeout=TCP_POST_OTA_CONNECT_TIMEOUT,
                                 recv_timeout=1.5)
        if s_try:
            try:
                reply = send_line(s_try, "version", timeout=1.5)
                if not reply:
                    raise OSError("no reply after OTA")
            except Exception:
                try: s_try.close()
                except: pass
                s_try = None
            else:
                s_new = s_try
                break

        # 2) a brief TCP-only window + a short BLE sniff.
        if time.time() >= first_ble_check_at and BLE_AVAILABLE:
            if asyncio.run(_quick_ble_seen(BLE_QUICK_SCAN_S)):
                print("[OTA] BLE lifeboat detected — switching to BLE now.")
                wifi_ok = asyncio.run(ble_session())  # lets you SETWIFI etc.
                if wifi_ok:
                    # hop back to TCP with normal timeouts.
                    return connect_and_auth(print_banner=False)
                # Either user stayed in BLE or Wi-Fi still not up; keep waiting a bit or exit
                break

        time.sleep(0.4)

    return s_new

# ---------- BLE fallback ---------- #
async def _scan_for_device(timeout: float = 12.0) -> Optional["BLEDevice"]:
    try:
        devices = await BleakScanner.discover(timeout=timeout)
    except Exception as e:
        print(f"[BLE] discover() failed: {e}")
        return None

    targets = {n.lower() for n in BLE_ADV_NAMES}
    for d in devices:
        n = (getattr(d, "name", "") or "").strip()
        a = (getattr(d, "address", "") or "").upper()
        su = []
        meta = getattr(d, "metadata", {}) or {}
        for key in ("uuids", "service_uuids"):
            vals = meta.get(key)
            if vals:
                su.extend(str(u).lower() for u in vals)
        print(f"[BLE] seen: {n or '?'}  {a or '?'}  uuids={su}")

    if BLE_ADDR:
        for d in devices:
            if (getattr(d, "address", "") or "").upper() == BLE_ADDR:
                print(f"[BLE] Matched by MAC: {BLE_ADDR}")
                return d
        print(f"[BLE] MAC {BLE_ADDR} not found in discovery results.")

    for d in devices:
        n = (getattr(d, "name", "") or "").lower()
        if n and n in targets:
            print(f"[BLE] Matched by name: {getattr(d, 'name', '')}")
            return d

    return None

async def _wait_for_services(client: "BleakClient", tries: int = 40, delay: float = 0.4):
    for _ in range(tries):
        svcs = getattr(client, "services", None)
        if not svcs:
            try:
                svcs = await client.get_services()
            except Exception:
                svcs = None
        if svcs:
            try:
                if any(True for _s in svcs for _c in _s.characteristics):
                    return svcs
            except Exception:
                pass
        await asyncio.sleep(delay)
    return None

async def ble_session() -> bool:
    """Return True if Wi-Fi became OK and we should switch back to TCP."""
    if not BLE_AVAILABLE:
        print("[BLE] Bleak not installed; BLE fallback disabled.")
        return False

    print("[BLE] Scanning....")
    dev = await _scan_for_device(timeout=12.0)
    if not dev:
        print("[BLE] No device found.")
        return False

    wifi_ok_event = asyncio.Event()
    expect_none_until = 0.0  # during this window, ERRSRC=="NONE" counts as OK.

    async def _notify(sender, data: bytearray):
        try:
            msg = data.decode(errors="ignore").strip()
        except Exception:
            msg = repr(data)

        if not msg:
            return

        print(f"[BLE] {msg}")
        low = msg.lower()

        # Normal signals OR grace-window acceptance of plain "NONE".
        if _is_wifi_ok(msg) or (low == "none" and time.monotonic() < expect_none_until):
            # tiny grace to let DHCP/stack settle before we jump to TCP.
            await asyncio.sleep(0.4)
            wifi_ok_event.set()

    async with BleakClient(dev) as client:
        print(f"[BLE] Connected to {getattr(dev, 'address', 'unknown')} ({getattr(dev, 'name', '')}).")

        svcs = await _wait_for_services(client)
        if not svcs:
            print("[BLE] Services not visible; disconnecting.")
            return False

        rx_uuid, tx_uuid, wifi_uuid, err_uuid, alert_uuid = _resolve_by_prefix(svcs)
        if not (rx_uuid and tx_uuid and wifi_uuid and err_uuid):
            print("[BLE] Could not resolve expected characteristics by prefix.")
            return False

        if DEBUG_BLE:
            for s in svcs:
                print(f"[BLE] service: {s.uuid}")
                for c in s.characteristics:
                    props = ",".join(sorted(getattr(c, "properties", []) or []))
                    print(f"       char: {c.uuid}  props=[{props}]")

        # Enable notifications.
        notify_ok = False
        try:
            await client.start_notify(tx_uuid, _notify)
            notify_ok = True
            try:
                if err_uuid:
                    await client.start_notify(err_uuid, _notify)
            except Exception:
                pass
            try:
                if alert_uuid:
                    await client.start_notify(alert_uuid, _notify)
            except Exception:
                pass
            print("[BLE] Notify on TX (and ERRSRC/ALERT if present).")
        except Exception:
            print("[BLE] Notify start failed; continuing without notify.")

        # AUTH.
        try:
            await client.write_gatt_char(rx_uuid, f"AUTH {TOKEN}\n".encode())
            if not notify_ok:
                # small read poll to fetch banner if notifications unsupported.
                for _ in range(6):
                    try:
                        data = await client.read_gatt_char(tx_uuid)
                        if data:
                            msg = data.decode(errors="ignore").strip()
                            if msg:
                                print(f"[BLE] {msg}")
                                if _is_wifi_ok(msg):
                                    wifi_ok_event.set()
                            break
                    except Exception:
                        pass
                    await asyncio.sleep(0.1)
            else:
                await asyncio.sleep(0.05)
        except Exception as e:
            print(f"[BLE] AUTH failed: {e}")
            return False

        # Interactive BLE loop.
        while True:
            try:
                line = input("LoPy-BLE> ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                break

            if not line:
                continue

            low = line.lower()
            if low in ("/q", "/quit", "exit"):
                break
            if low.startswith(("/ota ", "ota ")):
                print("[BLE] OTA over BLE is disabled. Use TCP.")
                continue
            if low.startswith(("/setwifi ", "setwifi ")):
                parts = line.split(maxsplit=2)
                if len(parts) == 3:
                    ssid, pwd = parts[1], parts[2]
                    try:
                        payload = (ssid + "\n" + pwd).encode()
                        await client.write_gatt_char(wifi_uuid, payload)

                        # For the next few seconds, a plain "NONE" means success.
                        expect_none_until = time.monotonic() + 12.0
                    except Exception as e:
                        print(f"[BLE] SETWIFI failed: {e}")
                        continue

                    print("[BLE] Waiting up to 20s for Wi-Fi to come up...")
                    try:
                        await asyncio.wait_for(wifi_ok_event.wait(), timeout=20.0)
                        print("[BLE] Wi-Fi is up. Switching to TCP...")
                        if notify_ok:
                            await _stop_notify_quiet(client, tx_uuid, err_uuid, alert_uuid)
                        return True

                    except asyncio.TimeoutError:
                        print("[BLE] Wi-Fi not up yet; staying in BLE.")
                        continue
                else:
                    print("usage: SETWIFI <ssid> <pwd>")
                continue

            # Default: send command to RX.
            try:
                await client.write_gatt_char(rx_uuid, (line + "\n").encode())
                if not notify_ok:
                    # quick read poll for a reply
                    for _ in range(8):
                        try:
                            data = await client.read_gatt_char(tx_uuid)
                            if data:
                                msg = data.decode(errors="ignore").strip()
                                if msg:
                                    print(f"[BLE] {msg}")
                                    if _is_wifi_ok(msg):
                                        wifi_ok_event.set()
                                break
                        except Exception:
                            pass
                        await asyncio.sleep(0.08)
            except Exception as e:
                print(f"[BLE] write failed: {e}")
                break

        # Best-effort cleanup on exit.
        if notify_ok:
            await _stop_notify_quiet(client, tx_uuid, err_uuid, alert_uuid)

    return False

# ---------- Non-blocking REPL helpers ----------#
def _readline_nonblock_windows(buf: list[str], slice_sec: float = 0.2) -> Optional[str]:
    # buf holds the current line as a single-element list so we keep state across calls
    import msvcrt
    deadline = time.time() + slice_sec
    while time.time() < deadline:
        if msvcrt.kbhit():
            ch = msvcrt.getwch()
            if ch in ("\r", "\n"):
                print()
                line = "".join(buf)
                buf.clear()
                return line
            elif ch == "\x08":  # backspace.
                if buf:
                    buf.pop()
                    # erase char from console.
                    print("\b \b", end="", flush=True)
            else:
                buf.append(ch)
                print(ch, end="", flush=True)
        else:
            time.sleep(0.02)
    return None

def _readline_nonblock_posix(slice_sec: float = 0.2) -> Optional[str]:
    import select
    r, _, _ = select.select([sys.stdin], [], [], slice_sec)
    if r:
        line = sys.stdin.readline()
        return line.rstrip("\r\n")
    return None

# ---------- Interactive TCP session with idle keepalive  ---------- #
def tcp_session():
    # Try TCP; if not, fall back to BLE, then retry TCP.
    while True:
        s = connect_and_auth()
        if s:
            break
        wifi_came_up = asyncio.run(ble_session())
        if not wifi_came_up:
            return
        for _ in range(10):
            s = connect_and_auth(print_banner=False)
            if s:
                send_line(s, "version")
                break
            time.sleep(1.0)
        if s:
            break
        print("[warn] Wi-Fi said OK but TCP still not reachable; staying in BLE.")

    print("LoPy> ", end="", flush=True)
    last_ping = time.time()
    first_ping_at = time.time() + 8.0
    win_buf: list[str] = []

    try:
        while True:
            if os.name == "nt":
                line = _readline_nonblock_windows(win_buf, slice_sec=0.2)
            else:
                line = _readline_nonblock_posix(slice_sec=0.2)

            now = time.time()
            if now >= first_ping_at and (now - last_ping) >= 3.0 and not line:
                last_ping = now
                ok = send_ping(s)
                if not ok:
                    s2 = try_reconnect("keepalive failed")
                    if not s2:
                        return
                    s = s2
                    print("LoPy> ", end="", flush=True)
                    continue

            if line is None:
                continue

            line = line.strip()
            if not line:
                print("LoPy> ", end="", flush=True)
                continue

            low = line.lower()
            if low in ("/q", "/quit", "exit"):
                break

            if (low.startswith("/ota ") or low.startswith("ota ")):
                path_str = line.split(None, 1)[1] if len(line.split(None, 1)) == 2 else ""
                bin_path = pathlib.Path(path_str)
                if not bin_path.is_file():
                    print("[err] file not found.")
                    print("LoPy> ", end="", flush=True)
                    continue
                s_new = ota_tcp(s, bin_path)
                if s_new:
                    s = s_new
                    print("[OTA] Reconnected over TCP.")
                    print("LoPy> ", end="", flush=True)
                else:
                    print("[OTA] TCP didn’t come back. Trying BLE fallback...")
                    wifi_came_up = asyncio.run(ble_session())
                    if wifi_came_up:
                        s = connect_and_auth() or s
                        if not s:
                            print("[warn] Still no TCP after BLE recovery.")
                            break
                        print("LoPy> ", end="", flush=True)
                    else:
                        print("[OTA] BLE couldn’t recover. You can keep using BLE or power-cycle.")
                        break
                continue

            if low.startswith("/setwifi ") or low.startswith("setwifi "):
                try:
                    send_line(s, line.strip())
                except OSError as e:
                    s2 = try_reconnect(str(e))
                    if not s2:
                        continue
                    s = s2
                    send_line(s, line.strip())
                print("LoPy> ", end="", flush=True)
                continue

            try:
                send_line(s, line)
            except OSError as e:
                s2 = try_reconnect(str(e))
                if not s2:
                    continue
                s = s2
                send_line(s, line)
            print("LoPy> ", end="", flush=True)

    except KeyboardInterrupt:
        print("\n[info] Session interrupted by user.")
    finally:
        try:
            s.shutdown(socket.SHUT_RDWR)
        except Exception:
            pass
        try:
            s.close()
        except Exception:
            pass

########
# Main #
########
def main():
    tcp_session()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[info] Session interrupted by user.")
    finally:
        print("[info] Bye.")