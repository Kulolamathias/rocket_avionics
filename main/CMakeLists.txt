idf_component_register(
    SRCS 
        "main.c"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        core
        services
        drivers
        # config
        # utils
        tests
        esp_event
        efuse
        nvs_flash
        driver
)