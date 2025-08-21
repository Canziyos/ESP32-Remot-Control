idf_component_register(SRCS "led.c" 
                        INCLUDE_DIRS include
                        REQUIRES cmd esp_driver_gpio)