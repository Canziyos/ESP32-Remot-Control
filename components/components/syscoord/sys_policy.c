// VALID-after-TCP+AUTH, recovery/lifeboat decisions
#include <string.h>
#include "sys_priv.h"

/* Mark TCP as authed (called by commands on AUTH OK over TCP) */
void syscoord_mark_tcp_authed(void) {
  atomic_store(&g_tcp_authed, true);
}

/* Promote to NORMAL only for TCP + after auth; also mark image VALID */
void syscoord_control_path_ok(const char *source) {
  if (!(source && strcmp(source, "TCP") == 0 && atomic_load(&g_tcp_authed))) {
    ESP_LOGW(SYSCOORD_TAG, "Ignoring control_path_ok from %s (TCP+auth required).",
             source ? source : "NULL");
    return;
  }

  /* If we’re running a NEW/PENDING image, cancel rollback now */
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
  if (run && esp_ota_get_state_partition(run, &st) == ESP_OK &&
      (st == ESP_OTA_IMG_NEW || st == ESP_OTA_IMG_PENDING_VERIFY)) {
    esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(SYSCOORD_TAG, "Marked OTA image VALID (cancel rollback): %s", esp_err_to_name(e));
  }

  /* Notify health monitor and go NORMAL */
  health_monitor_control_ok(source);
  set_mode(SC_MODE_NORMAL);
}

/* Manual escalation hook */
void syscoord_on_no_control_path(void) {
  enqueue_recovery();
}

/* Worker: handles slow ops for RECOVERY enter */
void syscoord_worker(void *arg) {
  (void)arg;
  sys_evt_t ev;
  for (;;) {
    if (xQueueReceive(s_sys_q, &ev, portMAX_DELAY) != pdTRUE) continue;

    switch (ev) {
      case SYS_EVT_ENTER_RECOVERY:
        set_mode(SC_MODE_RECOVERY);
        ble_fallback_init();
        ble_lifeboat_set(true);
        alert_raise(ALERT_BLE_FATAL, "recovery mode: no control after rollback");
        break;
      default:
        break;
    }
  }
}
