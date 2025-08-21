import os
import pathlib

# ---------- Config ----------
ADDR   = os.getenv("LOPY_ADDR", "192.168.10.125")
PORT   = int(os.getenv("LOPY_PORT", "8080"))
TOKEN  = os.getenv("LOPY_TOKEN", "baker")
DEBUG_BLE = True if os.getenv("LOPY_DEBUG_BLE", "1").strip() not in ("0","false","False") else False

# TCP upload
CHUNK  = 16 * 1024
PROGRESS_EVERY = 256 * 1024

# Reboot/reconnect timing
WAIT_AFTER_REBOOT_S = float(os.getenv("LOPY_WAIT_AFTER_REBOOT_S", "10"))
RECONNECT_TRIES = int(os.getenv("LOPY_RECONNECT_TRIES", "12"))
RECONNECT_DELAY_S = float(os.getenv("LOPY_RECONNECT_DELAY_S", "1.0"))

# UUID prefixes (match by prefix; endianness tolerant).
RX_PREFIX = "efbe0100"
TX_PREFIX = "efbe0200"
WIFI_PREFIX = "efbe0300"
ERR_PREFIX = "efbe0400"
ALERT_PREFIX = "efbe0500"
OTA_CTRL_PREFIX = "efbe0600"
OTA_DATA_PREFIX = "efbe0700"
DHT_PREFIX = "efbe0800-"
DHT_SUFFIX = "efbe0800"   # fallback match
# BLE identity.
BLE_ADV_NAMES = [n.strip() for n in os.getenv("LOPY_BLE_NAMES", "LoPy4").split(",") if n.strip()]
BLE_ADDR = (os.getenv("LOPY_BLE_ADDR", "") or "").replace("-", ":").upper()

# Fast fallback tunables (no device power impact).
TCP_POST_OTA_CONNECT_TIMEOUT = float(os.getenv("LOPY_TCP_RETRY_TIMEOUT", "1.3"))
POST_OTA_TCP_ONLY_WINDOW_S = float(os.getenv("LOPY_TCP_ONLY_WINDOW_S", "3.0"))
BLE_QUICK_SCAN_S = float(os.getenv("LOPY_BLE_QUICK_SCAN_S", "1.8"))

# Paths helper
ROOT = pathlib.Path(__file__).resolve().parent.parent
