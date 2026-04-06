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
        esp_video
        esp32-camera
)


target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp_video/include"
    "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp_video/private_include"
)