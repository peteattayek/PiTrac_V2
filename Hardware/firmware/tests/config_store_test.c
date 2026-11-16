// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// Host tests for config_store.c on a fake flash: the dual-slot record, its
// sequence numbers, the guard, and the Phase 8.6 halt telemetry's automatic
// writes (2026-10-19). No hardware, no SDK: the two config sectors are 8 KB of
// host memory behind XIP_BASE (mocks/hardware/flash.h).
#include "config_store.h"
#include "board.h"
#include "telemetry.h"
#include "power_fsm.h"
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *test_name;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, test_name, #condition); \
        return false; \
    } \
} while (0)

#define SLOTS_OFFSET (PICO_FLASH_SIZE_BYTES - 2u * FLASH_SECTOR_SIZE)

static uint8_t   s_flash[2u * FLASH_SECTOR_SIZE];   // slot A, then slot B
uintptr_t        mock_xip_base;
uintptr_t        mock_image_end;
static unsigned  attempts, erases;
static unsigned  skip_writes;      // writes that report success and never happen
static uint32_t  last_erase;

static void in_slots(uint32_t offs, size_t count) {
    if (offs < SLOTS_OFFSET || offs + count > SLOTS_OFFSET + sizeof(s_flash)) {
        fprintf(stderr, "%s: flash access outside the config sectors: 0x%lx + %u\n",
                test_name, (unsigned long)offs, (unsigned)count);
        exit(2);
    }
}

void flash_range_erase(uint32_t offs, size_t count) {
    in_slots(offs, count);
    if (offs % FLASH_SECTOR_SIZE || count != FLASH_SECTOR_SIZE) exit(2);
    erases++;
    last_erase = offs;
    memset(s_flash + (offs - SLOTS_OFFSET), 0xFF, count);
}

void flash_range_program(uint32_t offs, const uint8_t *data, size_t count) {
    in_slots(offs, count);
    for (size_t i = 0; i < count; i++)             // programming only clears bits
        s_flash[offs - SLOTS_OFFSET + i] &= data[i];
}

int flash_safe_execute(void (*func)(void *), void *param, uint32_t timeout_ms) {
    (void)timeout_ms;
    attempts++;
    if (skip_writes) { skip_writes--; return PICO_OK; }
    func(param);
    return PICO_OK;
}

// What config_store.c reads from the rest of the firmware.
static pstate_t state;
static uint32_t state_ms, clock_ms;
static bool     beam_on, armed, pi_present, pi_sensed;
pstate_t power_fsm_state(void)        { return state; }
uint32_t power_state_elapsed_ms(void) { return state_ms; }
bool     power_pi_present(void)       { return pi_present; }
// power_fsm.c's definition, over this file's two inputs (image 1.10, V19-F1).
bool     power_pi_maybe_present(void) { return pi_present || pi_sensed; }
bool     beam_enabled(void)           { return beam_on; }
bool     detect_armed(void)           { return armed; }
float    beam_duty(void)              { return 0.02f; }
void     beam_configure(uint32_t f, float d, int32_t p) { (void)f; (void)d; (void)p; }
void     adc_set_5vin_scale(float s)  { (void)s; }
float    adc_default_5vin_scale(void) { return 1.063f; }
void     detect_set_coalesce_us(uint32_t us) { (void)us; }
void     detect_set_path_mm(float mm) { (void)mm; }
// cfg_apply_threshold()'s target. -1 = never called since the fixture reset.
static int32_t  thr_applied;
static unsigned thr_calls;
void     detect_threshold_set_level(uint16_t level) { thr_applied = level; thr_calls++; }
absolute_time_t get_absolute_time(void) { return (uint64_t)clock_ms * 1000u; }
uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000u); }

static const pitrac_cfg_t *slot(int s) {
    return (const pitrac_cfg_t *)(s_flash + (size_t)s * FLASH_SECTOR_SIZE);
}

// A power-on: RAM is gone, flash is not. What main() does, in its order.
static void boot(void) {
    telemetry_test_power_on();
    cfg_init();
    telemetry_init(cfg()->telemetry, TELE_BOOT_POWER_ON);
    state = PS_STANDBY;
    state_ms = 0u;
}

