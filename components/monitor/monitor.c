#include "monitor.h"
#include "monitor_priv.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "alerts.h"
#include "bootflag.h"
#include "errsrc.h"

static const char *TAG = "HEALTH";

/* Latches + counters */
static bool s_done = false;     // latched once control OK or recovery triggered
static int  s_fail_streak = 0;  // consecutive connectivity failures (saturating)

static monitor_timeout_cb_t s_timeout_cb = NULL;  // called on second failure (enter RECOVERY)

void monitor_set_timeout_cb(monitor_timeout_cb_t cb) { s_timeout_cb = cb; }

void health_monitor_start(uint32_t ms_window) {
    (void)ms_window;
    s_done = false;
    s_fail_streak = 0;
    ESP_LOGI(TAG, "Health monitor armed (event-driven).");
}

void health_monitor_control_ok(const char *path) {
    if (s_done) return;
    s_done = true;
    s_fail_streak = 0;
    ESP_LOGI(TAG, "Control path OK via %s. Cancelling escalation.", path ? path : "UNKNOWN");
}

/* rollback implementation is in monitor_policy.c via monitor_try_rollback_now() */

void monitor_on_wifi_error(errsrc_t e) {
    if (s_done) return;

    if (e == ES_NONE) {
        if (s_fail_streak) ESP_LOGI(TAG, "Error cleared. Resetting failure streak from %d.", s_fail_streak);
        s_fail_streak = 0;
        return;
    }

    /* saturating +1 up to 0x7fff */
    if (s_fail_streak < 0x7fff) s_fail_streak++;
    ESP_LOGW(TAG, "Wi-Fi error (%d). Streak=%d.", (int)e, s_fail_streak);

    // Identify rollback eligibility.
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    bool can_rollback = monitor_is_rollback_eligible(run, &st);

    // First failure => rollback now if eligible
    if (s_fail_streak == 1) {
        if (can_rollback && !bootflag_is_post_rollback()) {
            ESP_LOGE(TAG, "No control path on first failure (state=%d). Rolling back now.", (int)st);
            (void)monitor_try_rollback_now("no control path; rolling back"); // reboot on success
            // If we land here, rollback failed and we keep running.
        } else {
            ESP_LOGW(TAG, "First failure but cannot rollback (subtype=%d, state=%d). Skipping.",
                     run ? run->subtype : -1, (int)st);
        }
        return;
    }

    // Second failure (or later) => enter RECOVERY (lifeboat).
    if (s_fail_streak >= 2) {
        ESP_LOGE(TAG, "No control after rollback or non-rollback path. Entering RECOVERY (lifeboat).");
        alert_raise(ALERT_BLE_FATAL, "no control after rollback; enabling lifeboat");
        s_done = true;                     // latch to avoid repeats.
        if (s_timeout_cb) s_timeout_cb();  // syscoord will bring up BLE via worker.
    }
}
