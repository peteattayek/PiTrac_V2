// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "config_store.h"
#include "board.h"
#include "beam.h"
#include "detect.h"
#include "power_fsm.h"
#include "adc_engine.h"
#include "telemetry.h"

#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include <string.h>

#define CFG_SLOT_SIZE     FLASH_SECTOR_SIZE                          // 4096
#define CFG_FLASH_OFFSET  (PICO_FLASH_SIZE_BYTES - 2u * CFG_SLOT_SIZE)
#define CFG_SLOT_A        (CFG_FLASH_OFFSET)
#define CFG_SLOT_B        (CFG_FLASH_OFFSET + CFG_SLOT_SIZE)

_Static_assert(TELE_WORDS == 16, "the telemetry words are part of the config format");

static pitrac_cfg_t s_cfg;
static const char  *s_source = "defaults";
static int          s_last_slot = -1;      // 0 = A, 1 = B: the newest valid record
// The newest record's seq. Kept apart from s_cfg.seq, which `cfg default` zeroes:
// until 2026-10-19 the next save then wrote seq 1 beside an older record with a
// higher seq, and the next boot quietly picked the older one.
static uint32_t     s_seq;
// The image reaches into the config sectors: every write is refused.
static bool         s_overlap;
// The halt telemetry's automatic writes, this boot (cfg_telemetry_service()).
static uint32_t     s_tele_saves, s_tele_failures;
static bool         s_tele_retry;
static uint32_t     s_tele_failed_ms;

// Where the image ends, as a PHYSICAL flash offset. A macro only so the host
// tests (tests/config_store_test.c) can place a fake flash; the firmware uses the
// linker symbol. The symbol is a runtime address: with A/B partitions the image
// runs translated, at XIP_BASE wherever it is stored, so the image's storage
// start (fw_layout.c) is added to its length.
#ifndef CFG_IMAGE_END_OFFSET
#include "fw_layout.h"
extern char __flash_binary_end;            // linker symbol
#define CFG_IMAGE_END_OFFSET() \
    (fw_layout_image_start() + ((uintptr_t)&__flash_binary_end - XIP_BASE))
#endif

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

// Read through the untranslated, uncached alias. With A/B partitions the boot
// ROM maps only the running partition at XIP_BASE (QMI ATRANS), and the config
// sectors lie outside both partitions: read through XIP_BASE they are past the
// mapped window. Uncached also means a read-back after a write sees the flash,
// not the cache.
static const pitrac_cfg_t *slot_ptr(int slot) {
    return (const pitrac_cfg_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE +
                                  (slot ? CFG_SLOT_B : CFG_SLOT_A));
}

static void set_source(int slot, bool telemetry_only) {
    static const char *const k[2][2] = {
        { "slot A", "slot A (defaults + halt telemetry only)" },
        { "slot B", "slot B (defaults + halt telemetry only)" },
    };
    s_source = k[slot ? 1 : 0][telemetry_only ? 1 : 0];
}

