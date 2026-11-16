// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// telemetry.h -- Phase 8 halt telemetry (BENCH_P8_PI.md 8.6).
//
// WHAT IT IS FOR. A Pi that halts or reboots on its own, or loses power while it
// runs, is the symptom of the faults this board can cause it (Q10, a reset, a
// supply drop). The RP2354 is the witness that survives those: it knows whether
// it ASKED the Pi to shut down, and it keeps counting while the Pi's own log may
// be the casualty. So the counts live here, not on the Pi.
//
// WHERE THEY LIVE.
//   - RAM, authoritative while running.
//   - A no-init RAM MIRROR that survives every reset except a power-on or
//     brownout, so a watchdog reset or SW2 loses nothing, and the next boot can
//     tell whether that reset cut a powered Pi. Every change is one COMMIT:
//     two copies, written alternately, each with a sequence number and a CRC.
//     A reset partway through a commit leaves the previous commit, so a reset
//     sees the state before a change or after it -- never a mix, never a lost
//     history (review 2026-10-19, F2).
//   - FLASH, in the config record's reserved telemetry words (config_store.h),
//     written by cfg_telemetry_service() only after TELEMETRY_SAVE_QUIET_MS in
//     STANDBY: never with a Pi powered, and at most once per quiet spell, so a
//     boot loop cannot wear the flash.
//
// No SDK calls and no hardware access: compiled into the native host tests.
// Called from the core-0 superloop only (the firmware installs no interrupt
// handlers), so nothing here needs a critical section.

#ifndef PITRAC_TELEMETRY_H
#define PITRAC_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

// The config record's telemetry[] (config_store.h): the flash format.
#define TELE_WORDS 16

// Counters, in word order. Each saturates at UINT32_MAX.
typedef enum {
    TELE_BOOTS = 0,            // RP2354 boots, any cause
    TELE_BOOTS_WATCHDOG,       // ...after the watchdog timer expired: a hang
    TELE_PI_SHUTDOWN_REQ,      // shutdown requests sent to the Pi (RPI5_SHUTDOWN pulses)
    TELE_PI_SHUTDOWN_CLEAN,    // ...where the Pi read down in time and the latch opened after it
    TELE_PI_SHUTDOWN_TIMEOUT,  // ...where it never did: FAULT_PI_SHUTDOWN_TIMEOUT
    TELE_PI_DOWN_UNREQ,        // RUNNING: the Pi read down with no request from the board
    TELE_PI_BACK_UP,           // ...and read up again without a power cycle: a Pi reboot
    TELE_PI_POWER_CUT,         // the latch opened on a powered Pi other than after a clean
                               // shutdown -- including an RP2354 reset, counted at the next boot
    TELE_FAULTS,               // faults latched (fault_raise(), first-wins)
    TELE__COUNTERS
} tele_counter_t;

// Words after the counters.
#define TELE_W_LAST_FAULT         9u   // fault_t of the last fault latched
#define TELE_W_LAST_FAULT_UPTIME 10u   // ...seconds into the boot it happened in
#define TELE_W_LAST_FAULT_BOOT   11u   // ...that boot's TELE_BOOTS number
#define TELE_W_BOOT_REASON       12u   // tele_boot_t of this boot
#define TELE_W_LAYOUT            13u   // TELE_LAYOUT once written; 0 = never (an older record)
                                       // 14 and 15 are reserved, 0
#define TELE_LAYOUT              1u

typedef enum {
    TELE_BOOT_POWER_ON = 0,    // power-on, brownout, the RUN pin (SW2) or a debugger
    TELE_BOOT_WATCHDOG,        // the watchdog timer expired
    TELE_BOOT_SOFTWARE,        // a NORMAL boot after a reboot: `reset`, a boot ROM reboot,
                               // or the boot ROM's trial watchdog (a rollback)
    TELE_BOOT_FLASH_UPDATE,    // a FLASH_UPDATE boot: a UF2 load or an update from the Pi
                               // (from image 1.3; 1.2 and older called it a power-on)
} tele_boot_t;

typedef enum {
    TELE_FROM_NOTHING = 0,     // no valid mirror and no telemetry in flash: counting from 0
    TELE_FROM_FLASH,           // power-on: the last saved counts
    TELE_FROM_RAM,             // a warm reset: the mirror, including anything not yet saved
} tele_source_t;

// At boot, after cfg_init(). `saved` is the config record's telemetry words (NULL
// or an older record: none). Takes the newest intact mirror copy if there is one
// -- it is never older than flash -- else flash, else zeros. Then counts this
// boot, a watchdog boot, and a power cut if that copy says a Pi was at risk when
// the reset came.
void telemetry_init(const uint32_t saved[TELE_WORDS], tele_boot_t reason);

void telemetry_count(tele_counter_t c);
void telemetry_fault(uint32_t fault_code, uint32_t uptime_s);

// The power FSM, at the end of every step: would a reset right now cut a powered
// Pi? Mirrored, not saved to flash: it is what lets the next boot count a reset
// that cut a running Pi. Commits only when it changes.
void telemetry_pi_powered(bool at_risk);

// The power FSM, where a Pi stops being at risk: the latch is about to open
// (FORCE_OFF), or a shutdown has just been judged clean. ONE commit counts `c`
// (TELE__COUNTERS for nothing) and clears the at-risk flag, so a reset on either
// side of it counts the event exactly once: before it, the next boot counts the
// cut; after it, the event is already counted and nothing is at risk.
void telemetry_release_pi(tele_counter_t c);

uint32_t      telemetry_get(tele_counter_t c);
uint32_t      telemetry_word(unsigned i);       // any of the TELE_WORDS
tele_source_t telemetry_source(void);

// Saving: snapshot, write, then mark that snapshot saved. A change between the
// two keeps the counts dirty.
uint32_t telemetry_snapshot(uint32_t out[TELE_WORDS]);   // returns its generation
bool     telemetry_dirty(void);
void     telemetry_saved(uint32_t generation);

// `hist clear confirm`: counters and the last fault to zero (the boot reason
// stays). Saved like any other change.
void telemetry_clear(void);

const char *telemetry_counter_name(tele_counter_t c);
const char *telemetry_boot_name(tele_boot_t b);
const char *telemetry_source_name(tele_source_t s);

#if !(defined(PICO_ON_DEVICE) && PICO_ON_DEVICE)
// Host tests only:
//   - what a power-on does to the mirror (noise in both copies);
//   - one corrupted word in the newest copy, or in the other one;
//   - a hook called before every word a commit stores and once after the last:
//     where a test can stage a reset in the middle of a commit.
void telemetry_test_power_on(void);
void telemetry_test_mirror_flip(bool newest, unsigned word, uint32_t mask);
extern void (*telemetry_test_store_hook)(void);
#endif

#endif // PITRAC_TELEMETRY_H
