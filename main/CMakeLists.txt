idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "." "include"
                    REQUIRES device_id led_control wifi_manager mqtt_ha ota_upgrade rssi_reporter nvs_flash pthread)
