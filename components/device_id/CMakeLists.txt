idf_component_register(
    SRCS "src/device_id.c" "src/pthread_stubs.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash log
)
