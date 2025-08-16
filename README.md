
# ESP32 (LoPy4) – Wi-Fi + BLE Remote Control

Small control plane for an ESP32: **TCP when Wi-Fi is up**, **BLE fallback when it’s not**.
Plain-text commands, OTA over TCP, and a tiny alerts pipeline.

## What it does

* **TCP control** (Wi-Fi STA) with simple line-based commands.
* **OTA over TCP** (size + CRC32, then data).
* **BLE fallback** GATT service:

  * RX (write commands), TX (status/echo), WIFI (write credentials),
  * ERRSRC (string error reason), ALERT (fw events).
* **Consistent replies**: BLE path mirrors TCP path (echo/status lines).
* **Alerts module**: raise firmware events; latest snapshot is readable, notifications on change.
* **Command bus**: commands fan out to modules; LED is just the demo target.

## TCP commands (plain text)

| Command                       | Reply / Effect    |
| ----------------------------- | ----------------- |
| `AUTH <pwd>`                  | `OK` (if correct) |
| `PING`                        | `PONG`            |
| `version`                     | e.g. `1.0.0`      |
| `led_on`                      | `led_on`          |
| `led_off`                     | `led_off`         |
| `OTA <size> <crc32>` + binary | handled by tool   |

> Note: some commands are case-sensitive (`PING` vs `ping`) (will be fixed later).

## BLE GATT layout (private 128-bit UUIDs, shown by tail)

Service: `efbe0000-fbfb-fbfb-fb4b-494545434956`

| Char (tail) | Props         | Purpose / Format                                                    |
| ----------- | ------------- | ------------------------------------------------------------------- |
| `efbe0100`  | Write/WriteNR | **RX** — send commands (`"AUTH ...\n"`, `"led_on\n"`, …)            |
| `efbe0200`  | Notify/Read   | **TX** — status/echo lines (newline-delimited)                      |
| `efbe0300`  | Write         | **WIFI** — credentials: `"<ssid>\n<pwd>"` or `SETWIFI <ssid> <pwd>` |
| `efbe0400`  | Notify/Read   | **ERRSRC** — string like `NONE`, `NO_AP`, `AUTH_FAIL`, …            |
| `efbe0500`  | Notify/Read   | **ALERT** — latest alert line, e.g. `ALERT seq=1 code=4 smoke test` |

Subscribe (CCCD) to **TX**, **ERRSRC**, and **ALERT** to get pushes. Reads always return the latest value.

### ALERTs (firmware events)

* Codes are in `alerts.h` (`ALERT_OTA_APPLY_FAIL`, `ALERT_WATCHDOG_RESET`, …).
* Each alert bumps a sequence and updates a short detail string.
* Format: `ALERT seq=<n> code=<code> <detail>`

## Host tool

`ota_send.py` is an interactive client:

* Tries **TCP first**, falls back to **BLE** (Bleak).
* Subscribes to **TX/ERRSRC/ALERT** on BLE.
* Handles OTA over TCP automatically.

Env:

```
LOPY_ADDR, LOPY_PORT, LOPY_TOKEN, LOPY_BLE_NAMES
```

Run:

```bash
python ota_send.py
# then at the prompt:
LoPy> AUTH hunter2
LoPy> version
LoPy> led_on
LoPy> OTA build/firmware.bin   # handled by the tool
```

## Build + flash

Requires ESP-IDF v5.x and a LoPy4.

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Architecture (short)

```
Wi-Fi (STA) ──► TCP server (8080) ──► command parser ──► command bus ──► modules (LED, OTA, …)
                    ▲
BLE fallback ───────┘ (RX/TX mirror) + WIFI cred write + ERRSRC + ALERT
```

## Files (map)

* `wifi_tcp.*` – TCP server + OTA framing
* `gatt_server.*` – BLE service (RX/TX/WIFI/ERRSRC/ALERT)
* `alerts.*` – alert pipeline (`alert_raise`, `alert_latest`, `alerts_subscribe`)
* `command.*` – plain-text command dispatch
* `monitor.*` – health/keepalive hooks
* `ble_fallback.*` – advertising/connect gating
* `ota_send.py` – host client (TCP+BLE)

## Notes

* When Wi-Fi isn’t ready, **ERRSRC** will say things like `NO_AP`.
* BLE path echoes the same replies you’d see over TCP (`led_on`, `WHAT`, `PONG`, etc.).
* Credentials over BLE are plain text (by design here, since it’s a fallback/setup path).

---
