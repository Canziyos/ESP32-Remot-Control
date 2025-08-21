// gatt_notify.c
#include <stdio.h>
#include <string.h>

#include "gatt_priv.h"
#include "sys_sink.h"   // brings in alerts.h so alert_record_t is known

static char s_last_errsrc_sent[64] = "";

/* Update characteristic value always; send indicate only if changed. */
static void errsrc_notify_if_changed(const char *current_err) {
    if (!current_err) current_err = "NONE";

    if (g_gatts_if != ESP_GATT_IF_NONE && gatt_handle_table[IDX_ERRSRC_VAL]) {
        esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_ERRSRC_VAL],
                                     (uint16_t)strlen(current_err),
                                     (const uint8_t*)current_err);
    }

    if (strncmp(current_err, s_last_errsrc_sent, sizeof(s_last_errsrc_sent)) == 0) {
        return;
    }

    if (es_notify_enabled && g_conn_id != 0xFFFF && g_gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_send_indicate(g_gatts_if, g_conn_id,
                                    gatt_handle_table[IDX_ERRSRC_VAL],
                                    (uint16_t)strlen(current_err),
                                    (uint8_t *)current_err, false);
    }

    strncpy(s_last_errsrc_sent, current_err, sizeof(s_last_errsrc_sent) - 1);
    s_last_errsrc_sent[sizeof(s_last_errsrc_sent) - 1] = '\0';
}

/* Public notify helpers */
void gatt_server_send_status(const char *s) {
    if (!s) return;
    size_t slen = strlen(s);

    if (g_gatts_if != ESP_GATT_IF_NONE && gatt_handle_table[IDX_TX_VAL]) {
        esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_TX_VAL],
                                     (uint16_t)slen, (const uint8_t*)s);
    }
    if (!tx_notify_enabled || g_conn_id == 0xFFFF || g_gatts_if == ESP_GATT_IF_NONE) return;

    const uint8_t *p = (const uint8_t *)s;
    size_t len = slen;

    uint16_t max_chunk = g_mtu_payload;
    if (max_chunk < 1)   max_chunk = 1;
    if (max_chunk > 180) max_chunk = 180;

    while (len) {
        uint16_t chunk = (len > max_chunk) ? max_chunk : (uint16_t)len;
        esp_ble_gatts_send_indicate(g_gatts_if, g_conn_id, gatt_handle_table[IDX_TX_VAL],
                                    chunk, (uint8_t *)p, false);
        p += chunk;
        len -= chunk;
    }
    const uint8_t nl = '\n';
    esp_ble_gatts_send_indicate(g_gatts_if, g_conn_id, gatt_handle_table[IDX_TX_VAL],
                                1, (uint8_t *)&nl, false);
}

/* Match the private header prototype exactly (avoids mismatch warnings). */
void gatt_alert_notify(const void *rec_any) {
    const alert_record_t *rec = (const alert_record_t *)rec_any;
    if (!rec) return;

    char line[128];
    int n = snprintf(line, sizeof(line), "ALERT seq=%u code=%u %s",
                     (unsigned)rec->seq, (unsigned)rec->code, rec->detail);
    if (n < 0) n = 0;
    size_t used = strnlen(line, sizeof(line));

    if (g_gatts_if != ESP_GATT_IF_NONE && gatt_handle_table[IDX_ALERT_VAL]) {
        esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_ALERT_VAL],
                                     (uint16_t)used, (const uint8_t*)line);
    }
    if (alert_notify_enabled && g_conn_id != 0xFFFF && g_gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_send_indicate(g_gatts_if, g_conn_id,
                                    gatt_handle_table[IDX_ALERT_VAL],
                                    (uint16_t)used, (uint8_t*)line, false);
    }
}

void gatt_server_notify_errsrc(const char *err) {
    if (!err || !*err) err = "NONE";
    errsrc_notify_if_changed(err);
}

/* Override of the syscoord hook: forward alerts to BLE. */
void syscoord_alert_sink(const alert_record_t *rec) {
    gatt_alert_notify(rec);
}
