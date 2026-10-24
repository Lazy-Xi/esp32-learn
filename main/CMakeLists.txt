idf_component_register(SRCS "main.cpp"
                    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "index.html")

set_target_properties(${COMPONENT_LIB} PROPERTIES
                     CXX_STANDARD 26
                     CXX_STANDARD_REQUIRED YES
                     CXX_EXTENSIONS YES)
