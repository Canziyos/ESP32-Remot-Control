// gatt_wifi_cred.c (drop-in)
#include <stdio.h>
#include <string.h>
#include <strings.h>      // strcasecmp
#include <stdbool.h>
#include "esp_log.h"

#include "gatt_priv.h"    // g_ble_cli, ble_cmd_on_rx(...)
#include "gatt_server.h"  // gatt_server_send_status(...)

static const char *TAG = "GATT.wifi";

static inline void rstrip_crlf(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\r' || s[n-1] == '\n' || (unsigned char)s[n-1] <= ' ')) {
        s[--n] = '\0';
    }
}

void gatt_on_wifi_cred_write(const uint8_t *data, uint16_t len) {
    if (!data || !len) return;

    char buf[200];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, data, len);
    buf[len] = '\0';

    // Forms supported:
    // 1) "<ssid>\n<pwd>"             (SSID may contain spaces; pwd may be empty)
    // 2) "SETWIFI <ssid> <pwd>"      (space-delimited; SSID without spaces)
    char ssid[33] = {0};
    char pwd[65]  = {0};
    bool ok = false;

    char *nl = strchr(buf, '\n');
    if (nl) {
        *nl = '\0';
        rstrip_crlf(buf);
        strncpy(ssid, buf, sizeof(ssid) - 1);

        char *p2 = nl + 1;
        rstrip_crlf(p2);
        strncpy(pwd, p2, sizeof(pwd) - 1);

        ok = (ssid[0] != '\0');  // allow empty pwd for OPEN networks
    } else {
        char cmd[16] = {0};
        // this form can't carry spaces inside SSID/PWD (by design)
        if (sscanf(buf, "%15s %32s %64s", cmd, ssid, pwd) == 3 &&
            strcasecmp(cmd, "SETWIFI") == 0) {
            ok = true;
        }
    }

    if (!ok) {
        ESP_LOGW(TAG, "Wi-Fi creds format invalid.");
        gatt_server_send_status("BADFMT");
        return;
    }

    // Route to the normal command path so existing auth/policy applies.
    char cmdline[120];
    int n = snprintf(cmdline, sizeof(cmdline), "setwifi %s %s", ssid, pwd);
    if (n < 0) n = 0;
    if (n >= (int)sizeof(cmdline)) cmdline[sizeof(cmdline)-1] = '\0';

    if (g_ble_cli) {
        size_t L = strnlen(cmdline, sizeof(cmdline));
        if (L < sizeof(cmdline) - 1) cmdline[L++] = '\n';
        ble_cmd_on_rx(g_ble_cli, (const uint8_t*)cmdline, (uint16_t)L);
    }
}
