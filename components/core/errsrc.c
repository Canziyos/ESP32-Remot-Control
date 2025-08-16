#include "errsrc.h"
#include <string.h>

static char s_err[64] = "NONE";
static errsrc_cb_t s_cb = 0;

/* Canonical strings for enums (kept small & stable). */
static const char* s_tab[] = {
    [ERRSRC_NONE]         = "NONE",
    [ERRSRC_NO_CREDS]     = "NO_CREDS",
    [ERRSRC_IP_LOST]      = "IP_LOST",
    [ERRSRC_AUTH_EXPIRE]  = "AUTH_EXPIRE",
    [ERRSRC_AUTH_FAIL]    = "AUTH_FAIL",
    [ERRSRC_NO_AP]        = "NO_AP",
    [ERRSRC_4WAY_TIMEOUT] = "4WAY_TIMEOUT",
    [ERRSRC_ASSOC_EXPIRE] = "ASSOC_EXPIRE",
    [ERRSRC_BEACON_TO]    = "BEACON_TO",
    [ERRSRC_DISCONNECTED] = "DISCONNECTED",
};

const char* errsrc_to_string(errsrc_t e) {
    if ((unsigned)e < (sizeof(s_tab)/sizeof(s_tab[0])) && s_tab[e]) return s_tab[e];
    return "UNKNOWN";
}

void errsrc_set_enum(errsrc_t e) {
    errsrc_set(errsrc_to_string(e));
}

void errsrc_set(const char *s) {
    if (!s || !*s) s = "NONE";
    if (strncmp(s_err, s, sizeof(s_err)) == 0) return;  // de-dup
    strncpy(s_err, s, sizeof(s_err)-1);
    s_err[sizeof(s_err)-1] = '\0';
    if (s_cb) s_cb(s_err);
}

const char* errsrc_get(void) {
    return s_err;
}

void errsrc_subscribe(errsrc_cb_t cb) {
    s_cb = cb;
    if (s_cb) s_cb(s_err);  // push current snapshot immediately.
}
