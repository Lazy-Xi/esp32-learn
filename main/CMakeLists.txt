set(WEB_DIST_DIR "$ENV{WEB_DIST_DIR}")

file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_http_server
        espressif__cjson
        pthread
)

set_target_properties(${COMPONENT_LIB} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS YES
)

littlefs_create_partition_image(
    littlefs
    "${WEB_DIST_DIR}"
    FLASH_IN_PROJECT
)