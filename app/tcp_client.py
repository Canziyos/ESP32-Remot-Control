import socket, struct, time, zlib, os
from typing import Optional
from . import config as C

# ---------- TCP low-level ---------- #
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
            print(f"[TCP] Connecting to {C.ADDR}:{C.PORT} ...")
        s = socket.create_connection((C.ADDR, C.PORT), timeout=connect_timeout)
        s.settimeout(recv_timeout)
        send_line(s, f"AUTH {C.TOKEN}", timeout=recv_timeout)
        return s
    except OSError as e:
        if print_banner:
            print(f"[warn] TCP connect failed: {e}")
        return None

def send_ping(sock: socket.socket) -> bool:
    try:
        sock.sendall(b"PING\n")
        reply = _recvline(sock, timeout=5.0)
        return reply.upper() == "PONG"
    except Exception:
        return False

# ---------- OTA over TCP ---------- #
def ota_tcp(sock: socket.socket, file_path) -> Optional[socket.socket]:
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
        for off in range(0, size, C.CHUNK):
            chunk = data[off:off + C.CHUNK]
            sock.sendall(chunk)
            sent += len(chunk)
            if sent - last >= C.PROGRESS_EVERY or sent == size:
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

    # Try to establish a new session (this is what we return)
    deadline = time.time() + max(C.WAIT_AFTER_REBOOT_S, 12)
    first_ble_check_at = time.time() + C.POST_OTA_TCP_ONLY_WINDOW_S
    s_new: Optional[socket.socket] = None
    print("[OTA] Waiting for device to come back...")

    # Let the orchestrator (session.py) decide if/when to jump to BLE.
    # Here: quick TCP probes. the caller will handle BLE as needed.
    while time.time() < deadline:
        s_try = connect_and_auth(print_banner=False,
                                 connect_timeout=C.TCP_POST_OTA_CONNECT_TIMEOUT,
                                 recv_timeout=1.5)
        if s_try:
            try:
                reply = send_line(s_try, "version", timeout=1.5)
                if reply:
                    s_new = s_try
                    break
            except Exception:
                try: s_try.close()
                except: pass
        time.sleep(0.4)

    return s_new
