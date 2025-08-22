![Target-ESP32](https://img.shields.io/badge/Target-ESP32%20%28LoPy4%29-blue)
![Primary-TCP](https://img.shields.io/badge/Primary-TCP%20%28Wi%E2%80%91Fi%29-brightgreen)
![Fallback-BLE](https://img.shields.io/badge/Fallback-BLE-orange)
![OTA-TCP%20%2B%20BLE](https://img.shields.io/badge/OTA-TCP%20%2B%20BLE%20%28recovery%29-purple)

# ESP32 (LoPy4) – Wi-Fi + BLE Remote Control

Tiny control plane for ESP32. **TCP over Wi-Fi when up**, **BLE lifeboat when not**.
Plain-text commands, OTA over TCP (with **BLE-OTA** as the last outpost), alerts, rollback that actually behaves,
and now a **DHT sensor that sometimes works and sometimes throws `NA` in your face**.

---

## What it does

* **TCP control** (STA) with line commands.
* **OTA over TCP** (`OTA <size> <crc32>` then raw bytes + CRC trailer).
* **BLE fallback** GATT: RX (write cmds), TX (status), WIFI (`<ssid>\n<pwd>`), ERRSRC, ALERT, **BLE-OTA** (CTRL/DATA).
* **Same replies on both paths** (BLE mirrors TCP).
* **Event-driven health + rollback**.
* **DHT sensor readings** – either stream them or demand.

---

## Health + rollback

* Boot in **WAIT\_CONTROL** (BLE hidden).
* **Proof of control** = Wi-Fi up **and** TCP `AUTH` OK ⇒ mark image **VALID** and switch to **NORMAL**.
* **1st connectivity failure** on a `NEW`/`PENDING_VERIFY` OTA slot ⇒ **rollback now**.
* **2nd failure** (or rollback not possible) ⇒ **RECOVERY**: bring up **BLE lifeboat** until Wi-Fi is fixed.

---

## TCP commands

(Auth = needs `AUTH` first.)

| Command                        | Auth | Reply / Effect                                      |
| ------------------------------ | :--: | --------------------------------------------------- |
| `AUTH <pwd>`                   |   –  | `OK` / `DENIED`                                     |
| `PING`                         |   –  | `PONG`                                              |
| `version`                      |   –  | e.g. `360e184-dirty`                                |
| `diag`                         |   ✓  | e.g. `PART=factory SUB=0 OFF=0x020000 SIZE=2097152` |
| `led_on` / `led_off`           |   ✓  | `LED_ON` / `LED_OFF`                                |
| `setwifi <ssid> <pwd>`         |   ✓  | `WIFI_UPDATED` (triggers reconnect)                 |
| `settoken <newtoken>`          |   ✓  | `OK` on success                                     |
| `errsrc`                       |   –  | e.g. `mode=NORMAL errsrc=0 NONE`                    |
| `OTA <size> <crc32>` + payload |   ✓  | `ACK` → `OK` or error; reboots                      |
| `dht?`                         |   –  | One-shot DHT read or `DHT NA` if the bastard fails  |
| `dhtstream on <ms>`            |   –  | Start periodic DHT stream (`DHTSTREAM ON`)          |
| `dhtstream off`                |   –  | Kill the stream (`DHTSTREAM OFF`)                   |
| `dhtstate`                     |   –  | Show stream state/interval/valid flag/sample age    |

> Commands are case-literal for now.

---

## BLE GATT (private UUIDs; matched by prefix)

**Service**: `efbe0000-fbfb-fbfb-fb4b-494545434956`

| Ends with  | Props           | Purpose / Format                                                                 |
| ---------- | --------------- | -------------------------------------------------------------------------------- |
| `efbe0100` | Write/WriteNR   | **RX** — send commands (`"AUTH …\n"`, `"led_on\n"`, …)                           |
| `efbe0200` | Notify/Read     | **TX** — status/echo lines                                                       |
| `efbe0300` | Write           | **WIFI** — `"<ssid>\n<pwd>"`                                                     |
| `efbe0400` | Notify/Read     | **ERRSRC** — `NONE`, `NO_AP`, `AUTH_FAIL`, `STA_GOT_IP`, …                       |
| `efbe0500` | Notify/Read     | **ALERT** — `ALERT seq=<n> code=<id> <detail>`                                   |
| `efbe0600` | Write (w/resp)  | **BLE-OTA CTRL** — `"BL_OTA START <size> <crc32>"`, `"BL_OTA FINISH"`, `"ABORT"` |
| `efbe0700` | Write (no resp) | **BLE-OTA DATA** — `<seq:le32><len:le16><payload...>`                            |
| `efbe0800` | Notify/Read     | **DHT** — temperature + humidity values (or `DHT NA`)                            |

**BLE-OTA**: `START` → stream DATA frames → optional `FINISH`.
If the last DATA completes the image, the device **finalizes and reboots** (FINISH optional).
If BLE drops after full image, it still **finalizes on disconnect**.
*(You can gate BLE-OTA to RECOVERY mode in firmware.)*

---

## DHT sensor (temperature + humidity)

* Background sampler task — sensor polled at safe intervals (≥2s or it sulks).
* `dht?` → one-shot read.
* `dhtstream on <ms>` → start periodic reads.
* `dhtstate` → see if it’s streaming, how old the value is, and whether it’s valid.
* Values mirrored to BLE (UUID `efbe0800-…`).

---

## Host CLI (Python)

Lives under `app/`. Prefers **TCP**, falls back to **BLE**, hops back to TCP when Wi-Fi is up.

```
app/
  cli.py               # entry point
  session.py           # interactive loop + keepalive + reconnection
  tcp_client.py        # connect/auth, send_line, keepalive
  ble_fallback.py      # scan/connect, RX/TX/ERRSRC/ALERT/SETWIFI, BLE-OTA, DHT
  ota.py               # OTA over TCP + BLE upload
  utils.py             # helpers (UUID prefix resolve, Wi-Fi-OK parser, etc.)
```

Run:

```bash
python -m app.cli
# or
python app/cli.py
```

Env:

```
LOPY_ADDR, LOPY_PORT, LOPY_TOKEN
LOPY_BLE_ADDR, LOPY_BLE_NAMES   # MAC wins; names are fallback.
```

---

## Build + flash

ESP-IDF v5.x (works on v6 dev too). Target: LoPy4 (ESP32).

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

**Partitions (example):**

```
factory @ 0x020000  (2M)
ota_0   @ 0x220000  (2M)
ota_1   @ 0x420000  (2M)
spiffs  @ 0x620000  (~2M)
```

---

## Architecture (super short)

```
Wi-Fi STA ─► TCP:8080 ─► command parser ─► bus ─► modules (LED, OTA, alerts, DHT, …)
                 ▲
BLE lifeboat ────┘ (RX/TX mirror + WIFI cred write + ERRSRC/ALERT + BLE-OTA + DHT)
```

---

## Security / storage

* Auth token in NVS (`"auth"/"token"`). `SETTOKEN` updates it.
* BLE creds are plain text (setup/recovery path).
* No TLS; lab-grade control plane.

---

## To be continued…

