cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sensor_node_tests)

target_sources(app PRIVATE
    test_frame_parser.c
)

target_include_directories(app PRIVATE
    ../drivers/sensor_node
)
