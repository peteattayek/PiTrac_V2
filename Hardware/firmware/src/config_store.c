// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "config_store.h"
#include "board.h"
#include "beam.h"
#include "detect.h"
#include "power_fsm.h"
#include "adc_engine.h"

#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include <string.h>

#define CFG_SLOT_SIZE     FLASH_SECTOR_SIZE                          // 4096
#define CFG_FLASH_OFFSET  (PICO_FLASH_SIZE_BYTES - 2u * CFG_SLOT_SIZE)
#define CFG_SLOT_A        (CFG_FLASH_OFFSET)
#define CFG_SLOT_B        (CFG_FLASH_OFFSET + CFG_SLOT_SIZE)

static pitrac_cfg_t s_cfg;
static const char  *s_source = "defaults";
static int          s_last_slot = -1;      // 0 = A, 1 = B

extern char __flash_binary_end;            // linker symbol

// Reflected CRC-32, computed nibble-wise so there is no 1 KB table in flash.
static uint32_t crc32(const void *data, size_t len) {
    static const uint32_t lut[16] = {
        0x00000000,0x1DB71064,0x3B6E20C8,0x26D930AC,0x76DC4190,0x6B6B51F4,
        0x4DB26158,0x5005713C,0xEDB88320,0xF00F9344,0xD6D6A3E8,0xCB61B38C,
        0x9B64C2B0,0x86D3D2D4,0xA00AE278,0xBDBDF21C };
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c = lut[(c ^  p[i]      ) & 0x0F] ^ (c >> 4);
        c = lut[(c ^ (p[i] >> 4)) & 0x0F] ^ (c >> 4);
    }
    return ~c;
}

static uint32_t cfg_crc(const pitrac_cfg_t *c) {
    return crc32(c, sizeof(*c) - sizeof(uint32_t));
}

static const pitrac_cfg_t *slot_ptr(int slot) {
    return (const pitrac_cfg_t *)(XIP_BASE + (slot ? CFG_SLOT_B : CFG_SLOT_A));
}

static bool slot_valid(const pitrac_cfg_t *c) {
    return c->magic == CFG_MAGIC &&
           c->size  == sizeof(pitrac_cfg_t) &&
           c->crc32 == cfg_crc(c);
}

void cfg_defaults(pitrac_cfg_t *c) {
    memset(c, 0, sizeof(*c));
    c->magic   = CFG_MAGIC;
    c->version = CFG_VERSION;
    c->size    = sizeof(pitrac_cfg_t);
    c->seq     = 0;
    c->carrier_hz         = SYSCLK_HZ / (CARRIER_TOP_DEFAULT + 1u);
    c->demod_phase_ticks  = 0;
    c->threshold_level    = 0;
    c->detect_coalesce_us = DETECT_COALESCE_US;
    c->adc5v_scale        = adc_default_5vin_scale();
    c->u12b_gain          = 14.5f;      // R98 unpopulated, as built
    c->path_mm            = 0.0f;       // unknown -> no velocity reported
    c->hpf_sel_track      = 0xff;       // never measured
}

void cfg_init(void) {
    // The image is ~80 KB against a 2 MB part, so this is defence against a
    // future growth surprise rather than a present risk -- but the alternative
    // failure is a config block silently overwritten by a reflash, which would
    // present as calibration mysteriously reverting.
    uintptr_t bin_end = (uintptr_t)&__flash_binary_end - XIP_BASE;
    if (bin_end >= CFG_FLASH_OFFSET) {
        cfg_defaults(&s_cfg);
        s_source = "defaults (BINARY OVERLAPS THE CONFIG SECTORS)";
        return;
    }

    const pitrac_cfg_t *a = slot_ptr(0), *b = slot_ptr(1);
    bool va = slot_valid(a), vb = slot_valid(b);

    if (va && vb)      { bool bnew = (int32_t)(b->seq - a->seq) > 0;
                         s_cfg = bnew ? *b : *a;
                         s_source = bnew ? "slot B" : "slot A";
                         s_last_slot = bnew ? 1 : 0; }
    else if (va)       { s_cfg = *a; s_source = "slot A"; s_last_slot = 0; }
    else if (vb)       { s_cfg = *b; s_source = "slot B"; s_last_slot = 1; }
    else               { cfg_defaults(&s_cfg); s_source = "defaults"; s_last_slot = -1; }

    // Push the values that other modules own back into them.
    if (s_cfg.adc5v_scale > 0.5f && s_cfg.adc5v_scale < 2.0f)
        adc_set_5vin_scale(s_cfg.adc5v_scale);
    if (s_cfg.detect_coalesce_us) detect_set_coalesce_us(s_cfg.detect_coalesce_us);
    if (s_cfg.path_mm > 0.0f)     detect_set_path_mm(s_cfg.path_mm);
}

void cfg_apply_beam(void) {
    // Guard against a corrupt-but-CRC-valid record steering the beam somewhere
    // dangerous: a frequency outside the range beam_configure() handles sanely,
    // or a phase outside one period, is a reason to fall back to the default.
    if (s_cfg.carrier_hz >= 5000u && s_cfg.carrier_hz <= 250000u) {
        beam_configure(s_cfg.carrier_hz, beam_duty(), s_cfg.demod_phase_ticks);
    }
}

const pitrac_cfg_t *cfg(void)     { return &s_cfg; }
pitrac_cfg_t       *cfg_mut(void) { return &s_cfg; }
const char         *cfg_source(void) { return s_source; }

const char *cfg_save_blocked_reason(void) {
    pstate_t st = power_fsm_state();
    if (st != PS_STANDBY && st != PS_BENCH_RUNNING)
        return "power state is not STANDBY or BENCH_RUNNING";
    if (beam_enabled())    return "the beam is on";
    if (detect_armed())    return "the detector is armed";
    if (power_pi_present())return "a Pi is present";
    return NULL;
}

typedef struct { uint32_t off; const uint8_t *src; } write_arg_t;

// Runs with interrupts off and, on a multicore build, the other core parked.
// flash_range_erase/program are already __no_inline_not_in_flash_func in the
// SDK, so RAM residency is handled for us.
static void cfg_write_cb(void *param) {
    const write_arg_t *w = param;
    flash_range_erase(w->off, CFG_SLOT_SIZE);
    flash_range_program(w->off, w->src, FLASH_PAGE_SIZE);
}

bool cfg_save(void) {
    const char *why = cfg_save_blocked_reason();
    if (why) return false;

    // Alternate slots so the sector holding the only good copy is never the one
    // being erased.
    int slot = (s_last_slot == 0) ? 1 : 0;

    static uint8_t page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
    s_cfg.magic   = CFG_MAGIC;
    s_cfg.version = CFG_VERSION;
    s_cfg.size    = sizeof(pitrac_cfg_t);
    s_cfg.seq++;
    s_cfg.crc32   = cfg_crc(&s_cfg);

    memset(page, 0xff, sizeof(page));
    memcpy(page, &s_cfg, sizeof(s_cfg));

    write_arg_t arg = { .off = slot ? CFG_SLOT_B : CFG_SLOT_A, .src = page };
    if (flash_safe_execute(cfg_write_cb, &arg, 500) != PICO_OK) {
        s_cfg.seq--;
        return false;
    }

    // Read back and verify before believing it. A save that silently did not
    // take is worse than one that failed loudly, because the next boot quietly
    // reverts to older calibration.
    if (!slot_valid(slot_ptr(slot))) { s_cfg.seq--; return false; }

    s_last_slot = slot;
    s_source    = slot ? "slot B" : "slot A";
    return true;
}
