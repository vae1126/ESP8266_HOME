idf_component_register(
    SRCS "src/wifi_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_event tcpip_adapter freertos log wifi_provisioning
)
