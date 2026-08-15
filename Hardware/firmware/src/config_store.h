// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// config_store.h -- versioned, CRC'd, dual-slot config in the last 8 KB of flash.
//
// WHY THIS EXISTS NOW. Phase 3 produces calibration numbers that are expensive
// to reacquire: demod_phase_ticks takes a 26 s sweep, the phase model takes
// five of them, and scan carrier takes minutes of warm beam. Re-entering those
// by hand after every reset is how they end up stale or wrong. The Phase 8 halt
// telemetry counters ride on the same machinery.
//
// DUAL SLOT, ALTERNATING. Two 4 KB sectors, written alternately, newest `seq`
// wins. That gives wear levelling, but the real reason is POWER-FAIL ATOMICITY:
// the sector holding the only good copy is never the one being erased. A reset
// in the middle of a save costs the newest write, never the config.
//
// THE GUARD IS THE IMPORTANT PART. A 4 KB erase stalls XIP and holds interrupts
// off for tens of milliseconds. During that window the power FSM does not run,
// so the V5_MIN_SUSTAINED monitor is BLIND -- on a board where losing that
// monitor is what feeds a Pi 5 through a 1 A diode (see PROGRESS.md section 0).
// cfg_save() therefore refuses unless the machine is quiet. Never call it from
// the armed or firing path.
//
// CORE 1: not launched in Phase 3/4, so flash_safe_execute() takes its
// single-core path and pico_multicore is not linked. WHEN PHASE 6 LAUNCHES
// CORE 1, flash_safe_execute_core_init() MUST BE CALLED ON IT. The failure mode
// otherwise is core 1 executing XIP during an erase, which is a hard fault, not
// a corrupted write.

#ifndef PITRAC_CONFIG_STORE_H
#define PITRAC_CONFIG_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define CFG_MAGIC    0x50695443u   // "PiTC"
#define CFG_VERSION  1u

typedef struct {
    uint32_t magic, seq;
    uint16_t version, size;

    // ---- Phase 3/4 ----
    int32_t  demod_phase_ticks;
    uint32_t carrier_hz;
    uint16_t threshold_level;
    uint16_t detect_coalesce_us;
    float    adc5v_scale;          // was RAM-only; PROGRESS.md section 9
    float    phase_a0, phase_a1, phase_a2;
    uint32_t phase_f_lo, phase_f_hi;
    float    u12b_gain;            // 14.5 as built; higher once R98 is fitted
    float    path_mm;              // beam width, for velocity
    uint8_t  hpf_sel_track;        // measured by `hpf test`. 0xff = never run.
    uint8_t  cal_warm;             // were the cal figures taken warm?
    uint8_t  phase_pure_delay;
    uint8_t  _pad0;

    // ---- Phase 8 halt telemetry, reserved ----
    uint32_t telemetry[16];

    uint8_t  reserved[128];
    uint32_t crc32;                // over the record up to this field
} pitrac_cfg_t;

// The record size is part of the on-flash format. A version bump that keeps the
// size stays readable in both directions.
_Static_assert(sizeof(pitrac_cfg_t) == 256, "config record size is part of the format");

void  cfg_init(void);              // pick the newest valid slot, or defaults

// Push the saved carrier and demod phase into beam.c.
//
// SEPARATE FROM cfg_init() ON PURPOSE. beam_init() calls beam_configure() with
// the compile-time default, so anything cfg_init() pushed into the beam earlier
// would simply be overwritten. This must therefore be called AFTER beam_init().
// Splitting it is the only way to make the ordering explicit rather than a
// silent dependency on the order of two lines in main().
void  cfg_apply_beam(void);
const pitrac_cfg_t *cfg(void);
pitrac_cfg_t       *cfg_mut(void); // mark dirty by writing through this
bool  cfg_save(void);              // guarded; see below
void  cfg_defaults(pitrac_cfg_t *c);
const char *cfg_source(void);      // "slot A" / "slot B" / "defaults"

// Why cfg_save() would refuse right now, or NULL if it would proceed.
const char *cfg_save_blocked_reason(void);

#endif // PITRAC_CONFIG_STORE_H
