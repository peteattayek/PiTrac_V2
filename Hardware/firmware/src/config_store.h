// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// config_store.h -- versioned, CRC'd, dual-slot config in the last 8 KB of flash.
//
// WHY THIS EXISTS NOW. Phase 3 produces calibration numbers that are expensive
// to reacquire: demod_phase_ticks takes a 26 s sweep, the phase model takes
// five of them, and scan carrier takes minutes of warm beam. Re-entering those
// by hand after every reset is how they end up stale or wrong. The Phase 8 halt
// telemetry counters ride on the same machinery (telemetry.h), written by
// cfg_telemetry_service() as well as by `cfg save`.
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
    uint8_t  phase_pure_delay;     // STORED VERDICT. Recomputed on restore when
                                   //   cal_duty is known -- see cli_init().
    uint8_t  _pad0;

    // The beam duty the phase and the model were calibrated at. WITHOUT THIS THE
    // RESTORED PHASE IS NOT INTERPRETABLE: phase_ticks carries a geometric term,
    //
    //     phase_ticks = t_chain_ticks - (duty/2) * (TOP+1)
    //
    // so a phase measured at 25 % is wrong by (TOP+1)*(0.25-D)/2 at any other
    // duty -- 166 ticks at the 2 % power-on default. 0 means "not recorded",
    // which is what an older saved record reads as.
    //
    // Carved out of `reserved` on 2026-08-25 rather than appended, because
    // slot_valid() compares `size` against sizeof(pitrac_cfg_t): growing the
    // struct would invalidate every existing saved record and silently revert
    // the board to defaults.
    float    cal_duty;

    // ---- Phase 8 halt telemetry (telemetry.h, TELE_WORDS) ----
    // Written by `cfg save` and by cfg_telemetry_service(). An older record has
    // zeros here, which telemetry_init() reads as "nothing saved yet".
    uint32_t telemetry[16];

    // 1 = written by cfg_telemetry_service() on a board that had never had a
    // `cfg save`: every calibration field is the default. Carved out of
    // `reserved` on 2026-10-19 like cal_duty, so the size and version stay: an
    // older record reads 0, "the operator saved this".
    uint8_t  telemetry_only;

    uint8_t  reserved[123];
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

// Push the saved comparator threshold into detect.c (image 1.6 on). Called
// AFTER detect_init(), which starts the DAC at level 0, for the same reason as
// cfg_apply_beam(). A saved level above DAC_TOP cannot come from `cfg save`, so
// a record carrying one is corrupt but CRC-valid: the boot default 0 stays.
//
// Before 1.6 nothing restored it. `cfg` printed the saved level while the DAC
// sat at 0, and a threshold tuned and saved in BENCH_P3_DETECT 3.7 was lost at
// every reset -- the same "saved but never restored" class as the carrier and
// phase (2026-08-14) and the phase model (2026-08-25).
void  cfg_apply_threshold(void);
const pitrac_cfg_t *cfg(void);
pitrac_cfg_t       *cfg_mut(void); // mark dirty by writing through this
bool  cfg_save(void);              // guarded; see below. Saves the halt telemetry too
void  cfg_defaults(pitrac_cfg_t *c);
const char *cfg_source(void);      // "slot A" / "slot B" / "defaults", + "telemetry only"

// Why cfg_save() would refuse right now, or NULL if it would proceed.
const char *cfg_save_blocked_reason(void);

// What the operator types to clear that refusal, or NULL when there is none.
// The same tests in the same order as cfg_save_blocked_reason(), so the two
// always describe the same blocker (image 1.6; before it the CLI only said
// "Quiesce first", and on Board 4 the owner had to work out `beam off`).
const char *cfg_save_unblock_hint(void);

// The running image reaches into the config sectors (every write is refused).
bool        cfg_image_overlaps(void);
// Where the two config sectors are: a physical flash offset, and 8 KB. Outside
// both A/B partitions (partition_table.json), so an update never touches them.
uint32_t    cfg_flash_offset(void);
uint32_t    cfg_flash_bytes(void);

// Phase 8.6: the halt telemetry to flash, on its own. Call from the main
// superloop ONLY -- never from pitrac_service(), which also runs inside blocking
// commands. Writes once the counts have changed and the board has sat in
// STANDBY for TELEMETRY_SAVE_QUIET_MS (so never with a Pi powered), under
// cfg_save()'s guard, retrying a failed write after TELEMETRY_SAVE_RETRY_MS and
// giving up for this boot after TELEMETRY_SAVE_MAX_FAILS.
//
// It writes the LAST SAVED record with new telemetry words. Calibration changed
// in RAM since the last `cfg save` is NOT persisted by it -- that stays the
// operator's decision.
void cfg_telemetry_service(void);

typedef struct {
    uint32_t saves;          // automatic writes this boot
    uint32_t failures;       // ...that did not verify
    bool     retry_wait;     // the last one failed: waiting to retry
    uint32_t retry_in_ms;
    bool     gave_up;        // TELEMETRY_SAVE_MAX_FAILS reached: no more this boot
} cfg_telemetry_stat_t;
void cfg_telemetry_status(cfg_telemetry_stat_t *out);

#endif // PITRAC_CONFIG_STORE_H
