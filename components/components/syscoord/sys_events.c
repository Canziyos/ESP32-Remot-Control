// on_wifi_state, on_ble_state, on_ble_service_started, on_monitor_event
#include "sys_priv.h"

/* Wi-Fi IP up/down -> manage mode + re-auth requirement */
void syscoord_on_wifi_state(bool up) {
  ESP_LOGI(SYSCOORD_TAG, "Wi-Fi: %s", up ? "UP" : "DOWN");
  if (!up) {
    /* force re-auth over TCP; if NORMAL, go back to WAIT_CONTROL */
    atomic_store(&g_tcp_authed, false);
    if (atomic_load(&g_mode) == SC_MODE_NORMAL) {
      set_mode(SC_MODE_WAIT_CONTROL);
    }
  }
}

/* Informational today; reserved for policy use later */
void syscoord_on_tcp_clients(int count) { (void)count; }
void syscoord_on_ble_state(bool connected) { (void)connected; }

/* GATT stack is up: if in RECOVERY, start lifeboat advertising */
void syscoord_on_ble_service_started(void) {
  sc_mode_t m = atomic_load(&g_mode);
  if (m == SC_MODE_RECOVERY) {
#if defined(CONFIG_FEATURE_BLE_LIFEBOAT) && CONFIG_FEATURE_BLE_LIFEBOAT
    ESP_LOGI(SYSCOORD_TAG, "BLE service up in RECOVERY -> start advertising.");
    ble_start_advertising();
#else
    ESP_LOGI(SYSCOORD_TAG, "BLE service up; lifeboat disabled.");
#endif
  } else {
    ESP_LOGI(SYSCOORD_TAG, "BLE service up; mode=%d, not advertising.", (int)m);
  }
}
