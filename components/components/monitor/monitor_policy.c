// monitor_policy.c
#include "monitor_priv.h"
#include "alerts.h"
#include "bootflag.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HEALTH-POL";

bool monitor_is_rollback_eligible(const esp_partition_t *run, esp_ota_img_states_t *out_state) {
    if (!run) return false;
    bool on_ota = (run->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
                   run->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1);
    if (!on_ota) return false;

    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(run, &st) != ESP_OK) return false;

    if (out_state) *out_state = st;
    return (st == ESP_OTA_IMG_NEW || st == ESP_OTA_IMG_PENDING_VERIFY);
}

static const esp_partition_t *select_manual_fallback_target(const esp_partition_t *run) {
    const esp_partition_t *factory =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    const esp_partition_t *other = NULL;

    if (run) {
        esp_partition_subtype_t alt =
            (run->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) ? ESP_PARTITION_SUBTYPE_APP_OTA_1 :
            (run->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) ? ESP_PARTITION_SUBTYPE_APP_OTA_0 : -1;
        if (alt != -1) {
            other = esp_partition_find_first(ESP_PARTITION_TYPE_APP, alt, NULL);
        }
    }

    if (factory && factory != run) return factory;
    if (other   && other   != run) return other;
    return NULL;
}

bool monitor_try_rollback_now(const char *why) {
    alert_raise(ALERT_ROLLBACK_EXECUTED, why ? why : "rolling back");

    // 1) Official IDF rollback (works for new/pending images); reboots on success.
    esp_err_t e = esp_ota_mark_app_invalid_rollback_and_reboot(); // no return on success.
    if (e != ESP_OK) {
        // 2) Manual fallback: boot factory or the other OTA slot.
        const esp_partition_t *run = esp_ota_get_running_partition();
        const esp_partition_t *target = select_manual_fallback_target(run);
        if (target) {
            bootflag_set_post_rollback(true);
            vTaskDelay(pdMS_TO_TICKS(250));  // let logs drain.
            ESP_LOGW(TAG, "Manual fallback to %s @ 0x%06x", target->label, (unsigned)target->address);
            (void)esp_ota_set_boot_partition(target);
            esp_restart(); // no return.
        }
        ESP_LOGE(TAG, "Manual fallback unavailable; cannot rollback.");
        return false;
    }
    // If we ever get here, official API returned (didn't reboot).
    ESP_LOGW(TAG, "Official rollback call returned: %s", esp_err_to_name(e));
    return false;
}
