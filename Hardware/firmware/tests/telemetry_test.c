// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// Host tests for telemetry.c, the Phase 8.6 halt telemetry: where the counts
// come from at boot (the RAM mirror, flash, or nothing), what a boot adds, and
// the save bookkeeping. No hardware, no SDK. power_fsm_test.c covers what the
// power FSM counts.
#include "telemetry.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static const char *test_name;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, test_name, #condition); \
        return false; \
    } \
} while (0)

// A saved record's telemetry words, as config_store.c would hand them over.
static void saved_words(uint32_t w[TELE_WORDS], uint32_t boots, uint32_t cuts) {
    memset(w, 0, TELE_WORDS * sizeof(uint32_t));
    w[TELE_BOOTS] = boots;
    w[TELE_PI_POWER_CUT] = cuts;
    w[TELE_W_LAYOUT] = TELE_LAYOUT;
}

static bool first_boot(void) {
    telemetry_test_power_on();
    telemetry_init(NULL, TELE_BOOT_POWER_ON);
    CHECK(telemetry_source() == TELE_FROM_NOTHING);
    CHECK(telemetry_get(TELE_BOOTS) == 1u);
    for (int k = 1; k < (int)TELE__COUNTERS; k++)
        CHECK(telemetry_get((tele_counter_t)k) == 0u);
    CHECK(telemetry_word(TELE_W_LAYOUT) == TELE_LAYOUT);
    CHECK(telemetry_word(TELE_W_BOOT_REASON) == (uint32_t)TELE_BOOT_POWER_ON);
    CHECK(telemetry_dirty());                   // the boot itself is news
    return true;
}

static bool from_flash(void) {
    uint32_t w[TELE_WORDS];
    saved_words(w, 41u, 3u);
    w[14] = w[15] = 0xDEADBEEFu;                // reserved: never carried forward
    telemetry_test_power_on();
    telemetry_init(w, TELE_BOOT_POWER_ON);
    CHECK(telemetry_source() == TELE_FROM_FLASH);
    CHECK(telemetry_get(TELE_BOOTS) == 42u);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 3u);
    CHECK(telemetry_word(14) == 0u && telemetry_word(15) == 0u);
    return true;
}

// A record saved before 2026-10-19 has zeros where the telemetry goes now.
static bool old_record_ignored(void) {
    uint32_t w[TELE_WORDS];
    memset(w, 0, sizeof(w));
    w[TELE_BOOTS] = 99u;                        // no layout word: not telemetry
    telemetry_test_power_on();
    telemetry_init(w, TELE_BOOT_POWER_ON);
    CHECK(telemetry_source() == TELE_FROM_NOTHING);
    CHECK(telemetry_get(TELE_BOOTS) == 1u);
    return true;
}

// A warm reset keeps everything, including counts flash never saw, and the
// mirror wins over an older flash copy.
static bool warm_reset_keeps_unsaved(void) {
    uint32_t w[TELE_WORDS];
    saved_words(w, 10u, 0u);
    telemetry_test_power_on();
    telemetry_init(w, TELE_BOOT_POWER_ON);      // 11 boots
    telemetry_count(TELE_PI_SHUTDOWN_REQ);
    telemetry_count(TELE_PI_SHUTDOWN_CLEAN);
    telemetry_init(w, TELE_BOOT_SOFTWARE);      // `reset`: flash still says 10
    CHECK(telemetry_source() == TELE_FROM_RAM);
    CHECK(telemetry_get(TELE_BOOTS) == 12u);
    CHECK(telemetry_get(TELE_PI_SHUTDOWN_REQ) == 1u);
    CHECK(telemetry_get(TELE_PI_SHUTDOWN_CLEAN) == 1u);
    CHECK(telemetry_word(TELE_W_BOOT_REASON) == (uint32_t)TELE_BOOT_SOFTWARE);
    return true;
}

// A power-on loses the mirror: back to the last saved counts.
static bool power_on_uses_flash(void) {
    uint32_t w[TELE_WORDS];
    saved_words(w, 10u, 0u);
    telemetry_test_power_on();
    telemetry_init(w, TELE_BOOT_POWER_ON);
    telemetry_count(TELE_FAULTS);               // never saved
    telemetry_test_power_on();
    telemetry_init(w, TELE_BOOT_POWER_ON);
    CHECK(telemetry_source() == TELE_FROM_FLASH);
    CHECK(telemetry_get(TELE_BOOTS) == 11u);
    CHECK(telemetry_get(TELE_FAULTS) == 0u);
    return true;
}

