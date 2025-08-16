// wifi.h
#pragma once
void wifi_start(const char *ssid, const char *pwd);
void wifi_set_credentials(const char *ssid, const char *pwd);

const char *wifi_get_last_error(void);
void wifi_set_last_error(const char *msg);
