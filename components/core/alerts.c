#include <string.h>
#include "alerts.h"

static alert_record_t s_latest = {0, ALERT_NONE, ""};
static alerts_cb_t    s_cb     = NULL;
static uint16_t       s_seq    = 0;

void alert_latest(alert_record_t *out)
{
    if (!out) return;
    *out = s_latest;
}

void alerts_subscribe(alerts_cb_t cb)
{
    s_cb = cb;
    // Immediately push current snapshot to the new subscriber (optional behavior)
    if (s_cb) s_cb(&s_latest);
}

void alert_raise(alert_code_t code, const char *detail_opt)
{
    s_seq++;
    s_latest.seq  = s_seq;
    s_latest.code = code;

    const char *d = detail_opt ? detail_opt : "";
    strncpy(s_latest.detail, d, sizeof(s_latest.detail) - 1);
    s_latest.detail[sizeof(s_latest.detail) - 1] = '\0';

    if (s_cb) s_cb(&s_latest);
}