// One flipped bit anywhere in the newest copy -- magic, seq, a count, the Pi
// flag or the CRC -- and that copy is not trusted: the previous commit is used.
// Both copies damaged: flash.
static bool corrupt_mirror_rejected(void) {
    const unsigned n_words = 2u + TELE_WORDS + 1u + 1u;     // magic, seq, words, flag, crc
    for (unsigned word = 0; word < n_words; word++) {
        uint32_t w[TELE_WORDS];
        saved_words(w, 5u, 0u);
        telemetry_test_power_on();
        telemetry_init(w, TELE_BOOT_POWER_ON);              // boots 6: commit 1
        telemetry_count(TELE_PI_DOWN_UNREQ);                // commit 2
        telemetry_test_mirror_flip(true, word, 1u << (word % 32u));
        telemetry_init(w, TELE_BOOT_SOFTWARE);
        CHECK(telemetry_source() == TELE_FROM_RAM);         // commit 1
        CHECK(telemetry_get(TELE_BOOTS) == 7u);
        CHECK(telemetry_get(TELE_PI_DOWN_UNREQ) == 0u);     // only the damaged change is lost

        telemetry_test_power_on();
        telemetry_init(w, TELE_BOOT_POWER_ON);
        telemetry_count(TELE_PI_DOWN_UNREQ);
        telemetry_test_mirror_flip(false, word, 1u << (word % 32u));  // the older copy first,
        telemetry_test_mirror_flip(true, word, 1u << (word % 32u));   // then the newest
        telemetry_init(w, TELE_BOOT_SOFTWARE);
        CHECK(telemetry_source() == TELE_FROM_FLASH);
        CHECK(telemetry_get(TELE_BOOTS) == 6u);
        CHECK(telemetry_get(TELE_PI_DOWN_UNREQ) == 0u);
    }
    return true;
}

// --- A reset in the middle of a commit (independent review 2026-10-19, F2) ---
static jmp_buf  s_reset;
static unsigned s_points, s_fire_at;    // s_fire_at 0: count store points only

static void store_point(void) {
    if (++s_points != s_fire_at) return;
    telemetry_test_store_hook = NULL;
    longjmp(s_reset, 1);
}

static void watch_stores(unsigned fire_at) {
    s_points = 0u;
    s_fire_at = fire_at;
    telemetry_test_store_hook = store_point;
}

// A release that counts a cut, with a reset before, between or after any of its
// stores: the boot sees the cut counted exactly once, and the history before it.
static bool torn_release_counts_once(void) {
    for (volatile unsigned k = 1u; k < 100u; k++) {
        telemetry_test_power_on();
        telemetry_init(NULL, TELE_BOOT_POWER_ON);         // boots 1
        telemetry_count(TELE_FAULTS);                      // history
        telemetry_pi_powered(true);                        // a Pi at risk
        watch_stores(k);
        if (setjmp(s_reset) == 0) {
            telemetry_release_pi(TELE_PI_POWER_CUT);
            telemetry_test_store_hook = NULL;
            CHECK(k > 20u);                                // every store point was tried
            telemetry_init(NULL, TELE_BOOT_SOFTWARE);
            CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);
            CHECK(telemetry_get(TELE_FAULTS) == 1u);
            return true;
        }
        telemetry_init(NULL, TELE_BOOT_SOFTWARE);          // the reset, mid-commit
        CHECK(telemetry_source() == TELE_FROM_RAM);
        CHECK(telemetry_get(TELE_FAULTS) == 1u);
        CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);
        CHECK(telemetry_get(TELE_BOOTS) == 2u);
    }
    return false;
}

// An ordinary commit torn by a reset loses only the change in it.
static bool torn_count_keeps_history(void) {
    for (volatile unsigned k = 1u; k < 100u; k++) {
        telemetry_test_power_on();
        telemetry_init(NULL, TELE_BOOT_POWER_ON);
        telemetry_count(TELE_FAULTS);
        watch_stores(k);
        if (setjmp(s_reset) == 0) {
            telemetry_count(TELE_PI_DOWN_UNREQ);
            telemetry_test_store_hook = NULL;
            CHECK(k > 20u);
            telemetry_init(NULL, TELE_BOOT_SOFTWARE);
            CHECK(telemetry_get(TELE_PI_DOWN_UNREQ) == 1u);
            CHECK(telemetry_get(TELE_FAULTS) == 1u);
            return true;
        }
        telemetry_init(NULL, TELE_BOOT_SOFTWARE);
        CHECK(telemetry_source() == TELE_FROM_RAM);
        CHECK(telemetry_get(TELE_FAULTS) == 1u);
        CHECK(telemetry_get(TELE_PI_DOWN_UNREQ) <= 1u);
        CHECK(telemetry_get(TELE_BOOTS) == 2u);
    }
    return false;
}

