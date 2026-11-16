// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// fw_layout.c -- see fw_layout.h. The board side of fw_update.c's port.

#include "fw_layout.h"
#include "fw_update.h"
#include "config_store.h"

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "boot/bootrom_constants.h"
#include "boot/picobin.h"
#include "boot/picoboot_constants.h"

#include <stdio.h>
#include <string.h>

static fwu_layout_t s_layout;
static uint32_t     s_image_start;

static bool range_ok(uint32_t start, uint32_t size) {
    return size != 0u && start % FLASH_SECTOR_SIZE == 0u && size % FLASH_SECTOR_SIZE == 0u
        && start < PICO_FLASH_SIZE_BYTES && size <= PICO_FLASH_SIZE_BYTES - start;
}

static bool overlap(uint32_t a0, uint32_t an, uint32_t b0, uint32_t bn) {
    return a0 < b0 + bn && b0 < a0 + an;
}

static void decode(uint32_t loc, uint32_t *start, uint32_t *size) {
    const uint32_t first = (loc & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS)
                           >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
    const uint32_t last  = (loc & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS)
                           >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
    *start = first * FLASH_SECTOR_SIZE;
    *size  = last >= first ? (last - first + 1u) * FLASH_SECTOR_SIZE : 0u;
}

static uint32_t link_type(uint32_t flags) {
    return (flags & PICOBIN_PARTITION_FLAGS_LINK_TYPE_BITS) >> PICOBIN_PARTITION_FLAGS_LINK_TYPE_LSB;
}
static uint32_t link_value(uint32_t flags) {
    return (flags & PICOBIN_PARTITION_FLAGS_LINK_VALUE_BITS) >> PICOBIN_PARTITION_FLAGS_LINK_VALUE_LSB;
}

void fw_layout_init(void) {
    fwu_layout_t *L = &s_layout;
    memset(L, 0, sizeof(*L));
    L->boot_partition = -1;
    L->run_start = 0u;
    L->run_size  = PICO_FLASH_SIZE_BYTES;
    L->cfg_start = cfg_flash_offset();
    L->cfg_size  = cfg_flash_bytes();
    L->why_not   = "no partition table: the image runs flat from 0x0";

    boot_info_t bi;
    if (rom_get_boot_info(&bi)) {
        L->boot_type      = bi.boot_type;
        L->boot_partition = bi.partition;
        L->buy_pending    = (bi.tbyb_and_update_info & BOOT_TBYB_AND_UPDATE_FLAG_BUY_PENDING) != 0u;
    }

    // Where this image is stored: the translation of its own first address. The
    // ROM returns it in the XIP_BASE-based storage space.
    intptr_t st = rom_flash_runtime_to_storage_addr(XIP_BASE);
    uint32_t a = st < 0 ? 0u : (uint32_t)st;
    if (a >= XIP_BASE) a -= XIP_BASE;
    s_image_start = a < PICO_FLASH_SIZE_BYTES ? a : 0u;
    L->run_start = s_image_start;
    L->run_size  = PICO_FLASH_SIZE_BYTES - s_image_start;

    // The partition table, loaded by every flash boot; a no-op then. The boot ROM
    // wants ~3.25 KB of work area, and nothing is staged yet.
    size_t work_n = 0u;
    uint8_t *work = fwu_scratch(&work_n);
    if (work) (void)rom_load_partition_table(work, (uint32_t)work_n, false);

    static uint32_t buf[4u + 2u * PARTITION_TABLE_MAX_PARTITIONS];
    int rc = rom_get_partition_table_info(buf, (uint32_t)count_of(buf),
                                          PT_INFO_PT_INFO | PT_INFO_PARTITION_LOCATION_AND_FLAGS);
    if (rc < 4 || buf[0] != (PT_INFO_PT_INFO | PT_INFO_PARTITION_LOCATION_AND_FLAGS)) {
        L->why_not = "the boot ROM returned no partition table";
        return;
    }
    const uint32_t count = buf[1] & 0xFFu;
    const bool present = (buf[1] & 0x100u) != 0u;
    if (!present || count == 0u) return;              // flat: why_not as above
    if (count > PARTITION_TABLE_MAX_PARTITIONS || (uint32_t)rc < 4u + 2u * count) {
        L->why_not = "the partition table info is short";
        return;
    }

    // This image's partition: the one that starts where it is stored.
    int mine = -1;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t s, n;
        decode(buf[4u + 2u * i], &s, &n);
        if (range_ok(s, n) && s == s_image_start) { mine = (int)i; break; }
    }
    if (mine < 0) { L->why_not = "this image is not at the start of a partition"; return; }

    // Its A/B partner: a B partition names its A in its link; an A is named by
    // its B.
    int other = -1;
    const uint32_t mflags = buf[4u + 2u * (uint32_t)mine + 1u];
    if (link_type(mflags) == PICOBIN_PARTITION_FLAGS_LINK_TYPE_A_PARTITION) {
        other = (int)link_value(mflags);
    } else {
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t f = buf[4u + 2u * i + 1u];
            if ((int)i != mine && link_type(f) == PICOBIN_PARTITION_FLAGS_LINK_TYPE_A_PARTITION
                && link_value(f) == (uint32_t)mine) { other = (int)i; break; }
        }
    }
    if (other < 0 || (uint32_t)other >= count || other == mine) {
        L->why_not = "this image's partition has no A/B partner";
        return;
    }

    uint32_t rs, rn, os, on;
    decode(buf[4u + 2u * (uint32_t)mine], &rs, &rn);
    decode(buf[4u + 2u * (uint32_t)other], &os, &on);
    L->run_start = rs; L->run_size = rn;
    if (!range_ok(os, on) || overlap(os, on, rs, rn) || overlap(os, on, L->cfg_start, L->cfg_size)
        || overlap(rs, rn, L->cfg_start, L->cfg_size)) {
        L->why_not = "the A/B partitions overlap each other or the config sectors";
        return;
    }
    L->other_start = os;
    L->other_size  = on;
    L->partitioned = true;
    L->why_not     = NULL;
}

