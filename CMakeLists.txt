cmake_minimum_required(VERSION 3.22)

set(IDF_BUILD_V2 ON)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/env.cmake")

# ESP-IDF Build System V2 原生入口
include("$ENV{IDF_PATH}/tools/cmakev2/idf.cmake")

# 项目初始化
project(esp32-learn C CXX ASM)

# 创建默认应用及其依赖
idf_project_default()
