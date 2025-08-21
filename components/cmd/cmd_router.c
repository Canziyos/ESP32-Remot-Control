#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "command_bus.h"
#include "commands.h"      // for cmd_reply(), cmd_ctx_t
#include "dht.h"           // dht_read_latest(), dht_set_stream()
#include "led.h"           // expected: void led_on(void); void led_off(void);

#include <stdio.h>

static void cmd_router_task(void *pv) {
    cmd_msg_t m;

    for (;;) {
        if (cmd_bus_receive_msg(&m, portMAX_DELAY) != pdTRUE) continue;

        switch (m.cmd) {
        /* ---- LED ---- */
        case CMD_LED_ON:
            led_on();
            if (m.ctx) cmd_reply(m.ctx, "LED_ON\n");
            break;

        case CMD_LED_OFF:
            led_off();
            if (m.ctx) cmd_reply(m.ctx, "LED_OFF\n");
            break;

        /* ---- DHT ---- */
        case CMD_DHT_QUERY: {
            dht_sample_t s;
            dht_read_latest(&s);
            if (m.ctx) {
                if (!s.valid) {
                    cmd_reply(m.ctx, "DHT NA\n");
                } else {
                    char buf[64];
                    snprintf(buf, sizeof(buf),
                             "DHT T=%.1fC RH=%.1f%% age=%u ms\n",
                             s.temp_c, s.rh, (unsigned)s.age_ms);
                    cmd_reply(m.ctx, buf);
                }
            }
            break;
        }

        case CMD_DHT_STREAM_ON: {
            uint32_t every_ms = m.u32 ? m.u32 : 0;
            dht_set_stream(true, every_ms);
            if (m.ctx) cmd_reply(m.ctx, "DHTSTREAM ON\n");
            break;
        }

        case CMD_DHT_STREAM_OFF:
            dht_set_stream(false, 0);
            if (m.ctx) cmd_reply(m.ctx, "DHTSTREAM OFF\n");
            break;

        case CMD_DHT_STATE: {
            bool on = false; uint32_t interval = 0;
            dht_get_stream_state(&on, &interval);
            dht_sample_t s; dht_read_latest(&s);
            if (m.ctx) {
                char buf[96];
                snprintf(buf, sizeof(buf),
                        "DHTSTATE stream=%d interval=%u valid=%d age=%u ms\n",
                        on ? 1 : 0, (unsigned)interval, s.valid ? 1 : 0, (unsigned)s.age_ms);
                cmd_reply(m.ctx, buf);
            }
            break;
        }

        default:
            // Unknown/unhandled; ignore.
            break;
        }
    }
}

void cmd_router_start(void) {
    // Must be called after cmd_bus_init().
    configASSERT(cmd_bus_is_ready());
    if (!cmd_bus_is_ready()) return;
    xTaskCreate(cmd_router_task, "cmd.router", 4096, NULL, 5, NULL);
}
