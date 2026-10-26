idf_component_register(
    SRCS "main.cpp" "relay_controller.cpp" "relay_http_server.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_wifi esp_netif esp_event nvs_flash esp_http_server espressif__cjson pthread
    EMBED_TXTFILES "index.html"
)

set_target_properties(${COMPONENT_LIB} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS YES
)
