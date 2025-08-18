![Target-ESP32](https://img.shields.io/badge/Target-ESP32%20%28LoPy4%29-blue)
![Primary-TCP](https://img.shields.io/badge/Primary-TCP%20%28Wi%E2%80%91Fi%29-brightgreen)
![Fallback-BLE](https://img.shields.io/badge/Fallback-BLE-orange)
![OTA-TCP%20%2B%20BLE](https://img.shields.io/badge/OTA-TCP%20%2B%20BLE%20%28recovery%29-purple)

# ESP32 (LoPy4) – Wi-Fi + BLE Remote Control  

Tiny control plane for ESP32. **TCP over Wi-Fi when up**, **BLE lifeboat when not**.
Plain-text commands, OTA over TCP (even OTA over BLE the ultimate outpost), alerts, and rollback that actually behaves.

## What it does

* **TCP control** (STA) with line commands.
* **OTA over TCP** (`OTA <size> <crc32>` then raw bytes + CRC trailer).
* **BLE fallback** GATT:

  * **RX** (write cmds), **TX** (echo/status),
  * **WIFI** (write `<ssid>\n<pwd>`),
  * **ERRSRC** (reason string), **ALERT** (fw events),
  * **BLE-OTA** (control + data) for the oh-no-Wi-Fi cases.
* **Consistent replies**: BLE path mirrors TCP.
* **Health/rollback logic**: see below.
* **Alerts module**: last alert is readable; pushes on change.
* **Command bus**: fanout to modules; LED = demo.

## Health + rollback (the real policy)

* On boot we’re in **WAIT\_CONTROL** (no BLE).
  Health monitor is **event-driven**.
* **Proof of control** = Wi-Fi up **and** TCP `AUTH` succeeded.
  When that happens:

  * OTA image is marked **VALID** (`esp_ota_mark_app_valid_cancel_rollback()`),
  * mode → **NORMAL**, BLE stays hidden.
* **Failure path** (Wi-Fi events feed the monitor):

  * First failure:

    * If running an **OTA slot** that’s `NEW`/`PENDING_VERIFY` => **rollback now** via
      `esp_ota_mark_app_invalid_rollback_and_reboot()`.
      (Manual partition switch fallback exists if the API says nope.)
    * If image is **factory**/**VALID** (no rollback) → just count the failure.
  * Second failure (or rollback wasn’t possible) → **RECOVERY**:

    * Bring up **BLE lifeboat** (advertising + GATT service),
    * stay there until you fix Wi-Fi issue, for instance I introduced fixing invalid creds over BLE and TCP control comes back.

## TCP commands

(Auth = whether you must `AUTH` first.)

| Command                        | Auth | Reply / Effect                          |
| ------------------------------ | :--: | --------------------------------------- |
| `AUTH <pwd>`                   |   –  | `OK` / `DENIED`                         |
| `PING`                         |   –  | `PONG`                                  |
| `version`                      |   –  | from `esp_app_desc_t.version` (git/tag) |
| `diag`                         |   ✓  | partition + OTA state + mode            |
| `led_on` / `led_off`           |   ✓  | `led_on` / `led_off`                    |
| `setwifi <ssid> <pwd>`         |   ✓  | `WIFI_UPDATED` (triggers reconnect)     |
| `settoken <newtoken>`          |   ✓  | `OK` on success                         |
| `errsrc`                       |   –  | `mode=<...> errsrc=<NONE/NO_AP/...>`    |
| `OTA <size> <crc32>` + payload |   ✓  | `ACK`, then `OK` or error; reboots      |

> Note: case on command names is currently literal; we’ll relax later.

## BLE GATT (private UUIDs; we match by prefix)

Service: `efbe0000-fbfb-fbfb-fb4b-494545434956`

| Char (ends with) | Props           | Purpose / Format                                                                 |
| ---------------- | --------------- | -------------------------------------------------------------------------------- |
| `efbe0100`       | Write/WriteNR   | **RX** — send commands (`"AUTH ...\n"`, `"led_on\n"`, …)                         |
| `efbe0200`       | Notify/Read     | **TX** — status/echo lines (newline-delimited)                                   |
| `efbe0300`       | Write           | **WIFI** — creds payload: `"<ssid>\n<pwd>"`                                      |
| `efbe0400`       | Notify/Read     | **ERRSRC** — `NONE`, `NO_AP`, `AUTH_FAIL`, `STA_GOT_IP`, …                       |
| `efbe0500`       | Notify/Read     | **ALERT** — `ALERT seq=<n> code=<id> <detail>`                                   |
| `efbe0600`       | Write (w/resp)  | **BLE-OTA CTRL** — `"BL_OTA START <size> <crc32>"`, `"BL_OTA FINISH"`, `"ABORT"` |
| `efbe0700`       | Write (no resp) | **BLE-OTA DATA** — frames: `<seq:le32><len:le16><payload...>`                    |

**BLE-OTA behavior (device):**

* `START` allocates slot; frames stream to DATA; `PROG x/y` pushed occasionally.
* If the **last DATA** exactly fills the image, we **finalize and reboot**; `FINISH` is optional.
* If BLE drops but the whole image was received, we still **finalize** on disconnect.

## Host tool: `ota_send.py`

* **Prefers TCP**, with idle keepalive (`PING`) + fast reconnect.
* If TCP looks dead, it **sniffs BLE** briefly:

  * if seen, it **enters BLE session** (RX/TX/ERRSRC/ALERT live),
  * you can `SETWIFI <ssid> <pwd>` there.
* **OTA**:

  * Over TCP by default (fast, CRC-checked).
  * If TCP doesn’t return post-reboot, it tries **BLE** and will hop back to TCP once IP shows.

**Env knobs**

```
LOPY_ADDR, LOPY_PORT, LOPY_TOKEN
LOPY_BLE_ADDR, LOPY_BLE_NAMES      # MAC wins; names are fallback matches
```

**One-liners**

```bash
# Linux/mac
export LOPY_BLE_ADDR= <mac_address>
python ota_send.py
```

## Build + flash

ESP-IDF v5.x (works fine on v6 dev too), LoPy4 (ESP32).

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Partitions (will be reviewed later):

```
factory @ 0x020000  (2M)
ota_0   @ 0x220000  (2M)
ota_1   @ 0x420000  (2M)
spiffs  @ 0x620000  (~2M)
```

## Architecture (super short)

```
Wi-Fi STA ─► TCP:8080 ─► command parser ─► bus ─► modules (LED, OTA, alerts,…)
                 ▲
BLE lifeboat ────┘ (RX/TX mirror + WIFI cred write + ERRSRC/ALERT + BLE-OTA)
```

## Versioning

`version` prints `esp_app_desc_t.version` (whatever you set as PROJECT\_VER / git describe).
On successful **TCP AUTH**, syscoord marks the running OTA image **VALID** so it won’t roll back next boot.

## Security / storage

* Auth token lives in NVS (`"auth"/"token"`). `SETTOKEN` updates it.
* BLE creds are **plain text** so far (setup/recovery path).
* No certs/TLS here; this is a lab-grade control plane.

## File map

* `wifi_tcp.*` — TCP server + OTA framing
* `commands.*` — parsing + auth + handlers
* `ota_handler.*` — TCP OTA + transport-agnostic BLE/TCP OTA helpers
* `gatt_server.*` — BLE service (RX/TX/WIFI/ERRSRC/ALERT + BLE-OTA)
* `monitor.*` — event-driven health + rollback/lifeboat decisions
* `syscoord.*` — modes, mark-valid on TCP AUTH, BLE gating
* `alerts.*` — alert pipeline
* `ota_send.py` — host (TCP first, BLE lifeboat)

---  
## To be continued....