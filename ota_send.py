#!/usr/bin/env python3
import os, socket, struct, zlib, pathlib, time, asyncio
from typing import Optional

# ---------- Optional BLE ----------
try:
    from bleak import BleakClient, BleakScanner
    from bleak.backends.device import BLEDevice
    BLE_AVAILABLE = True
except Exception:
    BLE_AVAILABLE = False
    BLEDevice = object

# ---------- Config ----------
ADDR   = os.getenv("LOPY_ADDR",  "192.168.10.125")
PORT   = int(os.getenv("LOPY_PORT", "8080"))
TOKEN  = os.getenv("LOPY_TOKEN", "hunter2")
DEBUG_BLE = True

CHUNK  = 16 * 1024
PROGRESS_EVERY = 256 * 1024

WAIT_AFTER_REBOOT_S = 10
RECONNECT_TRIES     = 12
RECONNECT_DELAY_S   = 1.0

# We resolve by prefix, so we don't care about full UUID endian flips.
# efbe0100 = RX (write), efbe0200 = TX (notify/read), efbe0300 = WIFI, efbe0400 = ERRSRC
RX_PREFIX    = "efbe0100"
TX_PREFIX    = "efbe0200"
WIFI_PREFIX  = "efbe0300"
ERR_PREFIX   = "efbe0400"
ALERT_PREFIX = "efbe0500"

BLE_ADV_NAMES = [n.strip() for n in os.getenv("LOPY_BLE_NAMES", "LoPy4").split(",") if n.strip()]
BLE_ADDR = (os.getenv("LOPY_BLE_ADDR", "") or "").replace("-", ":").upper()

# ---------- Helpers ----------
def _is_wifi_ok(msg: str) -> bool:
    m = msg.strip().lower()
    return ("wifi_err=none" in m) or (m == "none") or ("got ip" in m)

def _resolve_by_prefix(svcs) -> tuple[Optional[str], Optional[str], Optional[str], Optional[str], Optional[str]]:
    rx = tx = wifi = errc = alertc = None
    try:
        for s in svcs:
            for c in s.characteristics:
                u = str(c.uuid).lower()
                if   u.startswith(RX_PREFIX):    rx     = c.uuid
                elif u.startswith(TX_PREFIX):    tx     = c.uuid
                elif u.startswith(WIFI_PREFIX):  wifi   = c.uuid
                elif u.startswith(ERR_PREFIX):   errc   = c.uuid
                elif u.startswith(ALERT_PREFIX): alertc = c.uuid   # <— add this
    except Exception:
        pass
    return rx, tx, wifi, errc, alertc

# ---------- TCP ----------
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

def send_line(sock: socket.socket, line: str) -> str:
    sock.sendall((line + "\n").encode())
    try:
        reply = _recvline(sock, timeout=5.0)
    except socket.timeout:
        reply = ""
    if reply:
        print(f"[LoPy] {reply}")
    return reply

def connect_and_auth(print_banner: bool = True) -> Optional[socket.socket]:
    try:
        if print_banner:
            print(f"[TCP] Connecting to {ADDR}:{PORT} …")
        s = socket.create_connection((ADDR, PORT), timeout=5)
        s.settimeout(2.0)
        send_line(s, f"AUTH {TOKEN}")
        return s
    except OSError as e:
        if print_banner:
            print(f"[warn] TCP connect failed: {e}")
        return None

def try_reconnect(reason: str) -> Optional[socket.socket]:
    print(f"[warn] TCP error ({reason}) – attempting to reconnect…")
    for _ in range(RECONNECT_TRIES):
        s = connect_and_auth(print_banner=False)
        if s:
            send_line(s, "version")
            return s
        time.sleep(RECONNECT_DELAY_S)
    print("[err] Reconnect failed.")
    return None