static void reset_fixture(void) {
    memset(s_flash, 0xFF, sizeof(s_flash));
    mock_xip_base = (uintptr_t)s_flash - SLOTS_OFFSET;
    mock_image_end = 80u * 1024u;
    attempts = erases = skip_writes = 0u;
    last_erase = 0u;
    state = PS_STANDBY;
    state_ms = 0u;
    clock_ms = 1000u;
    beam_on = armed = pi_present = pi_sensed = false;
    thr_applied = -1;
    thr_calls = 0u;
}

// The firmware's CRC is the standard reflected CRC-32; a record an older
// firmware wrote can be built with this one.
static uint32_t crc32_ref(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void quiet_standby(void) {
    state = PS_STANDBY;
    state_ms = TELEMETRY_SAVE_QUIET_MS;
}

// ---------------------------------------------------------------------------

static bool blank_flash_defaults(void) {
    boot();
    CHECK(strcmp(cfg_source(), "defaults") == 0);
    CHECK(cfg()->seq == 0u);
    CHECK(telemetry_source() == TELE_FROM_NOTHING);
    CHECK(attempts == 0u);
    return true;
}

static bool save_and_reload(void) {
    boot();
    cfg_mut()->u12b_gain = 20.0f;
    CHECK(cfg_save());
    CHECK(erases == 1u && last_erase == SLOTS_OFFSET);       // slot A first
    CHECK(slot(0)->seq == 1u);
    CHECK(slot(0)->telemetry_only == 0u);
    CHECK(slot(0)->telemetry[TELE_BOOTS] == 1u);             // the telemetry rides along
    CHECK(slot(0)->telemetry[TELE_W_LAYOUT] == TELE_LAYOUT);
    CHECK(!telemetry_dirty());
    boot();
    CHECK(strcmp(cfg_source(), "slot A") == 0);
    CHECK(cfg()->u12b_gain == 20.0f);
    CHECK(telemetry_source() == TELE_FROM_FLASH);
    CHECK(telemetry_get(TELE_BOOTS) == 2u);
    return true;
}

// Each write goes to the slot NOT holding the newest record, so a reset during
// the erase can only cost the write in progress.
static bool slots_alternate(void) {
    boot();
    for (uint32_t i = 0; i < 5u; i++) {
        CHECK(cfg_save());
        uint32_t expect = SLOTS_OFFSET + (i % 2u) * FLASH_SECTOR_SIZE;
        CHECK(last_erase == expect);
        CHECK(slot((int)(i % 2u))->seq == i + 1u);
        if (i) CHECK(slot((int)((i + 1u) % 2u))->seq == i);  // the previous one survives
    }
    return true;
}

// The bug found 2026-10-19: `cfg default` zeroed the RAM seq, the next save
// wrote seq 1 beside an older record with a higher one, and the next boot
// quietly loaded the older record.
static bool default_then_save_sticks(void) {
    boot();
    cfg_mut()->u12b_gain = 20.0f;
    CHECK(cfg_save());                 // A, seq 1
    CHECK(cfg_save());                 // B, seq 2
    CHECK(cfg_save());                 // A, seq 3
    cfg_defaults(cfg_mut());           // `cfg default`
    CHECK(cfg_save());                 // must be seq 4, in B
    CHECK(slot(1)->seq == 4u);
    boot();
    CHECK(cfg()->u12b_gain == 14.5f);  // the defaults, not seq 3's 20
    CHECK(cfg()->seq == 4u);
    CHECK(strcmp(cfg_source(), "slot B") == 0);
    return true;
}

// A record written before 2026-10-19: zeros where the telemetry goes. It loads
// as before, the telemetry starts from nothing, and the first automatic write
// keeps every calibration field.
static bool old_record_upgrade(void) {
    pitrac_cfg_t old;
    cfg_defaults(&old);
    old.seq = 5u;
    old.u12b_gain = 20.0f;
    old.hpf_sel_track = 0u;
    old.crc32 = crc32_ref(&old, sizeof(old) - sizeof(uint32_t));
    memcpy(s_flash, &old, sizeof(old));
    boot();
    CHECK(strcmp(cfg_source(), "slot A") == 0);       // the CRC is the standard one
    CHECK(cfg()->u12b_gain == 20.0f);
    CHECK(telemetry_source() == TELE_FROM_NOTHING);
    quiet_standby();
    cfg_telemetry_service();
    CHECK(erases == 1u && last_erase == SLOTS_OFFSET + FLASH_SECTOR_SIZE);
    CHECK(slot(1)->seq == 6u);
    CHECK(slot(1)->u12b_gain == 20.0f && slot(1)->hpf_sel_track == 0u);
    CHECK(slot(1)->telemetry_only == 0u);
    CHECK(slot(1)->telemetry[TELE_BOOTS] == 1u);
    CHECK(memcmp(slot(0), &old, sizeof(old)) == 0);   // the old one untouched
    return true;
}

// Only in STANDBY, only after the quiet time, only with something new.
static bool tele_waits_for_quiet_standby(void) {
    boot();
    CHECK(telemetry_dirty());          // the boot itself
    state = PS_BENCH_RUNNING;
    state_ms = 600000u;
    cfg_telemetry_service();
    state = PS_RUNNING;
    cfg_telemetry_service();
    state = PS_STANDBY;
    state_ms = TELEMETRY_SAVE_QUIET_MS - 1u;
    cfg_telemetry_service();
    CHECK(attempts == 0u);
    state_ms = TELEMETRY_SAVE_QUIET_MS;
    cfg_telemetry_service();
    CHECK(attempts == 1u && erases == 1u);
    CHECK(!telemetry_dirty());
    cfg_telemetry_service();           // nothing new
    CHECK(attempts == 1u);
    telemetry_count(TELE_PI_SHUTDOWN_REQ);
    cfg_telemetry_service();
    CHECK(attempts == 2u);
    return true;
}

// The automatic write keeps the LAST SAVED calibration: a change made in RAM
// and never saved is not persisted behind the operator's back.
static bool tele_saves_flash_record_not_ram(void) {
    boot();
    cfg_mut()->u12b_gain = 20.0f;
    CHECK(cfg_save());
    cfg_mut()->u12b_gain = 30.0f;      // not saved
    telemetry_count(TELE_PI_POWER_CUT);
    quiet_standby();
    cfg_telemetry_service();
    CHECK(erases == 2u);
    CHECK(cfg()->u12b_gain == 30.0f);  // RAM untouched
    CHECK(cfg()->seq == 2u);
    boot();
    CHECK(cfg()->u12b_gain == 20.0f);
    CHECK(strcmp(cfg_source(), "slot B") == 0);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);
    return true;
}

