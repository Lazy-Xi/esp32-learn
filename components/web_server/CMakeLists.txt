file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "include"
    REQUIRES 
        esp_http_server
        espressif__cjson
    PRIV_REQUIRES
        log
)

# 生成前端文件系统镜像
littlefs_create_partition_image(
    littlefs
    "$ENV{WEB_DIST_DIR}"
    FLASH_IN_PROJECT
)
