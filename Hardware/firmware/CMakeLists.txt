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

# ---------------------------------------------------------------------------
# PiTrac "Second Board To Rule Them All" â€” RP2354B firmware
#
# Build:
#   cmake -B build -G Ninja -DPICO_BOARD=pitrac_ltb_v1 -DPICO_PLATFORM=rp2350
#   cmake --build build
#
# See SETUP.md if the toolchain is not installed yet.
# ---------------------------------------------------------------------------

# FORCEd on purpose. There is exactly one board this firmware runs on, and IDE
# wizards (the VS Code Pico extension in particular) like to pass their own
# -DPICO_BOARD=pico2. That would build for an RP2350A: 30 GPIOs instead of 48,
# so GPIO31/33/39/43-47 would silently not exist. Better to ignore the wizard
# than to produce a binary that half-works on real hardware.
set(PICO_PLATFORM rp2350 CACHE STRING "RP2354B is an RP2350 core with stacked flash" FORCE)
set(PICO_BOARD pitrac_ltb_v1 CACHE STRING "Custom board header in boards/" FORCE)

# Let PICO_BOARD find boards/pitrac_ltb_v1.h
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)

include(pico_sdk_import.cmake)

project(pitrac C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pitrac
    src/main.c
    src/safe_state.c
    src/adc_engine.c
    src/power_fsm.c
    src/panel.c
    src/beam.c
    src/cli.c
)

target_include_directories(pitrac PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

target_link_libraries(pitrac
    pico_stdlib
    pico_unique_id
    hardware_adc
    hardware_dma
    hardware_pwm
    hardware_gpio
    hardware_watchdog
)

# stdio on USB only. UART1 goes to the Pi and will carry a framed binary protocol â€”
# never let printf() onto it.
pico_enable_stdio_usb(pitrac 1)
pico_enable_stdio_uart(pitrac 0)

pico_add_extra_outputs(pitrac)   # .uf2, .hex, .bin, .dis, .elf.map

# Warnings on. This firmware drives 9 A pulses; -Wall is not optional.
target_compile_options(pitrac PRIVATE
    -Wall -Wextra
    -Wno-unused-parameter
)
