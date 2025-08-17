// components/core/monitor.c
#include "monitor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"      // for vTaskDelay.
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"      // for esp_partition_t.
#include "alerts.h"
#include "bootflag.h"

static const char *TAG = "HEALTH";

/* Former timer-based flow replaced with event-driven escalation. */
static bool s_done = false;              // Latched once control OK or recovery triggered.
static int  s_fail_streak = 0;           // Consecutive connectivity failures.

static monitor_timeout_cb_t s_timeout_cb = NULL;  // Called on second failure (enter RECOVERY).

void monitor_set_timeout_cb(monitor_timeout_cb_t cb) {
    s_timeout_cb = cb;
}

/* Public API kept for compatibility. No blind window anymore. */
void health_monitor_start(uint32_t ms_window) {
    (void)ms_window;
    s_done = false;
    s_fail_streak = 0;
    ESP_LOGI(TAG, "Health monitor armed (event-driven, no timer window).");
}

/* Called when control path is proven (Got IP / TCP ready / AUTH, per your policy). */
void health_monitor_control_ok(const char *path) {
    if (s_done) return;
    s_done = true;
    s_fail_streak = 0;
    ESP_LOGI(TAG, "Control path OK via %s. Cancelling escalation.", path ? path : "UNKNOWN");
    /* Do not touch rollback flags here; syscoord does that when entering NORMAL. */
}

/* New event-driven escalation entry point.
   Call this whenever you detect a Wi-Fi error condition (AUTH_FAIL, NO_AP, DHCP_FAIL, etc.).
   Pass ERRSRC_NONE when things are healthy to reset the streak. */
void monitor_on_wifi_error(errsrc_t e) {
    if (s_done) return;

    if (e == ERRSRC_NONE) {
        if (s_fail_streak) ESP_LOGI(TAG, "Error cleared. Resetting failure streak from %d.", s_fail_streak);
        s_fail_streak = 0;
        return;
    }

    ++s_fail_streak;
    ESP_LOGW(TAG, "Wi-Fi error (%d). Streak=%d.", (int)e, s_fail_streak);

    /* Determine if current app is an OTA slot that is still pending verification. */
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = 0;
    const bool on_ota =
        run && (run->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
                run->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1);
    bool pending = false;
    if (on_ota) {
        if (esp_ota_get_state_partition(run, &st) == ESP_OK) {
            pending = (st == ESP_OTA_IMG_NEW || st == ESP_OTA_IMG_PENDING_VERIFY);
        }
    }

    /* First failure → rollback only if:
       - we haven't rolled back yet AND
       - we are running a NEW/PENDING OTA image that can legally roll back. */
    if (s_fail_streak == 1 && !bootflag_is_post_rollback() && on_ota && pending) {
        ESP_LOGE(TAG, "No control path (first failure on pending OTA). Rolling back now.");
        alert_raise(ALERT_ROLLBACK_EXECUTED, "no control path; rolling back");
        bootflag_set_post_rollback(true);                // Remember we rolled back.
        vTaskDelay(pdMS_TO_TICKS(250));                  // Let logs flush.
        esp_ota_mark_app_invalid_rollback_and_reboot();  // No return.
        return;
    }

    /* If we cannot rollback (factory or VALID image), just keep counting failures. */
    if (s_fail_streak == 1 && (!on_ota || !pending)) {
        ESP_LOGW(TAG,
                 "First failure but current image cannot rollback (subtype=%d, state=%d). Skipping rollback.",
                 run ? run->subtype : -1, (int)st);
        /* Do nothing else here. Second failure will trigger RECOVERY. */
        return;
    }

    /* Second failure (or later) → enter RECOVERY (lifeboat). */
    if (s_fail_streak >= 2) {
        ESP_LOGE(TAG, "No control after rollback or non-rollback path. Entering RECOVERY (lifeboat).");
        alert_raise(ALERT_BLE_FATAL, "no control after rollback; enabling lifeboat");
        s_done = true;                                   // Latch to avoid repeats.
        if (s_timeout_cb) s_timeout_cb();                // syscoord will bring up BLE via worker.
    }
}