uint32_t fw_layout_image_start(void) { return s_image_start; }

const char *fw_layout_boot_type_name(uint8_t t) {
    switch (t & (uint8_t)~BOOT_TYPE_CHAINED_FLAG) {
        case BOOT_TYPE_NORMAL:       return "normal";
        case BOOT_TYPE_BOOTSEL:      return "BOOTSEL";
        case BOOT_TYPE_RAM_IMAGE:    return "RAM image";
        case BOOT_TYPE_FLASH_UPDATE: return "flash update";
        case BOOT_TYPE_PC_SP:        return "PC/SP";
        default:                     return "?";
    }
}

// ---------------------------------------------------------------------------
// fw_update.c's port
// ---------------------------------------------------------------------------
const fwu_layout_t *fwu_port_layout(void) { return &s_layout; }

// Only ever inside the partner partition: a range anywhere else -- this image,
// the config sectors, the partition table -- is refused here whatever the
// caller computed.
static bool in_partner(uint32_t offs, uint32_t n) {
    const fwu_layout_t *L = &s_layout;
    return L->partitioned && n != 0u && offs >= L->other_start
        && n <= L->other_size && offs - L->other_start <= L->other_size - n;
}

typedef struct { uint32_t offs; const uint8_t *src; uint32_t n; } flash_op_t;

// Interrupts off and, on a multicore build, the other core parked
// (flash_safe_execute). flash_range_erase/program run from RAM and take
// physical offsets, untouched by the address translation.
static void erase_cb(void *p)   { const flash_op_t *o = p; flash_range_erase(o->offs, FLASH_SECTOR_SIZE); }
static void program_cb(void *p) { const flash_op_t *o = p; flash_range_program(o->offs, o->src, o->n); }

bool fwu_port_erase(uint32_t offs) {
    if (offs % FLASH_SECTOR_SIZE || !in_partner(offs, FLASH_SECTOR_SIZE)) return false;
    flash_op_t o = { offs, NULL, FLASH_SECTOR_SIZE };
    return flash_safe_execute(erase_cb, &o, 500) == PICO_OK;
}

bool fwu_port_program(uint32_t offs, const uint8_t *src, uint32_t n) {
    if (n == 0u || n % FLASH_PAGE_SIZE || offs % FLASH_PAGE_SIZE
        || (offs % FLASH_SECTOR_SIZE) + n > FLASH_SECTOR_SIZE || !in_partner(offs, n)) return false;
    flash_op_t o = { offs, src, n };
    return flash_safe_execute(program_cb, &o, 500) == PICO_OK;
}

const uint8_t *fwu_port_read(uint32_t offs) {
    if (offs >= PICO_FLASH_SIZE_BYTES) return NULL;
    return (const uint8_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + offs);
}

int fwu_port_buy(void) {
    size_t n = 0u;
    uint8_t *work = fwu_scratch(&n);
    if (!work || n < FLASH_SECTOR_SIZE) return BOOTROM_ERROR_BUFFER_TOO_SMALL;
    return rom_explicit_buy(work, FLASH_SECTOR_SIZE);
}

int fwu_port_reboot_update(uint32_t start) {
    // p0 names the region just written, so the ROM tries that partition first.
    return rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS,
                      100u, XIP_BASE + start, 0u);
}

void fwu_port_reboot_normal(void) {
    (void)rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_NORMAL | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 200u, 0u, 0u);
    // Not expected to get here; the ROM's trial watchdog ends this too.
    for (;;) tight_loop_contents();
}

void fwu_port_log(const char *msg) {
    printf("update: %s\n", msg);
}
