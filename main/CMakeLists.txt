idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES device_id wifi_manager switch_control status_led mqtt_bridge mqtt_ha
)
