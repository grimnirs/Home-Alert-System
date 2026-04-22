# Copyright (c) 2025 Group 8
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Tell Zephyr where to find our custom DT bindings
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(base_node)

# Application source
target_sources(app PRIVATE src/main.c)

# Custom driver source (added to the Zephyr build system)
add_subdirectory(drivers/sensor_node)
