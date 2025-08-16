#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* New: canonical error sources used by monitor/wifi. */
typedef enum {
    ERRSRC_NONE = 0,
    ERRSRC_NO_CREDS,
    ERRSRC_IP_LOST,
    ERRSRC_AUTH_EXPIRE,
    ERRSRC_AUTH_FAIL,
    ERRSRC_NO_AP,
    ERRSRC_4WAY_TIMEOUT,
    ERRSRC_ASSOC_EXPIRE,
    ERRSRC_BEACON_TO,
    ERRSRC_DISCONNECTED,
} errsrc_t;

typedef void (*errsrc_cb_t)(const char *str);

/* String API (unchanged): store/read the last error as a small safe string. */
void        errsrc_set(const char *s);
const char* errsrc_get(void);

/* One subscriber gets called on every change (like alerts.c pattern). */
void        errsrc_subscribe(errsrc_cb_t cb);

/* Optional convenience (safe to use or ignore): */
const char* errsrc_to_string(errsrc_t e);  /* maps enum -> canonical string */
void        errsrc_set_enum(errsrc_t e);   /* sets string via enum */

#ifdef __cplusplus
}
#endif
