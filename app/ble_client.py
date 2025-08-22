import asyncio, struct, zlib, time
from typing import Optional, Tuple
from . import config as C
from utils import stop_notify_quiet, is_wifi_ok, resolve_by_prefix

# BLE import (keep same behavior)
try:
    from bleak import BleakClient, BleakScanner
    from bleak.backends.device import BLEDevice
    BLE_AVAILABLE = True
except Exception:
    BLE_AVAILABLE = False
    BLEDevice = object

# ---------- Small helpers ---------- #
async def quick_ble_seen(timeout: float) -> bool:
    if not BLE_AVAILABLE:
        return False
    try:
        devices = await BleakScanner.discover(timeout=timeout)
    except Exception:
        return False
    targets = {n.lower() for n in C.BLE_ADV_NAMES}
    for d in devices:
        name = (getattr(d, "name", "") or "").lower()
        addr = (getattr(d, "address", "") or "").upper()
        if C.BLE_ADDR and addr == C.BLE_ADDR:
            return True
        if name and name in targets:
            return True
    return False

async def _scan_for_device(timeout: float = 12.0) -> Optional["BLEDevice"]:
    if not BLE_AVAILABLE:
        return None
    try:
        devices = await BleakScanner.discover(timeout=timeout)
    except Exception as e:
        print(f"[BLE] discover() failed: {e}")
        return None

    targets = {n.lower() for n in C.BLE_ADV_NAMES}
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

    if C.BLE_ADDR:
        for d in devices:
            if (getattr(d, "address", "") or "").upper() == C.BLE_ADDR:
                print(f"[BLE] Matched by MAC: {C.BLE_ADDR}")
                return d
        print(f"[BLE] MAC {C.BLE_ADDR} not found in discovery results.")

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

# ---------- OTA over BLE ---------- #
async def ble_ota_upload(client, ctrl_uuid: str, data_uuid: str, tx_uuid: str, bin_path):
    data  = bin_path.read_bytes()
    size  = len(data)
    crc32 = zlib.crc32(data) & 0xFFFFFFFF
    print(f"[BLE-OTA] {bin_path.name}  {size} bytes  CRC 0x{crc32:08X}")

    try:
        await client.write_gatt_char(ctrl_uuid, f"BL_OTA START {size} {crc32:08X}".encode(), response=True)
    except Exception as e:
        print(f"[BLE-OTA] START failed: {e}")
        return False

    mtu = getattr(client, "mtu_size", None) or 23
    frame_payload = max(8, min(180, mtu - 3 - 6))

    seq = 0
    off = 0
    last_prog = 0
    try:
        while off < size:
            chunk = data[off:off+frame_payload]
            hdr = struct.pack("<IH", seq, len(chunk))
            await client.write_gatt_char(data_uuid, hdr + chunk, response=False)
            seq += 1
            off += len(chunk)

            if off - last_prog >= 64*1024:
                last_prog = off
                pct = (off * 100) // size
                print(f"[BLE-OTA] {off}/{size} ({pct}%)")
                await asyncio.sleep(0)
    except Exception as e:
        print(f"[BLE-OTA] DATA failed at seq {seq}: {e}")
        try:
            await client.write_gatt_char(ctrl_uuid, b"BL_OTA ABORT", response=True)
        except Exception:
            pass
        return False

    await asyncio.sleep(0.45)

    for attempt in range(2):
        try:
            await client.write_gatt_char(ctrl_uuid, b"BL_OTA FINISH", response=True)
            print("[BLE-OTA] FINISH sent.")
            print("[BLE-OTA] Done; device should reboot.")
            return True
        except Exception as e:
            if off >= size:
                print(f"[BLE-OTA] FINISH send failed ({e}); data complete. Treating as success (device likely rebooting).")
                return True
            if attempt == 0:
                await asyncio.sleep(0.5)
                continue
            print(f"[BLE-OTA] FINISH failed: {e}")
            return False

