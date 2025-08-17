# scan.py
import asyncio, sys
try:
    from bleak import BleakScanner
except Exception as e:
    print("[ERR] bleak not installed or import failed:", e)
    raise

if sys.platform.startswith("win"):
    # Important on Windows to avoid silent scan issues
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

async def main():
    print("[BLE] Scanning for 8s…")
    try:
        devices = await BleakScanner.discover(timeout=8.0)
    except Exception as e:
        print("[ERR] discover() failed:", e)
        return
    if not devices:
        print("[BLE] No devices discovered.")
        return
    for d in devices:
        name = (getattr(d, "name", "") or "").strip() or "?"
        addr = (getattr(d, "address", "") or "").upper()
        uuids = []
        meta = getattr(d, "metadata", {}) or {}
        for k in ("uuids", "service_uuids"):
            v = meta.get(k)
            if v:
                uuids.extend([str(u).lower() for u in v])
        print(f"[BLE] {name:20s}  {addr:17s}  uuids={uuids}")

if __name__ == "__main__":
    asyncio.run(main())