// On a board that never had `cfg save`, the record is the defaults, and says so.
static bool tele_only_record(void) {
    boot();
    quiet_standby();
    cfg_telemetry_service();
    CHECK(erases == 1u);
    CHECK(slot(0)->telemetry_only == 1u);
    CHECK(strstr(cfg_source(), "telemetry only") != NULL);
    boot();
    CHECK(strstr(cfg_source(), "telemetry only") != NULL);
    CHECK(cfg()->hpf_sel_track == 0xffu);
    CHECK(telemetry_get(TELE_BOOTS) == 2u);
    CHECK(cfg_save());                 // the operator's first save clears the mark
    CHECK(strcmp(cfg_source(), "slot B") == 0);
    CHECK(slot(1)->telemetry_only == 0u);
    return true;
}

// A write that never happened leaves the older, VALID record in that slot. The
// read-back compares the whole record, so it is not mistaken for the new one.
static bool verify_catches_lost_write(void) {
    boot();
    CHECK(cfg_save());                 // A, seq 1
    CHECK(cfg_save());                 // B, seq 2
    skip_writes = 1u;
    CHECK(!cfg_save());                // A still holds seq 1
    CHECK(slot(0)->seq == 1u);
    CHECK(cfg_save());                 // and the next try lands, as seq 3
    CHECK(slot(0)->seq == 3u);
    boot();
    CHECK(cfg()->seq == 3u);
    return true;
}

