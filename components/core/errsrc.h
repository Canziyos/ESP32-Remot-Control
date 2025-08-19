#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* canonical error sources used by monitor/wifi. */
typedef enum {
    ES_NONE = 0,
    ES_NO_CREDS,
    ES_IP_LOST,
    ES_AUTH_EXPIRE,
    ES_AUTH_FAIL,
    ES_NO_AP,
    ES_4WAY_TIMEOUT,
    ES_ASSOC_EXPIRE,
    ES_BEACON_TO,
    ES_DISCONNECTED,
} errsrc_t;


typedef void (*errsrc_cb_t)(const char *str);

/* String API. */
void errsrc_set(const char *s);
const char* errsrc_get(void);

/* Subscriber. */
void errsrc_subscribe(errsrc_cb_t cb);

/* Enum helpers. */
const char* errsrc_to_string(errsrc_t e);
void errsrc_set_enum(errsrc_t e);
errsrc_t errsrc_get_code(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
