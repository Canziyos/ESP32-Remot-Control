import time
import sys
from typing import Optional, Tuple

def is_wifi_ok(msg: str) -> bool:
    m = (msg or "").strip().lower()
    if m == "none":  # ignore bare ERRSRC “NONE” unless caller opens grace window.
        return False
    m = " ".join(m.split())
    return (
        m.startswith("wifi: got ip")
        or " ip=" in m
        or "sta_got_ip" in m
        or "wifi_err=none" in m
    )

async def stop_notify_quiet(client, *uuids):
    from asyncio import CancelledError
    for u in uuids:
        if not u:
            continue
        try:
            await client.stop_notify(u)
        except CancelledError:
            pass
        except Exception:
            pass
def resolve_by_suffix(services, suffix: str):
    suf = (suffix or "").lower()
    try:
        for s in services or []:
            for c in (getattr(s, "characteristics", []) or []):
                u = str(getattr(c, "uuid", "")).lower()
                if u.endswith(suf):
                    return str(c.uuid)
    except Exception:
        pass
    return None

def resolve_by_prefix(services, prefixes) -> Tuple[Optional[str], ...]:
    """Return UUIDs by given prefix order. prefixes = [RX, TX, WIFI, ERRSRC, ALERT, OTA_CTRL, OTA_DATA]"""
    outs = [None] * len(prefixes)
    try:
        for s in services or []:
            for c in (getattr(s, "characteristics", []) or []):
                u = str(getattr(c, "uuid", "")).lower()
                if not u:
                    continue
                for i, pref in enumerate(prefixes):
                    if outs[i] is None and u.startswith(pref):
                        outs[i] = str(c.uuid)
            if all(outs):
                break
    except Exception:
        pass
    return tuple(outs)

# -----console input (Non-blocking ) -----
def readline_nonblock_windows(buf: list[str], slice_sec: float = 0.2) -> Optional[str]:
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
            elif ch == "\x08":  # backspace
                if buf:
                    buf.pop()
                    print("\b \b", end="", flush=True)
            else:
                buf.append(ch)
                print(ch, end="", flush=True)
        else:
            time.sleep(0.02)
    return None

def readline_nonblock_posix(slice_sec: float = 0.2) -> Optional[str]:
    import select
    r, _, _ = select.select([sys.stdin], [], [], slice_sec)
    if r:
        line = sys.stdin.readline()
        return line.rstrip("\r\n")
    return None
def parse_dht_line(msg: str):
    """
    Parse lines like: 'DHT T=23.4C RH=47.1%'.
    Returns (temp_c: float|None, rh: float|None) or (None, None) on mismatch.
    """
    try:
        m = msg.strip()
        if not m.lower().startswith("dht "):
            return (None, None)
        # Very permissive parse
        import re
        t = re.search(r"[tT]\s*=\s*([+-]?\d+(?:\.\d+)?)\s*[cC]", m)
        h = re.search(r"[rR][hH]\s*=\s*([+-]?\d+(?:\.\d+)?)\s*%", m)
        return (float(t.group(1)) if t else None, float(h.group(1)) if h else None)
    except Exception:
        return (None, None)
