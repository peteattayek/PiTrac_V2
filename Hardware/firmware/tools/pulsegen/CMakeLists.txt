# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

# pulsegen -- bench test equipment for BENCH_P5_P7_MIC_CAMERA.md 7b, on a spare Pico or
# Pico 2 (pulsegen.c says what it does and how to wire it). Not PiTrac firmware.
#   cmake -S tools/pulsegen -B tools/pulsegen/build_pico  -G Ninja -DPICO_BOARD=pico
#   cmake -S tools/pulsegen -B tools/pulsegen/build_pico2 -G Ninja -DPICO_BOARD=pico2
#   cmake --build tools/pulsegen/build_pico   (or build_pico2)

include(pico_sdk_import.cmake)
project(pulsegen C CXX ASM)
pico_sdk_init()

add_executable(pulsegen pulsegen.c)
pico_generate_pio_header(pulsegen ${CMAKE_CURRENT_LIST_DIR}/pulsegen.pio)
target_compile_options(pulsegen PRIVATE -Wall -Wextra)
target_link_libraries(pulsegen pico_stdlib hardware_pio hardware_clocks)
pico_enable_stdio_usb(pulsegen 1)
pico_enable_stdio_uart(pulsegen 0)
pico_add_extra_outputs(pulsegen)
