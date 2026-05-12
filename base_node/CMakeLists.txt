cmake_minimum_required(VERSION 3.20.0)

list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(KCONFIG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/Kconfig)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(base_node)

target_sources(app PRIVATE
    src/main.c
    drivers/sensor_node/sensor_node.c
)

target_include_directories(app PRIVATE
    drivers/sensor_node
)