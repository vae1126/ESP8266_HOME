idf_component_register(
    SRCS "src/mqtt_ha.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt_bridge json device_id switch_control status_led
)
