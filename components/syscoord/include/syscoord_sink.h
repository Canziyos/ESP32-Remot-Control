#pragma once
#include "alerts.h"
#ifdef __cplusplus
extern "C" { 
#endif
// Weak, default no-op sink. Syscoord calls this when an alert is raised.
void syscoord_alert_sink(const alert_record_t *rec);
#ifdef __cplusplus
}
#endif
