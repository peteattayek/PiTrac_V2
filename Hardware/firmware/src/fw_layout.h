// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// fw_layout.h -- where this image runs from, and the boot ROM calls behind
// fw_update.c (its fwu_port_* functions are implemented in fw_layout.c).
//
// With the A/B partition table (partition_table.json), the boot ROM maps the
// partition it launched at XIP_BASE through the QMI's address translation, so
// the image always runs at 0x10000000 wherever it is stored. Anything that needs
// the real place -- the config store's overlap check, the update's target, a
// read of the other partition -- asks here, and reads flash through the
// untranslated alias (XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE).

#ifndef PITRAC_FW_LAYOUT_H
#define PITRAC_FW_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

// Early in main(), BEFORE cfg_init() (whose overlap check needs the image's
// storage start). Reads the boot info and the partition table from the boot ROM.
void fw_layout_init(void);

// The physical flash offset this image is stored at: a partition's start, or 0
// on a board without a partition table.
uint32_t fw_layout_image_start(void);

// The boot ROM's BOOT_TYPE_* for the `upd` command ("normal", "flash update", ...).
const char *fw_layout_boot_type_name(uint8_t boot_type);

#endif // PITRAC_FW_LAYOUT_H
