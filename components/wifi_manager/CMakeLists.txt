idf_component_register(SRCS "src/wifi_manager.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp8266 nvs_flash esp_event tcpip_adapter wpa_supplicant)
