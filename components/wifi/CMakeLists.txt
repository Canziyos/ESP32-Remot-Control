idf_component_register(SRCS "tcp_server.c" "wifi.c" INCLUDE_DIRS "."
                        REQUIRES bt command esp_wifi esp_event nvs_flash app_update)