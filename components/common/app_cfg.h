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