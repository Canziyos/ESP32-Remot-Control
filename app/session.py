import time, asyncio, pathlib, os
from typing import Optional
from . import config as C
from . import tcp_client as T
from . import ble_client as B
from . import utils as U

def try_reconnect(reason: str) -> Optional["socket.socket"]:
    import socket
    print(f"[warn] TCP error ({reason}) – attempting to reconnect...")

    # brief, fast TCP retries first
    for _ in range(C.RECONNECT_TRIES):
        s = T.connect_and_auth(print_banner=False,
                               connect_timeout=C.TCP_POST_OTA_CONNECT_TIMEOUT,
                               recv_timeout=1.5)
        if s:
            T.send_line(s, "version", timeout=1.5)
            return s
        time.sleep(C.RECONNECT_DELAY_S)

    # quick BLE sniff before committing to full BLE session.
    print("[err] Reconnect failed. Falling back to BLE...")
    if B.BLE_AVAILABLE and asyncio.run(B.quick_ble_seen(C.BLE_QUICK_SCAN_S)):
        wifi_came_up = asyncio.run(B.ble_session())
        if wifi_came_up:
            s = T.connect_and_auth(print_banner=False)
            if s:
                print("[info] Reconnected via TCP after BLE recovery.")
            return s
        return None

    wifi_came_up = asyncio.run(B.ble_session())
    if wifi_came_up:
        s = T.connect_and_auth(print_banner=False)
        if s:
            print("[info] Reconnected via TCP after BLE recovery.")
        return s
    return None

def tcp_session():
    import socket
    # Try TCP; else BLE; then retry TCP — keep session alive.
    while True:
        s = T.connect_and_auth()
        if s:
            break
        wifi_came_up = asyncio.run(B.ble_session())
        if not wifi_came_up:
            continue
        for _ in range(10):
            s = T.connect_and_auth(print_banner=False)
            if s:
                T.send_line(s, "version")
                break
        if not s:
            print("[warn] TCP still down; staying on BLE.")
            continue

    print("LoPy> ", end="", flush=True)
    last_ping = time.time()
    first_ping_at = time.time() + 8.0
    win_buf: list[str] = []

    try:
        while True:
            if os.name == "nt":
                line = U.readline_nonblock_windows(win_buf, slice_sec=0.2)
            else:
                line = U.readline_nonblock_posix(slice_sec=0.2)

            now = time.time()
            if now >= first_ping_at and (now - last_ping) >= 3.0 and not line:
                last_ping = now
                ok = T.send_ping(s)
                if not ok:
                    s2 = try_reconnect("keepalive failed")
                    if not s2:
                        # Drop into BLE and keep the session alive.
                        wifi_ok = asyncio.run(B.ble_session())
                        if wifi_ok:
                            s2 = T.connect_and_auth(print_banner=False)
                            if s2:
                                s = s2
                                print("LoPy> ", end="", flush=True)
                                continue
                        print("LoPy> ", end="", flush=True)
                        continue
                    else:
                        s = s2
                        last_ping   = time.time()
                        first_ping_at = time.time() + 8.0
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

                s_new = T.ota_tcp(s, bin_path)
                if s_new:
                    s = s_new
                    print("[OTA] Reconnected over TCP.")
                    print("LoPy> ", end="", flush=True)
                    continue
                else:
                    print("[OTA] TCP didn’t come back. Trying BLE fallback...")
                    wifi_ok = asyncio.run(B.ble_session())
                    if wifi_ok:
                        s2 = T.connect_and_auth(print_banner=False)
                        if s2:
                            s = s2
                            print("[OTA] Reconnected over TCP.")
                    else:
                        print("[OTA] Staying on BLE; TCP still down.")
                    print("LoPy> ", end="", flush=True)
                    continue

            if low.startswith("/setwifi ") or low.startswith("setwifi "):
                try:
                    T.send_line(s, line.strip())
                except OSError as e:
                    s2 = try_reconnect(str(e))
                    if not s2:
                        continue
                    s = s2
                    T.send_line(s, line.strip())
                print("LoPy> ", end="", flush=True)
                continue

            try:
                T.send_line(s, line)
            except OSError as e:
                s2 = try_reconnect(str(e))
                if not s2:
                    continue
                s = s2
                T.send_line(s, line)
            print("LoPy> ", end="", flush=True)

    except KeyboardInterrupt:
        print("\n[info] Session interrupted by user.")
    finally:
        try:
            s.shutdown(socket.SHUT_RDWR)  # type: ignore
        except Exception:
            pass
        try:
            s.close()  # type: ignore
        except Exception:
            pass
