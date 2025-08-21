#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "app_cfg.h"
#include "wifi_priv.h"

#define TAG "WIFI"

#define RECONN_BASE_MS WIFI_RECONN_BASE_MS
#define RECONN_MAX_MS  WIFI_RECONN_MAX_MS

static TimerHandle_t s_reconn_tmr;
static int s_retries = 0;

static void wifi_reconnect_timer_cb(TimerHandle_t xTimer) {
    (void)xTimer;
    esp_err_t e = esp_wifi_connect();
    if (e == ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "connect(): already connecting; will rely on events.");
    } else if (e != ESP_OK) {
        ESP_LOGE(TAG, "connect(): %s", esp_err_to_name(e));
    } else {
        ESP_LOGI(TAG, "connect(): requested.");
    }
}

void wifi_backoff_reset(void) {
    s_retries = 0;
}

void wifi_backoff_stop_timer(void) {
    if (s_reconn_tmr) xTimerStop(s_reconn_tmr, 0);
}

void wifi_backoff_schedule(void) {
    if (!s_reconn_tmr) {
        s_reconn_tmr = xTimerCreate("wifi.reconn", pdMS_TO_TICKS(1000), pdFALSE, NULL, wifi_reconnect_timer_cb);
    }
    uint32_t delay_ms = RECONN_BASE_MS << (s_retries < 6 ? s_retries : 6); // up to 32x
    if (delay_ms > RECONN_MAX_MS) delay_ms = RECONN_MAX_MS;

#if WIFI_RECONN_JITTER_PCT > 0
    {
        uint32_t span = (delay_ms * WIFI_RECONN_JITTER_PCT) / 100U; // e.g., 10%
        if (span) {
            uint32_t r   = esp_random();
            int32_t  off = (int32_t)(r % span) - (int32_t)(span / 2U);
            int32_t  d2  = (int32_t)delay_ms + off;
            delay_ms = (uint32_t)((d2 < 0) ? 0 : d2);
        }
    }
#endif

    xTimerStop(s_reconn_tmr, 0);
    xTimerChangePeriod(s_reconn_tmr, pdMS_TO_TICKS(delay_ms), 0);
    xTimerStart(s_reconn_tmr, 0);

    ESP_LOGI(TAG, "Will reconnect in %u ms (retry #%d).", (unsigned)delay_ms, s_retries);
    s_retries++;
}
