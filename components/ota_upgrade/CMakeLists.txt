idf_component_register(SRCS "src/ota_upgrade.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "${IDF_PATH}/components/mqtt/esp-mqtt/include"
                    REQUIRES mqtt json device_id mqtt_ha esp_https_ota)