// Failed automatic writes retry after TELEMETRY_SAVE_RETRY_MS and stop after
// TELEMETRY_SAVE_MAX_FAILS; `cfg save` still works.
static bool tele_retry_and_give_up(void) {
    boot();
    quiet_standby();
    skip_writes = 100u;
    cfg_telemetry_stat_t t;
    cfg_telemetry_service();
    CHECK(attempts == 1u);
    cfg_telemetry_status(&t);
    CHECK(t.failures == 1u && t.retry_wait && !t.gave_up);
    clock_ms += TELEMETRY_SAVE_RETRY_MS - 1u;
    cfg_telemetry_service();
    CHECK(attempts == 1u);
    clock_ms += 1u;
    for (unsigned k = 2; k <= TELEMETRY_SAVE_MAX_FAILS; k++) {
        cfg_telemetry_service();
        CHECK(attempts == k);
        clock_ms += TELEMETRY_SAVE_RETRY_MS;
    }
    cfg_telemetry_status(&t);
    CHECK(t.gave_up && !t.retry_wait);
    clock_ms += 10u * TELEMETRY_SAVE_RETRY_MS;
    cfg_telemetry_service();
    CHECK(attempts == TELEMETRY_SAVE_MAX_FAILS);      // stopped for this boot
    CHECK(telemetry_dirty());
    skip_writes = 0u;
    CHECK(cfg_save());
    CHECK(!telemetry_dirty());
    return true;
}

static bool guard_blocks_writes(void) {
    boot();
    quiet_standby();
    beam_on = true;
    CHECK(!cfg_save());
    cfg_telemetry_service();
    beam_on = false;
    armed = true;
    CHECK(!cfg_save());
    armed = false;
    pi_present = true;
    CHECK(!cfg_save());
    cfg_telemetry_service();
    pi_present = false;
    pi_sensed = true;                  // a Pi's 3V3, not classified (1.10)
    CHECK(!cfg_save());
    cfg_telemetry_service();
    pi_sensed = false;
    state = PS_RUNNING;
    CHECK(!cfg_save());
    CHECK(attempts == 0u);
    state = PS_BENCH_RUNNING;          // a bench session may `cfg save`...
    CHECK(cfg_save());
    return true;
}

// Image 1.6: a saved threshold reaches the DAC at boot. Board 4 (2026-11-11)
// saved level 51 and `cfg` showed it, while every reset left the DAC at 0.
static bool threshold_restored_at_boot(void) {
    boot();
    cfg_mut()->threshold_level = 51u;
    CHECK(cfg_save());
    boot();                            // RAM gone: only flash carries the 51
    CHECK(cfg()->threshold_level == 51u);
    cfg_apply_threshold();
    CHECK(thr_calls == 1u && thr_applied == 51);
    return true;
}

// A board that never saved a threshold keeps the boot default, 0.
static bool threshold_default_zero(void) {
    boot();
    cfg_apply_threshold();
    CHECK(thr_calls == 1u && thr_applied == 0);
    return true;
}

// Above DAC_TOP cannot come from `cfg save` (detect.c clamps): a corrupt record
// with a valid CRC. Nothing is applied, so the DAC keeps detect_init()'s 0.
static bool threshold_out_of_range_ignored(void) {
    pitrac_cfg_t bad;
    cfg_defaults(&bad);
    bad.seq = 3u;
    bad.threshold_level = (uint16_t)(DAC_TOP + 1u);
    bad.crc32 = crc32_ref(&bad, sizeof(bad) - sizeof(uint32_t));
    memcpy(s_flash, &bad, sizeof(bad));
    boot();
    CHECK(strcmp(cfg_source(), "slot A") == 0);       // it loads; only the level is refused
    cfg_apply_threshold();
    CHECK(thr_calls == 0u);
    // ...and the top legal level is applied as it is.
    bad.threshold_level = (uint16_t)DAC_TOP;
    bad.seq = 4u;
    bad.crc32 = crc32_ref(&bad, sizeof(bad) - sizeof(uint32_t));
    memcpy(s_flash + FLASH_SECTOR_SIZE, &bad, sizeof(bad));
    boot();
    cfg_apply_threshold();
    CHECK(thr_calls == 1u && thr_applied == (int32_t)DAC_TOP);
    return true;
}

// The current hint is non-NULL and names `want`. A NULL hint fails the CHECK
// rather than crashing inside strstr().
static bool hint_has(const char *want) {
    const char *h = cfg_save_unblock_hint();
    return h != NULL && strstr(h, want) != NULL;
}

