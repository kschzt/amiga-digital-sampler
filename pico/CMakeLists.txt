cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_amiga_sampler C CXX ASM)

pico_sdk_init()

add_executable(pico_amiga_sampler
    pico_amiga_sampler.c
)

pico_generate_pio_header(pico_amiga_sampler ${CMAKE_CURRENT_LIST_DIR}/sampleout.pio)

pico_enable_stdio_usb(pico_amiga_sampler 1)
pico_enable_stdio_uart(pico_amiga_sampler 0)

target_link_libraries(pico_amiga_sampler
    pico_stdlib
    hardware_pio
    hardware_spi
)

pico_add_extra_outputs(pico_amiga_sampler)