// telemetry_release_pi() counts and clears in ONE commit; with nothing to count
// and nothing at risk it commits nothing.
static bool release_pi(void) {
    telemetry_test_power_on();
    telemetry_init(NULL, TELE_BOOT_POWER_ON);
    uint32_t w[TELE_WORDS];
    telemetry_saved(telemetry_snapshot(w));
    watch_stores(0u);
    telemetry_release_pi(TELE__COUNTERS);                  // nothing to do
    CHECK(s_points == 0u);
    CHECK(!telemetry_dirty());
    telemetry_pi_powered(true);
    watch_stores(0u);
    telemetry_release_pi(TELE__COUNTERS);                  // clears the flag only
    CHECK(s_points > 0u && s_points < 30u);                // one commit
    CHECK(!telemetry_dirty());                             // the flag is never saved
    telemetry_init(NULL, TELE_BOOT_SOFTWARE);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 0u);
    telemetry_pi_powered(true);
    watch_stores(0u);
    telemetry_release_pi(TELE_PI_SHUTDOWN_CLEAN);
    telemetry_test_store_hook = NULL;
    CHECK(s_points > 0u && s_points < 30u);                // still one commit
    CHECK(telemetry_dirty());
    CHECK(telemetry_get(TELE_PI_SHUTDOWN_CLEAN) == 1u);
    telemetry_init(NULL, TELE_BOOT_SOFTWARE);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 0u);
    CHECK(telemetry_get(TELE_PI_SHUTDOWN_CLEAN) == 1u);
    return true;
}

static bool boot_reasons(void) {
    telemetry_test_power_on();
    telemetry_init(NULL, TELE_BOOT_POWER_ON);
    telemetry_init(NULL, TELE_BOOT_SOFTWARE);
    CHECK(telemetry_get(TELE_BOOTS_WATCHDOG) == 0u);
    telemetry_init(NULL, TELE_BOOT_WATCHDOG);
    CHECK(telemetry_get(TELE_BOOTS_WATCHDOG) == 1u);
    CHECK(telemetry_get(TELE_BOOTS) == 3u);
    CHECK(telemetry_word(TELE_W_BOOT_REASON) == (uint32_t)TELE_BOOT_WATCHDOG);
    telemetry_init(NULL, TELE_BOOT_FLASH_UPDATE);   // a UF2 load or an update: no hang
    CHECK(telemetry_get(TELE_BOOTS_WATCHDOG) == 1u);
    CHECK(telemetry_get(TELE_BOOTS) == 4u);
    CHECK(telemetry_word(TELE_W_BOOT_REASON) == (uint32_t)TELE_BOOT_FLASH_UPDATE);
    return true;
}

// The Pi flag is mirrored, not saved: it counts a reset that cut a powered Pi,
// once, and a reset with no Pi powered counts nothing.
static bool power_cut_at_reset(void) {
    telemetry_test_power_on();
    telemetry_init(NULL, TELE_BOOT_POWER_ON);
    telemetry_saved(telemetry_snapshot((uint32_t[TELE_WORDS]){0}));
    telemetry_pi_powered(true);
    CHECK(!telemetry_dirty());                  // not a saved field
    telemetry_init(NULL, TELE_BOOT_SOFTWARE);   // reset under the Pi
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);
    telemetry_init(NULL, TELE_BOOT_SOFTWARE);   // init clears the flag: not again
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);
    telemetry_pi_powered(true);
    telemetry_pi_powered(false);                // the latch opened cleanly first
    telemetry_init(NULL, TELE_BOOT_SOFTWARE);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 1u);
    // A power-on loses the flag with the mirror: the Pi lost power with the
    // board, and no count is possible.
    telemetry_pi_powered(true);
    telemetry_test_power_on();
    telemetry_init(NULL, TELE_BOOT_POWER_ON);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == 0u);
    return true;
}

static bool saturates(void) {
    uint32_t w[TELE_WORDS];
    saved_words(w, UINT32_MAX, UINT32_MAX);
    telemetry_test_power_on();
    telemetry_init(w, TELE_BOOT_POWER_ON);
    CHECK(telemetry_get(TELE_BOOTS) == UINT32_MAX);
    telemetry_count(TELE_PI_POWER_CUT);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == UINT32_MAX);
    telemetry_count((tele_counter_t)TELE__COUNTERS);   // out of range: ignored
    telemetry_count((tele_counter_t)200);
    CHECK(telemetry_get((tele_counter_t)200) == 0u);
    CHECK(telemetry_word(TELE_WORDS) == 0u);
    return true;
}