// The version must match too. Magic, size and CRC prove the record is intact,
// not that its fields mean what this build thinks they mean: a layout change
// that keeps the size (a field repurposed, a float read as int) would pass all
// three. Bump CFG_VERSION on any such change and the old record falls back to
// defaults instead of being misread. Board 3's record is v1 (seq 5).
static bool slot_valid(const pitrac_cfg_t *c) {
    return c->magic   == CFG_MAGIC &&
           c->version == CFG_VERSION &&
           c->size    == sizeof(pitrac_cfg_t) &&
           c->crc32   == cfg_crc(c);
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
    // The image is 220 KB (2026-10-20, r4) against a 1016 KB partition or a 2 MB
    // flat layout, so this is defence against a future growth surprise rather
    // than a present risk -- but the alternative failure is a config block
    // silently overwritten by a reflash, which would present as calibration
    // mysteriously reverting. A trial image that fails it does not buy itself
    // (fw_update.c).
    // Once per boot in the firmware; the host tests call it once per simulated boot.
    s_overlap = false;
    s_tele_saves = s_tele_failures = 0;
    s_tele_retry = false;

    uintptr_t bin_end = CFG_IMAGE_END_OFFSET();
    if (bin_end >= CFG_FLASH_OFFSET) {
        cfg_defaults(&s_cfg);
        s_source = "defaults (BINARY OVERLAPS THE CONFIG SECTORS)";
        s_overlap = true;
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
    s_seq = s_cfg.seq;
    if (s_last_slot >= 0) set_source(s_last_slot, s_cfg.telemetry_only != 0u);

    // Push the values that other modules own back into them.
    if (s_cfg.adc5v_scale > 0.5f && s_cfg.adc5v_scale < 2.0f)
        adc_set_5vin_scale(s_cfg.adc5v_scale);
    if (s_cfg.detect_coalesce_us) detect_set_coalesce_us(s_cfg.detect_coalesce_us);
    if (s_cfg.path_mm > 0.0f)     detect_set_path_mm(s_cfg.path_mm);
}

void cfg_apply_beam(void) {
    // Guard against a corrupt-but-CRC-valid record steering the beam somewhere
    // dangerous: a frequency outside the carrier's range (board.h) is a reason to
    // fall back to the default, and beam_configure() refuses it anyway. The range
    // is the beam's own, beam_freq_allowed(), so whatever `cfg save` stored from a
    // running carrier comes back (image 1.18, review note V117-N2): until 1.17 this
    // took 5-250 kHz, and a saved 1250 Hz with its phase came back as the 104 kHz
    // default with phase 0.
    if (beam_freq_allowed(s_cfg.carrier_hz)) {
        beam_configure(s_cfg.carrier_hz, beam_duty(), s_cfg.demod_phase_ticks);
    }
}

const pitrac_cfg_t *cfg(void)     { return &s_cfg; }
pitrac_cfg_t       *cfg_mut(void) { return &s_cfg; }
const char         *cfg_source(void) { return s_source; }
bool                cfg_image_overlaps(void) { return s_overlap; }
uint32_t            cfg_flash_offset(void)   { return CFG_FLASH_OFFSET; }
uint32_t            cfg_flash_bytes(void)    { return 2u * CFG_SLOT_SIZE; }

// "A Pi" is power_pi_maybe_present(): the FSM's classification OR the Pi's 3V3
// sensed now (image 1.10, review finding V19-F1). With the FSM's flag alone, a
// late Pi's 3V3 was up in BENCH_RUNNING for PI_PRESENT_DEBOUNCE_MS with this
// guard open. The telemetry save and an update's install sit behind this guard
// too, so a 3V3 sensed in STANDBY (a Pi powered some other way) holds them off.
const char *cfg_save_blocked_reason(void) {
    if (s_overlap) return "the firmware image overlaps the config sectors";
    pstate_t st = power_fsm_state();
    if (st != PS_STANDBY && st != PS_BENCH_RUNNING)
        return "power state is not STANDBY or BENCH_RUNNING";
    if (beam_enabled())    return "the beam is on";
    if (detect_armed())    return "the detector is armed";
    if (power_pi_maybe_present())
        return power_pi_present() ? "a Pi is present"
                                  : "a Pi's 3V3 is sensed (GPIO24), not yet classified";
    return NULL;
}

// Keep this in step with cfg_save_blocked_reason() above: same tests, same order.
const char *cfg_save_unblock_hint(void) {
    if (s_overlap)
        return "nothing at the CLI: this image reaches into the config sectors. "
               "Load an image built for this flash layout";
    pstate_t st = power_fsm_state();
    if (st != PS_STANDBY && st != PS_BENCH_RUNNING)
        return "send 'off', wait until 'stat' shows STANDBY (a powered Pi is shut "
               "down first), then 'cfg save'";
    if (beam_enabled())    return "send 'beam off', then 'cfg save'";
    if (detect_armed())    return "send 'detect disarm', then 'cfg save'";
    if (power_pi_maybe_present()) {
        if (power_pi_present())
            return "send 'off', wait until 'stat' shows STANDBY (the Pi is shut down "
                   "first), then 'cfg save'";
        // BENCH_RUNNING, a late Pi inside the FSM's debounce: there `off` is still
        // a hard cut (no Pi to shut down), so the Pi is classified first.
        if (st != PS_STANDBY)
            return "wait until 'stat' shows the Pi (PI_BOOTING or RUNNING, within "
                   "0.1 s), then send 'off', wait until 'stat' shows STANDBY, then "
                   "'cfg save'";
        return "the latch is open, so something else powers the Pi (its own USB-C?): "
               "remove that supply, check that 'stat' shows '3v3 0', then 'cfg save'";
    }
    return NULL;
}

void cfg_apply_threshold(void) {
    if (s_cfg.threshold_level <= DAC_TOP)
        detect_threshold_set_level(s_cfg.threshold_level);
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

// Write `rec` as the newest record. Alternates slots, so the sector holding the
// only good copy is never the one being erased. Fills in the header, the next
// seq and the CRC; the caller checks the guard first.
static bool write_record(pitrac_cfg_t *rec) {
    int slot = (s_last_slot == 0) ? 1 : 0;

    rec->magic   = CFG_MAGIC;
    rec->version = CFG_VERSION;
    rec->size    = sizeof(pitrac_cfg_t);
    rec->seq     = s_seq + 1u;
    rec->crc32   = cfg_crc(rec);

    static uint8_t page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
    memset(page, 0xff, sizeof(page));
    memcpy(page, rec, sizeof(*rec));

    write_arg_t arg = { .off = slot ? CFG_SLOT_B : CFG_SLOT_A, .src = page };
    if (flash_safe_execute(cfg_write_cb, &arg, 500) != PICO_OK) return false;

    // Read back and verify before believing it. A save that silently did not
    // take is worse than one that failed loudly, because the next boot quietly
    // reverts to older calibration. The whole record, not just a valid CRC: a
    // valid older record left by an erase that never happened would pass that.
    if (memcmp(slot_ptr(slot), rec, sizeof(*rec)) != 0) return false;

    s_seq       = rec->seq;
    s_last_slot = slot;
    return true;
}

bool cfg_save(void) {
    const char *why = cfg_save_blocked_reason();
    if (why) return false;

    pitrac_cfg_t rec = s_cfg;
    uint32_t gen = telemetry_snapshot(rec.telemetry);
    rec.telemetry_only = 0u;
    if (!write_record(&rec)) return false;

    s_cfg = rec;
    telemetry_saved(gen);
    set_source(s_last_slot, false);
    return true;
}

void cfg_telemetry_service(void) {
    if (!telemetry_dirty()) return;
    if (s_tele_failures >= TELEMETRY_SAVE_MAX_FAILS) return;
    if (power_fsm_state() != PS_STANDBY) return;
    if (power_state_elapsed_ms() < TELEMETRY_SAVE_QUIET_MS) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (s_tele_retry && now - s_tele_failed_ms < TELEMETRY_SAVE_RETRY_MS) return;
    if (cfg_save_blocked_reason()) return;

    // The last SAVED record, not s_cfg: RAM changes stay unsaved until `cfg save`.
    pitrac_cfg_t rec;
    if (s_last_slot >= 0) {
        rec = *slot_ptr(s_last_slot);
    } else {
        cfg_defaults(&rec);
        rec.telemetry_only = 1u;
    }
    uint32_t gen = telemetry_snapshot(rec.telemetry);
    if (!write_record(&rec)) {
        s_tele_failures++;
        s_tele_retry = true;
        s_tele_failed_ms = now;
        return;
    }
    s_tele_saves++;
    s_tele_retry = false;
    s_cfg.seq = rec.seq;
    telemetry_saved(gen);
    set_source(s_last_slot, rec.telemetry_only != 0u);
}

void cfg_telemetry_status(cfg_telemetry_stat_t *out) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t waited = now - s_tele_failed_ms;
    out->saves       = s_tele_saves;
    out->failures    = s_tele_failures;
    out->gave_up     = s_tele_failures >= TELEMETRY_SAVE_MAX_FAILS;
    out->retry_wait  = !out->gave_up && s_tele_retry && waited < TELEMETRY_SAVE_RETRY_MS;
    out->retry_in_ms = out->retry_wait ? TELEMETRY_SAVE_RETRY_MS - waited : 0u;
}
