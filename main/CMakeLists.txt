file(GLOB_RECURSE MAIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp"
)

# 创建 main 组件
add_library(${COMPONENT_TARGET} STATIC
    ${MAIN_SOURCES}
)

# 导出头文件目录
target_include_directories(${COMPONENT_TARGET}
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/include"
)

set(MAIN_COMPONENTS
    driver
    esp_driver_gpio
    esp_wifi
    esp_event
    esp_netif
    joltwallet__littlefs
    nvs_flash
)

foreach(component IN LISTS MAIN_COMPONENTS)
    idf_component_include(${component})
    target_link_libraries(${COMPONENT_TARGET}
        PRIVATE
            idf::${component}
    )
endforeach()

if(CONFIG_WEB_SERVER_ENABLED)
    message(STATUS "HTTP enabled: including web_server")

    # 引入 Web Server 组件
    idf_component_include(web_server)

    # 建立组件链接关系
    target_link_libraries(${COMPONENT_TARGET}
        PRIVATE
            idf::web_server
    )

    # 生成前端文件系统镜像
    littlefs_create_partition_image(
        littlefs
        "${WEB_DIST_DIR}"
        FLASH_IN_PROJECT
    )
else()
    message(STATUS "HTTP disabled: web_server will not be included")
endif()

set_target_properties(${COMPONENT_TARGET} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS YES
)
