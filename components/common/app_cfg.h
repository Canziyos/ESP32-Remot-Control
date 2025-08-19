// app_cfg.h
#pragma once

/* Compile-time defaults. Runtime overrides come from NVS. */
/* Later we can gate these with Kconfig for release builds. */

#ifndef APP_DEFAULT_AUTH_TOKEN
#define APP_DEFAULT_AUTH_TOKEN "hunter2"
#endif

/* NVS namespaces/keys. */
#ifndef NVS_NS_AUTH
#define NVS_NS_AUTH "auth"
#endif
#ifndef NVS_KEY_AUTH_TOKEN
#define NVS_KEY_AUTH_TOKEN "token"
#endif

/* Wi-Fi credentials (NVS) */
#ifndef NVS_NS_WIFI
#define NVS_NS_WIFI "wifi"
#endif
#ifndef NVS_KEY_WIFI_SSID
#define NVS_KEY_WIFI_SSID "ssid"
#endif
#ifndef NVS_KEY_WIFI_PASSWORD
#define NVS_KEY_WIFI_PASSWORD "password"
#endif
/* Wi-Fi reconnect tuning (centralized). */
#ifndef WIFI_RECONN_BASE_MS
#define WIFI_RECONN_BASE_MS  500   /* initial delay. */
#endif
#ifndef WIFI_RECONN_MAX_MS
#define WIFI_RECONN_MAX_MS   30000 /* cap at 30s. */
#endif
#ifndef WIFI_RECONN_JITTER_PCT
#define WIFI_RECONN_JITTER_PCT 10  /* ±10% jitter; set 0 to disable. */
#endif