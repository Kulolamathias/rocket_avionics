idf_component_register(
    SRCS 
        "src/core_self_test.c"
        "src/timer_service_test.c"
        "src/wifi_mqtt_test.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        core
        services

        esp_timer
        efuse
)