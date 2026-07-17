idf_component_register(
    SRCS "src/ota_upgrade.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt_bridge device_id status_led json esp_https_ota
)