// Image 1.6: every refusal names the command that clears it, and the hint and
// the reason always describe the same blocker.
static bool unblock_hint_names_the_command(void) {
    boot();
    CHECK(cfg_save_blocked_reason() == NULL && cfg_save_unblock_hint() == NULL);
    beam_on = true;
    CHECK(strstr(cfg_save_blocked_reason(), "beam") != NULL);
    CHECK(hint_has("'beam off'"));
    armed = true;                      // the beam still wins: the same order as the reason
    CHECK(hint_has("'beam off'"));
    beam_on = false;
    CHECK(strstr(cfg_save_blocked_reason(), "armed") != NULL);
    CHECK(hint_has("'detect disarm'"));
    armed = false;
    pi_present = true;
    CHECK(strstr(cfg_save_blocked_reason(), "Pi") != NULL);
    CHECK(hint_has("'off'"));
    pi_present = false;
    state = PS_RUNNING;
    CHECK(strstr(cfg_save_blocked_reason(), "power state") != NULL);
    CHECK(hint_has("STANDBY"));
    state = PS_BENCH_RUNNING;
    CHECK(cfg_save_blocked_reason() == NULL && cfg_save_unblock_hint() == NULL);
    return true;
}

// Image 1.10 (review finding V19-F1): a Pi's 3V3 sensed, with no Pi classified,
// blocks a save, the telemetry write and so an update's install. Each case names
// its own fix: in BENCH_RUNNING, the FSM's late-detect debounce (classify first:
// an `off` inside it is a hard cut); in STANDBY, a Pi powered some other way.
static bool sensed_pi_blocks_writes(void) {
    boot();
    state = PS_BENCH_RUNNING;
    pi_sensed = true;
    CHECK(!cfg_save());
    CHECK(strstr(cfg_save_blocked_reason(), "3V3 is sensed") != NULL);
    CHECK(hint_has("PI_BOOTING"));
    CHECK(!hint_has("USB-C"));
    pi_present = true;                 // classified: the 1.9 wording
    CHECK(strcmp(cfg_save_blocked_reason(), "a Pi is present") == 0);
    CHECK(hint_has("'off'") && !hint_has("PI_BOOTING"));
    pi_present = false;

    quiet_standby();                   // STANDBY, the latch open, a 3V3 sensed
    CHECK(!cfg_save());
    CHECK(strstr(cfg_save_blocked_reason(), "3V3 is sensed") != NULL);
    CHECK(hint_has("own USB-C") && hint_has("'3v3 0'"));
    cfg_telemetry_service();
    CHECK(attempts == 0u && erases == 0u);
    CHECK(telemetry_dirty());
    pi_sensed = false;                 // gone: the telemetry write goes ahead
    cfg_telemetry_service();
    CHECK(attempts == 1u && erases == 1u);
    CHECK(cfg_save_blocked_reason() == NULL && cfg_save_unblock_hint() == NULL);
    return true;
}

static bool overlap_refuses(void) {
    mock_image_end = SLOTS_OFFSET;     // the image reaches the config sectors
    boot();
    CHECK(strstr(cfg_source(), "OVERLAPS") != NULL);
    CHECK(!cfg_save());
    CHECK(cfg_save_blocked_reason() != NULL && cfg_save_unblock_hint() != NULL);
    quiet_standby();
    cfg_telemetry_service();
    CHECK(attempts == 0u && erases == 0u);
    return true;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(name) {#name, name}
        TEST(blank_flash_defaults), TEST(save_and_reload), TEST(slots_alternate),
        TEST(default_then_save_sticks), TEST(old_record_upgrade),
        TEST(tele_waits_for_quiet_standby), TEST(tele_saves_flash_record_not_ram),
        TEST(tele_only_record), TEST(verify_catches_lost_write),
        TEST(tele_retry_and_give_up), TEST(guard_blocks_writes), TEST(overlap_refuses),
        TEST(threshold_restored_at_boot), TEST(threshold_default_zero),
        TEST(threshold_out_of_range_ignored), TEST(unblock_hint_names_the_command),
        TEST(sensed_pi_blocks_writes)
#undef TEST
    };
    if (argc == 2) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            if (strcmp(argv[1], cases[i].name) != 0) continue;
            test_name = cases[i].name;
            reset_fixture();
            if (!cases[i].run()) return 1;
            printf("PASS %s\n", test_name);
            return 0;
        }
    }
    fprintf(stderr, "Expected one registered test name\n");
    return 2;
}