// A change between the snapshot and its save keeps the counts dirty.
static bool dirty_and_saved(void) {
    uint32_t w[TELE_WORDS];
    telemetry_test_power_on();
    telemetry_init(NULL, TELE_BOOT_POWER_ON);
    uint32_t g = telemetry_snapshot(w);
    CHECK(w[TELE_BOOTS] == 1u);
    telemetry_saved(g);
    CHECK(!telemetry_dirty());
    telemetry_count(TELE_PI_SHUTDOWN_REQ);
    CHECK(telemetry_dirty());
    g = telemetry_snapshot(w);
    telemetry_count(TELE_PI_SHUTDOWN_CLEAN);    // lands mid-write
    telemetry_saved(g);
    CHECK(telemetry_dirty());
    telemetry_saved(telemetry_snapshot(w));
    CHECK(!telemetry_dirty());
    CHECK(w[TELE_PI_SHUTDOWN_CLEAN] == 1u);
    return true;
}

static bool fault_recorded(void) {
    telemetry_test_power_on();
    telemetry_init(NULL, TELE_BOOT_POWER_ON);
    telemetry_init(NULL, TELE_BOOT_SOFTWARE);   // boot 2
    telemetry_fault(7u, 123u);
    CHECK(telemetry_get(TELE_FAULTS) == 1u);
    CHECK(telemetry_word(TELE_W_LAST_FAULT) == 7u);
    CHECK(telemetry_word(TELE_W_LAST_FAULT_UPTIME) == 123u);
    CHECK(telemetry_word(TELE_W_LAST_FAULT_BOOT) == 2u);
    telemetry_fault(3u, 5u);
    CHECK(telemetry_get(TELE_FAULTS) == 2u);
    CHECK(telemetry_word(TELE_W_LAST_FAULT) == 3u);
    return true;
}

static bool clear_keeps_boot(void) {
    telemetry_test_power_on();
    telemetry_init(NULL, TELE_BOOT_WATCHDOG);
    telemetry_count(TELE_PI_POWER_CUT);
    telemetry_fault(2u, 9u);
    telemetry_saved(telemetry_snapshot((uint32_t[TELE_WORDS]){0}));
    telemetry_clear();
    for (int k = 0; k < (int)TELE__COUNTERS; k++)
        CHECK(telemetry_get((tele_counter_t)k) == 0u);
    CHECK(telemetry_word(TELE_W_LAST_FAULT) == 0u);
    CHECK(telemetry_word(TELE_W_BOOT_REASON) == (uint32_t)TELE_BOOT_WATCHDOG);
    CHECK(telemetry_word(TELE_W_LAYOUT) == TELE_LAYOUT);
    CHECK(telemetry_dirty());                   // saved like any change
    telemetry_init(NULL, TELE_BOOT_SOFTWARE);   // and the mirror holds the clear
    CHECK(telemetry_source() == TELE_FROM_RAM);
    CHECK(telemetry_get(TELE_BOOTS) == 1u);
    return true;
}

static bool names(void) {
    for (int k = 0; k < (int)TELE__COUNTERS; k++)
        CHECK(strcmp(telemetry_counter_name((tele_counter_t)k), "?") != 0);
    CHECK(strcmp(telemetry_counter_name(TELE__COUNTERS), "?") == 0);
    for (int b = TELE_BOOT_POWER_ON; b <= TELE_BOOT_FLASH_UPDATE; b++)
        CHECK(strcmp(telemetry_boot_name((tele_boot_t)b), "?") != 0);
    for (int s = TELE_FROM_NOTHING; s <= TELE_FROM_RAM; s++)
        CHECK(strcmp(telemetry_source_name((tele_source_t)s), "?") != 0);
    return true;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(name) {#name, name}
        TEST(first_boot), TEST(from_flash), TEST(old_record_ignored),
        TEST(warm_reset_keeps_unsaved), TEST(power_on_uses_flash),
        TEST(corrupt_mirror_rejected), TEST(boot_reasons), TEST(power_cut_at_reset),
        TEST(saturates), TEST(dirty_and_saved), TEST(fault_recorded),
        TEST(clear_keeps_boot), TEST(names), TEST(torn_release_counts_once),
        TEST(torn_count_keeps_history), TEST(release_pi)
#undef TEST
    };
    if (argc == 2) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            if (strcmp(argv[1], cases[i].name) != 0) continue;
            test_name = cases[i].name;
            if (!cases[i].run()) return 1;
            printf("PASS %s\n", test_name);
            return 0;
        }
    }
    fprintf(stderr, "Expected one registered test name\n");
    return 2;
}
