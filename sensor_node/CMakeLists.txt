# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(sensor_node C CXX ASM)

pico_sdk_init()

add_executable(sensor_node
    main.c
)

target_link_libraries(sensor_node
    pico_stdlib
    hardware_adc
    hardware_i2c
    hardware_gpio
)

pico_enable_stdio_usb(sensor_node 1)
pico_enable_stdio_uart(sensor_node 0)

pico_add_extra_outputs(sensor_node)