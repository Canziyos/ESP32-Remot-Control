// wifi.h
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void wifi_start(const char *ssid, const char *pwd);
void wifi_set_credentials(const char *ssid, const char *pwd);

#ifdef __cplusplus
}
#endif
