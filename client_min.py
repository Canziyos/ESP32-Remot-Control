#!/usr/bin/env python3
# client_min.py — minimal TCP REPL for LoPy4.

import os
import socket

ADDR  = os.getenv("LOPY_ADDR",  "192.168.10.125")
PORT  = int(os.getenv("LOPY_PORT", "8080"))
TOKEN = os.getenv("LOPY_TOKEN", "hunter2")

def recvline(sock: socket.socket, maxlen: int = 512, timeout: float = 5.0) -> str:
    """Read a single line terminated by \\n, trimming \\r, with a timeout."""
    sock.settimeout(timeout)
    buf = bytearray()
    while len(buf) < maxlen:
        b = sock.recv(1)
        if not b or b == b"\n":
            break
        if b != b"\r":
            buf += b
    return buf.decode(errors="ignore").strip()

def send_line(sock: socket.socket, line: str) -> str:
    sock.sendall((line + "\n").encode())
    try:
        return recvline(sock, timeout=5.0)
    except socket.timeout:
        return ""

def main():
    print(f"[TCP] Connecting to {ADDR}:{PORT}.")
    try:
        s = socket.create_connection((ADDR, PORT), timeout=5)
    except OSError as e:
        print(f"[err] Connect failed: {e}.")
        return

    # Authenticate once.
    print(f"[TCP] AUTH {TOKEN}.")
    auth_reply = send_line(s, f"AUTH {TOKEN}")
    if auth_reply:
        print(f"[LoPy] {auth_reply}.")
    else:
        print("[warn] No reply to AUTH (continuing anyway).")

    # Simple REPL.
    try:
        while True:
            try:
                line = input("LoPy> ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                break
            if not line:
                continue
            if line.lower() in ("/q", "/quit", "exit"):
                break

            reply = send_line(s, line)
            if reply:
                print(f"[LoPy] {reply}.")
            else:
                print("[LoPy] (no reply).")
    except OSError as e:
        print(f"[err] Socket error: {e}.")
    finally:
        try:
            s.shutdown(socket.SHUT_RDWR)
        except Exception:
            pass
        try:
            s.close()
        except Exception:
            pass
        print("[TCP] Closed.")

if __name__ == "__main__":
    main()
