idf_component_register(SRCS "src/mqtt_ha.c"
                    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/main/include"
                    PRIV_INCLUDE_DIRS "${IDF_PATH}/components/mqtt/esp-mqtt/include"
                    REQUIRES mqtt json device_id led_control ota_upgrade)
