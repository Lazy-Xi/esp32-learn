file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
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
    EMBED_TXTFILES "index.html"
)

set_target_properties(${COMPONENT_LIB} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS YES
)