# ---------- OTA over TCP ----------
def ota_tcp(sock: socket.socket, file_path: pathlib.Path) -> Optional[socket.socket]:
    data  = file_path.read_bytes()
    size  = len(data)
    crc32 = zlib.crc32(data) & 0xFFFFFFFF
    print(f"[OTA] {file_path.name}  {size} bytes  CRC 0x{crc32:08X}")

    # Guard the header send + ACK read (this is where the socket often dies)
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

    print("[OTA] ACK confirmed – starting upload …")
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
        _ = _recvline(sock, timeout=30.0)  # expect "OK" then reboot.
    except KeyboardInterrupt:
        print("\n[OTA] Interrupted by user. Device will reject any partial image via CRC.")
        return None
    except (socket.timeout, OSError) as e:
        print(f"[OTA] Socket error during upload: {e}")
        return None
    finally:
        # this socket is stale after reboot; close it
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
    print(f"[OTA] Upload complete - {duration:.2f}s at {kbps:.1f} KB/s. Device rebooting…")

    # Try to establish a NEW session (this is what we return)
    deadline = time.time() + max(WAIT_AFTER_REBOOT_S, 12)
    s_new: Optional[socket.socket] = None
    print("[OTA] Waiting for device to come back…")
    while time.time() < deadline:
        s_try = connect_and_auth(print_banner=False)
        if s_try:
            send_line(s_try, "version")
            s_new = s_try
            break
        time.sleep(0.5)

    return s_new


# ---------- BLE fallback ----------
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

    print("[BLE] Scanning…")
    dev = await _scan_for_device(timeout=12.0)
    if not dev:
        print("[BLE] No device found.")
        return False

    wifi_ok_event = asyncio.Event()

    async def _notify(sender, data: bytearray):
        try:
            msg = data.decode(errors="ignore").strip()
        except Exception:
            msg = repr(data)
        if msg:
            print(f"[BLE] {msg}")
            if _is_wifi_ok(msg):
                wifi_ok_event.set()

    async with BleakClient(dev, timeout=25.0 ) as client:
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

        # Enable notifications
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
                # AUTH
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

        # Interactive BLE loop
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
                    except Exception as e:
                        print(f"[BLE] SETWIFI failed: {e}")
                        continue

                    print("[BLE] Waiting up to 20s for Wi-Fi to come up…")
                    try:
                        await asyncio.wait_for(wifi_ok_event.wait(), timeout=20.0)
                        print("[BLE] Wi-Fi is up. Switching to TCP…")
                        return True
                    except asyncio.TimeoutError:
                        print("[BLE] Wi-Fi not up yet; staying in BLE.")
                        continue
                else:
                    print("usage: SETWIFI <ssid> <pwd>")
                continue

            # Default: send command to RX
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

        # Clean up (best effort)
        try:
            if notify_ok:
                await client.stop_notify(tx_uuid)
                try:
                    await client.stop_notify(err_uuid)
                except Exception:
                    pass
                try:
                    if alert_uuid:
                        await client.stop_notify(alert_uuid)
                except Exception:
                    pass
        except Exception:
            pass
    return False

# ---------- Interactive TCP session ----------
def tcp_session():
    s = connect_and_auth()
    if not s:
        wifi_came_up = asyncio.run(ble_session())
        if not wifi_came_up:
            return
        s = connect_and_auth()
        if not s:
            print("[warn] Wi-Fi reported OK, but TCP still not reachable yet.")
            return

    while True:
        try:
            line = input("LoPy> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break

        if not line:
            continue

        low = line.lower()
        if low in ("/q", "/quit", "exit"):
            break

        if low.startswith("/ota ") or low.startswith("ota "):
            path_str = line.split(None, 1)[1] if len(line.split(None, 1)) == 2 else ""
            bin_path = pathlib.Path(path_str)
            if not bin_path.is_file():
                print("[err] file not found.")
                continue

            s_new = ota_tcp(s, bin_path)   # returns a fresh socket or None
            if s_new:
                s = s_new
                print("[OTA] Reconnected over TCP.")
            else:
                print("[OTA] TCP didn’t come back. Trying BLE fallback…")
                wifi_came_up = asyncio.run(ble_session())
                if wifi_came_up:
                    s = connect_and_auth() or s
                    if not s:
                        print("[warn] Still no TCP after BLE recovery.")
                        break
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
            continue

        # Regular command
        try:
            send_line(s, line)
        except OSError as e:
            s2 = try_reconnect(str(e))
            if not s2:
                continue
            s = s2
            send_line(s, line)

    try:
        s.shutdown(socket.SHUT_RDWR)
        s.close()
    except Exception:
        pass

# ---------- Main ----------
def main():
    tcp_session()

if __name__ == "__main__":
    main()
