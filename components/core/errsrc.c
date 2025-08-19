#include "errsrc.h"
#include <string.h>

static char s_err[64] = "NONE";
static errsrc_cb_t s_cb = 0;
static errsrc_t s_last = ES_NONE;

/* Canonical strings for enums. */
static const char* s_tab[] = {
    [ES_NONE] = "NONE",
    [ES_NO_CREDS] = "NO_CREDS",
    [ES_IP_LOST]  = "IP_LOST",
    [ES_AUTH_EXPIRE] = "AUTH_EXPIRE",
    [ES_AUTH_FAIL] = "AUTH_FAIL",
    [ES_NO_AP] = "NO_AP",
    [ES_4WAY_TIMEOUT] = "4WAY_TIMEOUT",
    [ES_ASSOC_EXPIRE] = "ASSOC_EXPIRE",
    [ES_BEACON_TO] = "BEACON_TO",
    [ES_DISCONNECTED] = "DISCONNECTED",
};

const char* errsrc_to_string(errsrc_t e) {
    if ((unsigned)e < (sizeof(s_tab) / sizeof(s_tab[0])) && s_tab[e]) return s_tab[e];
    return "UNKNOWN";
}

/* Map canonical string back to enum for callers that still set by string. */
static errsrc_t str_to_enum(const char *s) {
    if (!s || !*s) return ES_NONE;
    for (unsigned i = 0; i < sizeof(s_tab) / sizeof(s_tab[0]); ++i) {
        if (s_tab[i] && strcmp(s, s_tab[i]) == 0) return (errsrc_t)i;
    }
    return ES_NONE;
}

void errsrc_set_enum(errsrc_t e) {
    s_last = e;
    errsrc_set(errsrc_to_string(e));
}

void errsrc_set(const char *s) {
    if (!s || !*s) s = "NONE";
    if (strncmp(s_err, s, sizeof(s_err)) == 0) return;  /* de-dup. */
    strncpy(s_err, s, sizeof(s_err) - 1);
    s_err[sizeof(s_err) - 1] = '\0';
    s_last = str_to_enum(s);  /* keep enum in sync even for string callers. */
    if (s_cb) s_cb(s_err);
}

const char* errsrc_get(void) {
    return s_err;
}

void errsrc_subscribe(errsrc_cb_t cb) {
    s_cb = cb;
    if (s_cb) s_cb(s_err);  /* push current snapshot immediately. */
}

errsrc_t errsrc_get_code(void) {
    return s_last;
}
