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
# PiTrac "The one board to rule them all" -- RP2354B firmware
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
    src/dma_safe.c
    src/power_fsm.c
    src/panel.c
    src/beam.c
    src/pio_alloc.c
    src/service.c
    src/shot.c
    src/detect.c
    src/cal.c
    src/cal_math.c
    src/config_store.c
    src/telemetry.c
    src/strobe_plan.c
    src/strobe_live.c
    src/strobe.c
    src/cam_plan.c
    src/camera.c
    src/sha256.c
    src/fw_update.c
    src/fw_layout.c
    src/link_frame.c
    src/link_proto.c
    src/link_cli.c
    src/pi_link.c
    src/cli.c
)

target_include_directories(pitrac PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

# Generated PIO headers land in the build dir, which is already on the include
# path for this target -> #include "detect.pio.h"
pico_generate_pio_header(pitrac ${CMAKE_CURRENT_LIST_DIR}/src/detect.pio)

# Phase 6's burst engine. Loaded by strobe.c since 2026-10-02 (6a/6b dry tests;
# live current 6c/6d since 2026-10-07); before that pioasm only checked it.
pico_generate_pio_header(pitrac ${CMAKE_CURRENT_LIST_DIR}/src/strobe_burst.pio)

# Phase 7 (2026-11-10): the camera handshake on PIO1, and the one-instruction
# gate strobe.c loads just below the burst program so the handshake's FIRE
# starts it. Both are also assembled for the host tests (tests/cam_pio_test.c).
pico_generate_pio_header(pitrac ${CMAKE_CURRENT_LIST_DIR}/src/cam_handshake.pio)
pico_generate_pio_header(pitrac ${CMAKE_CURRENT_LIST_DIR}/src/strobe_gate.pio)

target_link_libraries(pitrac
    pico_stdlib
    pico_unique_id
    hardware_adc
    hardware_dma
    hardware_pwm
    hardware_gpio
    hardware_watchdog
    hardware_pio
    hardware_flash
    hardware_xip_cache   # adc ringtest: exercise the snapshot with a cold XIP cache
    hardware_uart        # the Pi serial link (pi_link.c)
    pico_rand            # the link's session id, new at every boot (pi_link.c)
    pico_flash
    pico_bootrom         # partitions, explicit buy, update reboot (fw_layout.c)
)

# The image version, in the IMAGE_DEF the boot ROM reads (major.minor). The ROM
# compares it between the A and B partitions on a normal boot, and the Pi tool
# and `id`/`upd` show it. BUMP THE MINOR FOR EVERY IMAGE THAT LEAVES THIS PC:
# an update with a version no higher than the running one still installs (a
# FLASH_UPDATE boot prefers the new partition), but its buy then erases the old
# image whenever the old one would otherwise win the next normal boot (a higher
# version, or an equal one in A: equal versions boot A), and then there is
# nothing to roll back to. 1.1 is the first partition-aware image (2026-10-20).
# 1.2 (2026-11-10) is r5's code with only the version changed, for the update
# from the Pi (BENCH_P8_PI.md 8.9). 1.3 (2026-11-10): the 10 s shutdown holdoff,
# the flash-update `this boot` label and the released-J8.37 label. 1.4
# (2026-11-10): Phase 7, the camera handshake and a dry camera-gated burst
# (`cam`); it failed its independent review and never left this PC. 1.5
# (2026-11-10): 1.4 with the review's three findings fixed (H7-F1/F2/F3). 1.6
# (2026-11-11): `level` at any phase, the `cal demod` floor and phase restore,
# the saved threshold restored at boot, `cfg save` naming its fix; it failed its
# independent review on `level` (V16-F1) and never left this PC. 1.7
# (2026-11-11): 1.6 without the all-phase `level` (back to 1.5's current-phase
# reading) and with the review's comment note (V16-N1); the update for Board 4,
# which shipped on 1.5. 1.8 (2026-11-12): the strobe may fire with a Pi powered;
# live mode then arms no watchdog, and a live session ends if a Pi appears or goes
# away (owner's decision); `level` worded as an aiming aid (1.7 review, V17-N1).
# It failed its independent review (V18-F1: live mode used the power FSM's
# debounced Pi flag) and never left this PC. 1.9 (2026-11-12): 1.8 with the
# strobe's Pi observed fresh -- the 3V3 sensed now and the FSM's classification
# together, UNSETTLED when they disagree -- and V18-N1's comments. Its review
# confirmed that fix but did not pass (V19-F1: `reset`, `bootsel` and `cfg save`
# trusted the FSM's Pi flag, false for 0.1 s with a late Pi powered); it never
# left this PC. 1.10 (2026-11-12): 1.9 with those guards counting a Pi's 3V3
# sensed now as a Pi (power_pi_maybe_present()), and the review's two text notes.
# The minor is a number, not a decimal fraction: 1.10 is above 1.9 (the ROM, the
# board and pitrac-link all compare (major << 16) | minor). Its review did not
# pass (V110-F1: `reset`/`bootsel` checked, then waited 50 ms and rebooted without
# checking again); it never left this PC. 1.11 (2026-11-12): 1.10 with the guard
# checked again after that wait, immediately before the reboot, the watchdog
# disarmed only then, and no plain reset while the board is POWERING_ON. 1.12
# (2026-11-18): text only -- the `J4: NO CAMERA (CR-09)` printouts and comments
# retired after 7c, t_cam's measured ~0.2 us in comments, and the 1.11 review's
# two wording notes (V111-N1 watchdog bound, V111-N2 late-refusal message).
# 1.13 (2026-11-24): link protocol 2 -- the Pi can run any CLI command over the
# link, through the same dispatcher and guards (the owner's decision); a shared
# live strobe session; `forceoff`, `reset force` and `bootsel force` need
# `confirm` from the Pi; exactly-once for a resent command (link_cli.h). It
# failed its independent review (V113-F1..F6: a command could run twice or after
# a cancel, "stopped" could mean finished, a power cut from the Pi in FAULT, lost
# output unnoticed) and never left this PC. 1.14 (2026-11-24): link protocol 3,
# tickets the board issues and runs at most once, cancel by ticket, "stopped"
# only when a command acts on it, output offsets and a CRC; from the Pi `off`
# only as the orderly shutdown, `fault clear` in FAULT and `adc5vcal` need
# `confirm`; the beam held to 25 % effective for USB and the Pi (the owner).
# 1.14 failed its independent review (V114-F1..F8) and never left this PC.
# 1.15 (2026-11-25): its fixes -- a cancel already received is found before a
# ticket runs, the beam checks take U9 at its 200 us bound, a 0 ms wait still
# polls, a stopped `cal model` publishes nothing, the long dumps stop and say
# PARTIAL; and on the Pi's side, pitrac_link.py's stop, I/O-error and ticket fixes.
# 1.15 failed its independent confirm review (V115-F1..F7) and never left this PC.
# 1.16 (2026-11-26): its fixes -- CR LF is one Enter at the USB console, a stop on a
# dump's last line is too late to cut it, the beam's 25 % is on the PWM's high time
# (no U9 width assumed), `cal model` and `scan carrier` put the carrier back
# register for register, and link protocol 4 names the session in every CLI answer.
# On the Pi's and the PC's side: pitrac_link.py takes only its own (session, ticket),
# and a rollback to protocol 1 is UNKNOWN with the ticket; scope.py sends one
# terminator and takes only a whole record.
# 1.16 failed its independent confirm review (V116-F1..F4) and never left this PC.
# 1.17 (2026-11-26): the carrier held to 1-200 kHz on every path, the beam's pin held
# low through any reconfiguration so no pulse is stretched, and `capture ... id=<n>`
# says the id back in its header. On the PC's and the Pi's side: scope.py joins a
# line cut by a read timeout and takes only the record with its own id, and a
# cli-cancel that cannot open the port is UNKNOWN with the ticket. Link protocol 4,
# unchanged.
# 1.17 failed its independent confirm review (V117-F1..F3) and never left this PC.
# 1.18 (2026-11-26): the beam's pin given back after its dark with no counter wait (the
# first pulse may be short, never long), and a saved carrier restored over the beam's
# own 1-200 kHz range. On the PC's and the Pi's side: scope.py takes a record only
# after its command's own echo; cli-cancel is UNKNOWN with the ticket through open,
# tty setup, query, Ctrl-C and close. Link protocol 4, unchanged.
# 1.18 failed its independent confirm review (V118-F1, F2) and never left this PC.
# 1.19 (2026-11-26): 1.18's machine code with only the version changed. Its review's
# findings are all off the board: pitrac_link.py keeps a decided outcome whatever
# its output, a Ctrl-C or a failing close then do (V118-F1), BRINGUP_NEW_BOARD.md
# Step 6.3b takes the pin's high time from the period on the same trace (V118-F2),
# and scope.py words its id's limit (V118-N1). Link protocol 4, unchanged.
# 1.19 failed its independent confirm review (V119-F1..F3) and never left this PC.
# 1.20 (2026-11-27): 1.19's machine code with only the version changed. Its review's
# findings are all off the board again: pitrac_link.py counts Ctrl-C and acts on it only
# while an outcome is pending, with one decision point per exit (V119-F1); a stdout that
# blocks holds nothing up, and the poweroff request after an apply comes first (V119-F2);
# Step 6.3b's records hold the periods they measure (V119-F3). Link protocol 4, unchanged.
# 1.20 failed its independent confirm review (V120-F1..F4) and never left this PC.
# 1.21 (2026-11-27): 1.20's machine code with only the version changed. Its review's
# findings are all in pitrac_link.py: an update asks the board's update state first and
# drops only an image its own BEGIN started (V120-F1); two Ctrl-Cs still send the stop
# before the tool stops waiting (V120-F2); every frame is written in bounded time, a stop
# seen until the port takes its first byte (V120-F3); a writer thread that cannot start
# leaves its stream unavailable, and the exit decided stands (V120-F4). Link protocol 4,
# unchanged.
# 1.21 failed confirm review (V121-F1..F4); do not load it.
# 1.22 (2026-11-27): version bump only on the MCU. Pi client: stoppable CLI retry,
# accepted prefix on I/O errors, one frame deadline, truthful APPLY classification.
# 1.22 failed confirm review (V122-F1): a counted stop was lost on a retry wait I/O error.
# 1.23 (2026-11-27): MCU minor only; the Pi attempts the original ticket's abort
# after a zero-byte failed retry when it observes a pending stop.
# Images before
# 1.1 carry no version and do not run from a partition: never load one onto a
# converted board.
set(PITRAC_FW_VERSION_MAJOR 1)
set(PITRAC_FW_VERSION_MINOR 23)
pico_set_binary_version(pitrac MAJOR ${PITRAC_FW_VERSION_MAJOR} MINOR ${PITRAC_FW_VERSION_MINOR})

# Try before you buy (fw_update.h): the boot ROM runs this image only on a
# FLASH_UPDATE boot until it buys itself, and goes back to the previous image if
# it never does. A UF2 dropped on the BOOTSEL drive, `picotool load -x` and an
# update from the Pi all end in a FLASH_UPDATE boot. An SWD load followed by a
# reset does NOT: such an image is never launched. Turn this off only for SWD
# work, on a board without the partition table; the Pi refuses such an image.
option(PITRAC_TBYB "Flag the image try-before-you-buy (needed for updates from the Pi)" ON)
if (PITRAC_TBYB)
    target_compile_definitions(pitrac PRIVATE PICO_CRT0_IMAGE_TYPE_TBYB=1)
endif()

# TEST ONLY: an image that hangs at the end of init while on trial and never buys
# itself, to check on the bench that the boot ROM's trial watchdog puts the
# previous image back (BENCH_P8_PI.md 8.7). Build it in a directory of its own so
# the bench build never inherits it:
#     cmake -B build_hang -G Ninja -DPITRAC_TEST_TRIAL_HANG=ON
option(PITRAC_TEST_TRIAL_HANG "TEST ONLY: hang before the buy when launched on trial" OFF)
if (PITRAC_TEST_TRIAL_HANG)
    message(WARNING "PITRAC_TEST_TRIAL_HANG is ON: this image never buys itself. Test builds only.")
    target_compile_definitions(pitrac PRIVATE PITRAC_TEST_TRIAL_HANG=1)
endif()

# stdio on USB only. UART1 goes to the Pi and carries a framed binary protocol
# (link_frame.h) -- never let printf() onto it.
pico_enable_stdio_usb(pitrac 1)
pico_enable_stdio_uart(pitrac 0)

# No USB path may reset the RP2354 (PROGRESS.md section 9, found 2026-10-16). The
# SDK turns two on by default: a host opening the CDC port at 1200 baud, and
# picotool's vendor interface (`picotool reboot -f`, `picotool load -f`). Both
# reboot into BOOTSEL past the CLI's reset guard -- and on this board any reset
# opens the +5V latch, a hard power cut to a seated Pi. With them off, reflash
# with `bootsel` at the CLI (guarded) or SW1 + SW2; `picotool info` on a running
# board no longer works (it needs the board in BOOTSEL first).
target_compile_definitions(pitrac PRIVATE
    PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=0
    PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=0
)

pico_add_extra_outputs(pitrac)   # .uf2, .hex, .bin, .dis, .elf.map

# The RAM that outlives a reboot into ANOTHER image -- the update note
# (fw_update.c, s_notes) and the telemetry mirror (telemetry.c, s_mirror) -- is
# read by the next image at its own addresses: a contract between images. The
# build stops if either moves or changes size; tools/check_retained.cmake says
# what to do when a move is deliberate. name:address:size, as placed by r4
# (2026-10-20), the first image to carry the note.
set(PITRAC_RETAINED "s_mirror:0x20000110:0xa0,s_notes:0x200001b0:0x58")
add_custom_command(TARGET pitrac POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DELF=$<TARGET_FILE:pitrac>
            -DEXPECT=${PITRAC_RETAINED}
            -P ${CMAKE_CURRENT_LIST_DIR}/tools/check_retained.cmake
    VERBATIM)

# The A/B partition table (partition_table.json) as build/pitrac_pt.uf2, loaded
# ONCE per board over USB before the first partition-aware image (BENCH_P8_PI.md
# section 8.7). It writes flash sector 0 only; the config sectors are untouched.
pico_init_picotool()
if (TARGET picotool)
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/pitrac_pt.uf2
        # No BTStack on this board, and its silicon is A4 (erratum E10 is A2's):
        # picotool's warning about the last sectors does not apply.
        COMMAND picotool partition create --quiet
                ${CMAKE_CURRENT_LIST_DIR}/partition_table.json
                ${CMAKE_CURRENT_BINARY_DIR}/pitrac_pt.uf2
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/partition_table.json
        VERBATIM)
    add_custom_target(pitrac_pt ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/pitrac_pt.uf2)
endif()

# Warnings on. This firmware drives 9 A pulses; -Wall is not optional.
target_compile_options(pitrac PRIVATE
    -Wall -Wextra
    -Wno-unused-parameter
)
