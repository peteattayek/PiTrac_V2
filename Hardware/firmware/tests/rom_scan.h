// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// rom_scan.h -- the boot ROM's view of a partition, on host memory: the test
// oracle for what a NORMAL boot can find there (tests/fw_update_test.c).
//
// It runs the boot ROM's own block scanner, s_varm_crit_next_block() from
// pico-bootrom-rp2350 (varm_blocks.c), unchanged, inside host definitions of
// ours (rom_scan.c). It shares nothing with fw_update.c's parser, so a test that
// asks it whether a partition holds a launchable image does not ask the code
// under test (review 2026-10-20, R4-F1).
//
// What it models: the first-block search over the partition's first 4 KB (as
// the ROM's BLOCK_LIST_SEARCH_MAX), the scanner reading on past them to finish a
// block or to search after an invalid one, and the block loop's links. What it
// does not: signatures, hashes, load maps, rollback versions, or the rest of the
// launch. "launchable" is conservative: any IMAGE_TYPE item in the loop that is an
// RP2350 Arm Secure executable without the TBYB flag, even if the loop then fails.

#ifndef PITRAC_TEST_ROM_SCAN_H
#define PITRAC_TEST_ROM_SCAN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     found;        // the scanner found a valid first block
    uint32_t first;        // ...at this offset in the partition
    int      blocks;       // valid blocks walked, the first included
    bool     loop_closed;  // the walk came back to the first block
    bool     launchable;   // an IMAGE_TYPE: EXE, Arm, Secure, RP2350, NOT TBYB
    uint32_t version;      // (major << 16) | minor of the launchable one (highest)
    bool     trial_only;   // an IMAGE_TYPE as above but TBYB-flagged
} rom_view_t;

// `part` must be 4-byte aligned; `size` is the partition's size in bytes.
rom_view_t rom_scan_partition(const uint8_t *part, uint32_t size);

#endif // PITRAC_TEST_ROM_SCAN_H
