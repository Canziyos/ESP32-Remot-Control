// state machine & mode transitions.
#include "sys_priv.h"
#include "syscoord_sink.h"


const char *SYSCOORD_TAG = "SYSCOORD";

/* ---- state ---- */
_Atomic sc_mode_t g_mode = SC_MODE_STARTUP;
_Atomic bool g_tcp_authed = false;
__attribute__((weak)) void syscoord_alert_sink(const alert_record_t *rec) { (void)rec; }


/* worker plumbing (allocated in init) */
QueueHandle_t s_sys_q   = NULL;
TaskHandle_t  s_sys_task = NULL;
static void on_alert_from_core(const alert_record_t *rec) {
    if (!rec) return;
    syscoord_alert_sink(rec);
    // Fan-out sink #1: BLE ALERT characteristic.
    //gatt_alert_notify(rec);
    // Future: TCP broadcast can be added here.
}

/* ---- mode switch ---- */
void set_mode(sc_mode_t m) {
  sc_mode_t prev = atomic_exchange(&g_mode, m);
  if (prev == m) return;

  ESP_LOGI(SYSCOORD_TAG, "mode: %d -> %d", prev, m);

  switch (m) {
    case SC_MODE_STARTUP:
      /* nothing so far*/
      break;

    case SC_MODE_WAIT_CONTROL:
      /* Hide BLE while waiting for control; (re)arm monitor (event-driven). */
      ble_lifeboat_set(false);
      ble_fallback_stop();
      health_monitor_start(0);
      ESP_LOGI(SYSCOORD_TAG, "Health monitor armed (event-driven).");
      break;

    case SC_MODE_NORMAL:
      /* Control path proven. Hide BLE lifeboat. Clear post-rollback flag. */
      ble_lifeboat_set(false);
      ble_fallback_stop();
      bootflag_set_post_rollback(false);
      break;

    case SC_MODE_RECOVERY:
      /* BLE bring-up happens in worker thread after event. */
      break;
  }
}

/* ---- public API ---- */
void syscoord_init(void) {
  ESP_LOGI(SYSCOORD_TAG, "System coordinator init.");

  /* Log partition + OTA state once at boot */
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
  if (run) (void)esp_ota_get_state_partition(run, &st);
  ESP_LOGI(SYSCOORD_TAG, "Running: label=%s subtype=%d off=0x%06x size=%u state=%s",
           run ? run->label : "?",
           run ? run->subtype : -1,
           run ? (unsigned)run->address : 0,
           run ? (unsigned)run->size : 0,
           _ota_st_name(st));

  /* worker before subscribing to monitor */
  s_sys_q = xQueueCreate(4, sizeof(sys_evt_t));
  xTaskCreate(syscoord_worker, "syscoord.wkr", 4096, NULL, 5, &s_sys_task);

  /* monitor will notify us when escalation is needed */
  monitor_set_timeout_cb(enqueue_recovery);

  // syscoord owns alert fan-out to sinks (BLE/TCP/...). */
  alerts_subscribe(on_alert_from_core);

  /* Begin in WAIT_CONTROL; Wi-Fi / monitor drive the next steps */
  set_mode(SC_MODE_WAIT_CONTROL);
}

sc_mode_t syscoord_get_mode(void) {
  return atomic_load(&g_mode);
}