# ---------- Interactive BLE session ---------- #
async def ble_session() -> bool:
    """Return True if Wi-Fi became OK and we should switch back to TCP."""
    if not BLE_AVAILABLE:
        return False

    print("[BLE] Scanning....")
    dev = await _scan_for_device(timeout=12.0)
    if not dev:
        print("[BLE] No device found.")
        return False

    wifi_ok_event = asyncio.Event()
    expect_none_until = 0.0

    async def _notify(sender, data: bytearray):
        nonlocal expect_none_until
        try:
            msg = data.decode(errors="ignore").strip()
        except Exception:
            msg = repr(data)
        if not msg:
            return

        print(f"[BLE] {msg}")
        low = msg.lower()
        if is_wifi_ok(msg) or (low == "none" and time.monotonic() < expect_none_until):
            await asyncio.sleep(0.4)
            wifi_ok_event.set()

    async with BleakClient(dev) as client:
        print(f"[BLE] Connected to {getattr(dev, 'address', 'unknown')} ({getattr(dev, 'name', '')}).")

        svcs = await _wait_for_services(client)
        if not svcs:
            print("[BLE] Services not visible; disconnecting.")
            return False

        # Include DHT prefix at the end; if device doesn't expose it, dht_uuid will be None.
        rx_uuid, tx_uuid, wifi_uuid, err_uuid, alert_uuid, ota_ctrl_uuid, ota_data_uuid, dht_uuid = \
            resolve_by_prefix(
                svcs,
                [C.RX_PREFIX, C.TX_PREFIX, C.WIFI_PREFIX, C.ERR_PREFIX, C.ALERT_PREFIX,
                 C.OTA_CTRL_PREFIX, C.OTA_DATA_PREFIX, C.DHT_PREFIX]
            )

        if not (rx_uuid and tx_uuid and wifi_uuid and err_uuid):
            print("[BLE] Could not resolve expected characteristics by prefix.")
            return False

        if C.DEBUG_BLE:
            for s in svcs:
                print(f"[BLE] service: {s.uuid}")
                for c in s.characteristics:
                    props = ",".join(sorted(getattr(c, "properties", []) or []))
                    print(f"       char: {c.uuid}  props=[{props}]")

        # Enable notifications (TX/ERR/ALERT always; DHT if present)
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
            # Optional DHT stream
            try:
                if dht_uuid:
                    async def _notify_dht(sender, data: bytearray):
                        try:
                            msg = data.decode(errors="ignore").strip()
                        except Exception:
                            msg = repr(data)
                        if msg:
                            print(f"[BLE][DHT] {msg}")
                    await client.start_notify(dht_uuid, _notify_dht)
            except Exception:
                pass
            print("[BLE] Notify on TX (and ERRSRC/ALERT if present).")
        except Exception:
            print("[BLE] Notify start failed; continuing without notify.")

        # AUTH
        try:
            await client.write_gatt_char(rx_uuid, f"AUTH {C.TOKEN}\n".encode())
            if not notify_ok:
                for _ in range(6):
                    try:
                        data = await client.read_gatt_char(tx_uuid)
                        if data:
                            msg = data.decode(errors="ignore").strip()
                            if msg:
                                print(f"[BLE] {msg}")
                                if is_wifi_ok(msg):
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

            # BLE-OTA
            if low.startswith(("/ota ", "ota ")):
                import pathlib
                path_str = line.split(None, 1)[1] if len(line.split(None, 1)) == 2 else ""
                bin_path = pathlib.Path(path_str)
                if not bin_path.is_file():
                    print("[BLE-OTA] file not found.")
                    continue
                if not (ota_ctrl_uuid and ota_data_uuid):
                    print("[BLE-OTA] Device lacks BLE-OTA characteristics.")
                    continue

                ok = await ble_ota_upload(client, ota_ctrl_uuid, ota_data_uuid, tx_uuid, bin_path)
                if ok:
                    await asyncio.sleep(2.0)
                    return False
                return False

            # SETWIFI passthrough
            if low.startswith(("/setwifi ", "setwifi ")):
                parts = line.split(maxsplit=2)
                if len(parts) == 3:
                    ssid, pwd = parts[1], parts[2]
                    try:
                        payload = (ssid + "\n" + pwd).encode()
                        await client.write_gatt_char(wifi_uuid, payload)
                        # For a short window, plain "NONE" means success.
                        expect_none_until = time.monotonic() + 12.0
                    except Exception as e:
                        print(f"[BLE] SETWIFI failed: {e}")
                        continue

                    print("[BLE] Waiting up to 20s for Wi-Fi to come up...")
                    try:
                        await asyncio.wait_for(wifi_ok_event.wait(), timeout=20.0)
                        print("[BLE] Wi-Fi is up. Switching to TCP...")
                        if notify_ok:
                            await stop_notify_quiet(client, tx_uuid, err_uuid, alert_uuid, dht_uuid)
                        return True
                    except asyncio.TimeoutError:
                        print("[BLE] Wi-Fi not up yet; staying in BLE.")
                        continue
                else:
                    print("usage: SETWIFI <ssid> <pwd>")
                continue

            # Manual one-shot DHT read (works even if we didn't enable DHT notify)
            if low in ("dht", "dht?", "/dht"):
                if dht_uuid:
                    try:
                        data = await client.read_gatt_char(dht_uuid)
                        try:
                            msg = data.decode(errors="ignore").strip()
                        except Exception:
                            msg = repr(data)
                        print(f"[BLE][DHT] {msg if msg else '<empty>'}")
                    except Exception as e:
                        print(f"[BLE] DHT read failed: {e}")
                else:
                    print("[BLE] Device has no DHT characteristic.")
                continue

            # Default: command to RX
            try:
                await client.write_gatt_char(rx_uuid, (line + "\n").encode())
                if not notify_ok:
                    for _ in range(8):
                        try:
                            data = await client.read_gatt_char(tx_uuid)
                            if data:
                                msg = data.decode(errors="ignore").strip()
                                if msg:
                                    print(f"[BLE] {msg}")
                                    if is_wifi_ok(msg):
                                        wifi_ok_event.set()
                                break
                        except Exception:
                            pass
                        await asyncio.sleep(0.08)
            except Exception as e:
                print(f"[BLE] write failed: {e}")
                break

        if notify_ok:
            await stop_notify_quiet(client, tx_uuid, err_uuid, alert_uuid, dht_uuid)

    return False
