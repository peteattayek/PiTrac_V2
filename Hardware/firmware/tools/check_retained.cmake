# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
#
# check_retained.cmake -- run after every firmware link (CMakeLists.txt, POST_BUILD).
#
# Some RAM outlives a reboot into ANOTHER image: the update note (fw_update.c,
# s_notes) and the telemetry mirror (telemetry.c, s_mirror). The image that
# installs an update writes them; the image it installs reads them, at ITS
# addresses. If a change moves either one, an update from an older image to this
# one loses it without a word: no result reported, and -- for the note -- the
# Pi is not powered back on after the update. So the build stops here instead.
#
#   cmake -DNM=<arm-none-eabi-nm> -DELF=<pitrac.elf>
#         -DEXPECT=<name>:<address>:<size>[,...] -P check_retained.cmake
#
# If the move is deliberate: change the record's magic (FWU_NOTE_MAGIC, or the
# mirror's), set the new addresses in PITRAC_RETAINED (CMakeLists.txt), and write
# in PROGRESS.md that an update INTO this image from an older one is neither
# reported nor followed by the Pi's power-on (press the button). Never move them
# silently.

if (NOT NM OR NOT ELF OR NOT EXPECT)
    message(FATAL_ERROR "check_retained: NM, ELF and EXPECT are all required")
endif()

execute_process(COMMAND "${NM}" -S --defined-only "${ELF}"
                OUTPUT_VARIABLE syms RESULT_VARIABLE rc)
if (NOT rc EQUAL 0)
    message(FATAL_ERROR "check_retained: ${NM} failed on ${ELF}")
endif()

string(REPLACE "," ";" entries "${EXPECT}")
foreach (entry IN LISTS entries)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 name)
    list(GET parts 1 want_addr)
    list(GET parts 2 want_size)
    # nm -S: "<address> <size> <type> <name>", hex without 0x.
    string(REGEX MATCHALL "[0-9a-fA-F]+ [0-9a-fA-F]+ [bBdD] ${name}\n" found "${syms}")
    list(LENGTH found n)
    if (NOT n EQUAL 1)
        message(FATAL_ERROR "check_retained: ${n} symbols named ${name} in ${ELF} (want exactly 1)")
    endif()
    string(REGEX MATCH "([0-9a-fA-F]+) ([0-9a-fA-F]+)" _ "${found}")
    math(EXPR addr "0x${CMAKE_MATCH_1}" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR size "0x${CMAKE_MATCH_2}" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR want_a "${want_addr}" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR want_s "${want_size}" OUTPUT_FORMAT HEXADECIMAL)
    if (NOT addr STREQUAL want_a OR NOT size STREQUAL want_s)
        message(FATAL_ERROR
            "check_retained: ${name} is at ${addr}, ${size} bytes; the images before this one "
            "put it at ${want_a}, ${want_s} bytes. An update from them to this image would lose "
            "it. See tools/check_retained.cmake before changing PITRAC_RETAINED.")
    endif()
    message(STATUS "check_retained: ${name} at ${addr}, ${size} bytes, as before")
endforeach()
