cmake_minimum_required(VERSION 3.13)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(geiger_gen3)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# project

add_executable(
    geiger_gen3
    geiger_gen3.cpp
)

target_include_directories(
    geiger_gen3 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/.. 
)

# Add pico_stdlib library which aggregates commonly used features
target_link_libraries(
    geiger_gen3 
    pico_multicore
    pico_stdlib 
    pico_cyw43_arch_lwip_threadsafe_background
    hardware_adc
)

pico_enable_stdio_usb(geiger_gen3 1)
pico_enable_stdio_uart(geiger_gen3 0)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(geiger_gen3)
