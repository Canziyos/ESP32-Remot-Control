// components/core/monitor.c
#include "monitor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "alerts.h"
#include "bootflag.h"
#include "errsrc.h"

static const char *TAG = "HEALTH";

/* Event-driven escalation state */
static bool s_done = false;        // latched once control OK or recovery triggered
static int  s_fail_streak = 0;     // consecutive connectivity failures

static monitor_timeout_cb_t s_timeout_cb = NULL;  // called on second failure (enter RECOVERY)

void monitor_set_timeout_cb(monitor_timeout_cb_t cb) { s_timeout_cb = cb; }

void health_monitor_start(uint32_t ms_window) {
    (void)ms_window;
    s_done = false;
    s_fail_streak = 0;
    ESP_LOGI(TAG, "Health monitor armed (event-driven, no timer window).");
}

void health_monitor_control_ok(const char *path) {
    if (s_done) return;
    s_done = true;
    s_fail_streak = 0;
    ESP_LOGI(TAG, "Control path OK via %s. Cancelling escalation.", path ? path : "UNKNOWN");
}

/* Attempt official rollback; if it returns (error), try manual fallback. */
static void try_rollback_now(void) {
    alert_raise(ALERT_ROLLBACK_EXECUTED, "no control path; rolling back");

    // 1) Official IDF API: works only for NEW/PENDING_VERIFY images
    esp_err_t e = esp_ota_mark_app_invalid_rollback_and_reboot();  // no return on success
    ESP_LOGW(TAG, "esp_ota_mark_app_invalid_rollback_and_reboot() returned: %s",
             esp_err_to_name(e));

    // 2) Manual fallback: prefer factory, else the "other" OTA slot
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *factory =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    const esp_partition_t *other = NULL;
    if (run) {
        esp_partition_subtype_t alt =
            (run->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) ?
             ESP_PARTITION_SUBTYPE_APP_OTA_1 :
            (run->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) ?
             ESP_PARTITION_SUBTYPE_APP_OTA_0 : -1;
        if (alt != -1) {
            other = esp_partition_find_first(ESP_PARTITION_TYPE_APP, alt, NULL);
        }
    }

    const esp_partition_t *target = NULL;
    if (factory && factory != run) target = factory;
    else if (other && other != run) target = other;

    if (target) {
        bootflag_set_post_rollback(true);          // we attempted rollback
        vTaskDelay(pdMS_TO_TICKS(250));            // let logs drain
        ESP_LOGW(TAG, "Forcing boot of %s at 0x%06x", target->label, (unsigned)target->address);
        esp_ota_set_boot_partition(target);
        esp_restart();                              // no return
    } else {
        ESP_LOGE(TAG, "No alternate partition available; cannot rollback.");
        // Fall through; second failure will enable BLE lifeboat.
    }
}

void monitor_on_wifi_error(errsrc_t e) {
    if (s_done) return;

    if (e == ES_NONE) {
        if (s_fail_streak) ESP_LOGI(TAG, "Error cleared. Resetting failure streak from %d.", s_fail_streak);
        s_fail_streak = 0;
        return;
    }

    ++s_fail_streak;
    ESP_LOGW(TAG, "Wi-Fi error (%d). Streak=%d.", (int)e, s_fail_streak);

    // Identify if we are on an OTA slot and whether it is pending verify
    const esp_partition_t *run = esp_ota_get_running_partition();
    bool on_ota = run && (run->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
                          run->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1);

    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    bool can_rollback = false;
    if (on_ota && esp_ota_get_state_partition(run, &st) == ESP_OK) {
        // Treat both NEW and PENDING_VERIFY as rollback-eligible
        can_rollback = (st == ESP_OTA_IMG_NEW || st == ESP_OTA_IMG_PENDING_VERIFY);
    }

    // First failure → rollback now if eligible
    if (s_fail_streak == 1) {
        if (on_ota && can_rollback && !bootflag_is_post_rollback()) {
            ESP_LOGE(TAG, "No control path on first failure (state=%d). Rolling back now.", (int)st);
            try_rollback_now();   // no return on success
            // If we land here, rollback API failed and manual fallback was impossible.
            // We keep running and will go to BLE on the second failure.
        } else {
            ESP_LOGW(TAG, "First failure but cannot rollback (subtype=%d, state=%d). Skipping rollback.",
                     run ? run->subtype : -1, (int)st);
        }
        return;
    }

    // Second failure (or later) → enter RECOVERY (lifeboat)
    if (s_fail_streak >= 2) {
        ESP_LOGE(TAG, "No control after rollback or non-rollback path. Entering RECOVERY (lifeboat).");
        alert_raise(ALERT_BLE_FATAL, "no control after rollback; enabling lifeboat");
        s_done = true;                     // latch to avoid repeats
        if (s_timeout_cb) s_timeout_cb();  // syscoord will bring up BLE via worker
    }
}
