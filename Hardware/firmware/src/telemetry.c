// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// telemetry.c -- see telemetry.h. No SDK calls: compiled into the host tests.

#include "telemetry.h"

#include <stddef.h>
#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
// Not zeroed by the runtime, so it survives a watchdog reset, SW2 and a reboot.
// A power-on leaves noise in it, which the magic, the layout and the CRC reject.
#define TELE_NOINIT(name) __uninitialized_ram(name)
#define STORE_POINT()     ((void)0)
#else
#define TELE_NOINIT(name) name
void (*telemetry_test_store_hook)(void);
#define STORE_POINT() do { if (telemetry_test_store_hook) telemetry_test_store_hook(); } while (0)
#endif

#define TELE_MIRROR_MAGIC 0x54454C31u   // "TEL1"

// One commit. Two are kept, written alternately, so the copy being written is
// never the newest valid one.
typedef struct {
    uint32_t magic;
    uint32_t seq;                       // of two valid copies, the higher wins
    uint32_t words[TELE_WORDS];
    uint32_t pi_powered;                // a Pi was at risk: a reset now is a cut
    uint32_t crc;                       // over everything above
} tele_mirror_t;

#define MIRROR_WORDS (sizeof(tele_mirror_t) / sizeof(uint32_t))

static tele_mirror_t TELE_NOINIT(s_mirror)[2];

static uint32_t      s_w[TELE_WORDS];
static uint32_t      s_gen;             // bumped on every change worth saving
static uint32_t      s_saved_gen;
static bool          s_pi_powered;
static tele_source_t s_source;
static uint32_t      s_mirror_seq;      // seq of the newest valid copy
static unsigned      s_mirror_next;     // the copy the next commit writes

// Reflected CRC-32, bitwise: 19 words, so speed does not matter.
static uint32_t crc32_bytes(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint32_t mirror_crc(const tele_mirror_t *m) {
    return crc32_bytes(m, offsetof(tele_mirror_t, crc));
}

static bool mirror_valid(const tele_mirror_t *m) {
    return m->magic == TELE_MIRROR_MAGIC
        && m->words[TELE_W_LAYOUT] == TELE_LAYOUT
        && m->crc == mirror_crc(m);
}

// The newest valid copy, or -1 when neither is valid.
static int mirror_newest(void) {
    bool v0 = mirror_valid(&s_mirror[0]);
    bool v1 = mirror_valid(&s_mirror[1]);
    if (v0 && v1) return (int32_t)(s_mirror[1].seq - s_mirror[0].seq) > 0 ? 1 : 0;
    return v0 ? 0 : (v1 ? 1 : -1);
}

// Commit RAM to the mirror. The new commit goes into the copy that is NOT the
// newest, one word at a time, in whatever order: a reset partway through leaves
// that copy failing its CRC, still holding the commit before last, or already
// equal to the new commit -- and in the first two cases the other copy, the
// previous commit, is the newest valid one. So a reset loses at most the change
// being committed, and never mixes two states (review 2026-10-19, F2).
static void mirror_commit(void) {
    tele_mirror_t next;
    next.magic = TELE_MIRROR_MAGIC;
    next.seq = s_mirror_seq + 1u;
    memcpy(next.words, s_w, sizeof(s_w));
    next.pi_powered = s_pi_powered ? 1u : 0u;
    next.crc = mirror_crc(&next);

    uint8_t *dst = (uint8_t *)&s_mirror[s_mirror_next];
    const uint8_t *src = (const uint8_t *)&next;
    for (size_t i = 0; i < MIRROR_WORDS; i++) {
        STORE_POINT();
        memcpy(dst + 4u * i, src + 4u * i, 4u);
    }
    STORE_POINT();
    s_mirror_seq   = next.seq;
    s_mirror_next ^= 1u;
}

static void bump(uint32_t *w) { if (*w != UINT32_MAX) (*w)++; }

static void changed(void) {
    s_gen++;
    mirror_commit();
}

void telemetry_init(const uint32_t saved[TELE_WORDS], tele_boot_t reason) {
    bool cut = false;
    int newest = mirror_newest();
    if (newest >= 0) {
        // A warm reset. The mirror is committed on every change, so it is never
        // older than flash and holds whatever had not been saved yet.
        const tele_mirror_t *m = &s_mirror[newest];
        memcpy(s_w, m->words, sizeof(s_w));
        cut = m->pi_powered != 0u;
        s_mirror_seq  = m->seq;
        s_mirror_next = (unsigned)newest ^ 1u;
        s_source = TELE_FROM_RAM;
    } else {
        // A power-on: both copies are noise. Clear them, so no stale bytes can
        // ever pass for a newer copy.
        memset(s_mirror, 0, sizeof(s_mirror));
        s_mirror_seq  = 0u;
        s_mirror_next = 0u;
        if (saved && saved[TELE_W_LAYOUT] == TELE_LAYOUT) {
            memcpy(s_w, saved, sizeof(s_w));
            s_source = TELE_FROM_FLASH;
        } else {
            // No telemetry yet: an older record keeps these words at zero.
            memset(s_w, 0, sizeof(s_w));
            s_source = TELE_FROM_NOTHING;
        }
    }
    s_w[TELE_W_LAYOUT] = TELE_LAYOUT;
    s_w[14] = s_w[15] = 0u;
    s_pi_powered = false;

    bump(&s_w[TELE_BOOTS]);
    if (reason == TELE_BOOT_WATCHDOG) bump(&s_w[TELE_BOOTS_WATCHDOG]);
    // The latch opens on every reset (R12), so a reset with a Pi at risk is a
    // power cut to it -- the hazard BENCH_P8_PI.md 8.0 is about.
    if (cut) bump(&s_w[TELE_PI_POWER_CUT]);
    s_w[TELE_W_BOOT_REASON] = (uint32_t)reason;

    // The boot itself is news: dirty until saved.
    s_saved_gen = 0u;
    s_gen = 1u;
    mirror_commit();
}

void telemetry_count(tele_counter_t c) {
    if ((unsigned)c >= (unsigned)TELE__COUNTERS) return;
    bump(&s_w[c]);
    changed();
}

void telemetry_fault(uint32_t fault_code, uint32_t uptime_s) {
    bump(&s_w[TELE_FAULTS]);
    s_w[TELE_W_LAST_FAULT]        = fault_code;
    s_w[TELE_W_LAST_FAULT_UPTIME] = uptime_s;
    s_w[TELE_W_LAST_FAULT_BOOT]   = s_w[TELE_BOOTS];
    changed();
}

void telemetry_pi_powered(bool at_risk) {
    if (at_risk == s_pi_powered) return;
    s_pi_powered = at_risk;
    mirror_commit();            // not a saved field: no generation bump
}

void telemetry_release_pi(tele_counter_t c) {
    bool count = (unsigned)c < (unsigned)TELE__COUNTERS;
    if (!count && !s_pi_powered) return;
    if (count) {
        bump(&s_w[c]);
        s_gen++;
    }
    s_pi_powered = false;
    mirror_commit();            // the count and the flag, together
}

uint32_t telemetry_get(tele_counter_t c) {
    return ((unsigned)c < (unsigned)TELE__COUNTERS) ? s_w[c] : 0u;
}

uint32_t telemetry_word(unsigned i) { return i < TELE_WORDS ? s_w[i] : 0u; }

tele_source_t telemetry_source(void) { return s_source; }

uint32_t telemetry_snapshot(uint32_t out[TELE_WORDS]) {
    memcpy(out, s_w, sizeof(s_w));
    return s_gen;
}

bool telemetry_dirty(void) { return s_gen != s_saved_gen; }

void telemetry_saved(uint32_t generation) { s_saved_gen = generation; }

void telemetry_clear(void) {
    uint32_t reason = s_w[TELE_W_BOOT_REASON];
    memset(s_w, 0, sizeof(s_w));
    s_w[TELE_W_BOOT_REASON] = reason;
    s_w[TELE_W_LAYOUT] = TELE_LAYOUT;
    changed();
}

const char *telemetry_counter_name(tele_counter_t c) {
    switch (c) {
        case TELE_BOOTS:               return "boots";
        case TELE_BOOTS_WATCHDOG:      return "boots after a watchdog hang";
        case TELE_PI_SHUTDOWN_REQ:     return "Pi shutdown requests";
        case TELE_PI_SHUTDOWN_CLEAN:   return "  ...Pi read down in time";
        case TELE_PI_SHUTDOWN_TIMEOUT: return "  ...timed out (latch opened anyway)";
        case TELE_PI_DOWN_UNREQ:       return "Pi went down unrequested";
        case TELE_PI_BACK_UP:          return "  ...and came back (Pi reboot)";
        case TELE_PI_POWER_CUT:        return "power cuts to a powered Pi";
        case TELE_FAULTS:              return "faults latched";
        case TELE__COUNTERS:           break;
    }
    return "?";
}

const char *telemetry_boot_name(tele_boot_t b) {
    switch (b) {
        case TELE_BOOT_POWER_ON: return "power-on, brownout, RUN pin (SW2) or debugger";
        case TELE_BOOT_WATCHDOG: return "WATCHDOG -- the firmware hung";
        case TELE_BOOT_SOFTWARE: return "software reboot ('reset', a boot ROM reboot, or a trial's rollback)";
        case TELE_BOOT_FLASH_UPDATE: return "flash update boot (a UF2 load, or an update from the Pi)";
    }
    return "?";
}

const char *telemetry_source_name(tele_source_t s) {
    switch (s) {
        case TELE_FROM_NOTHING: return "none saved -- counting from zero";
        case TELE_FROM_FLASH:   return "flash (power-on)";
        case TELE_FROM_RAM:     return "RAM mirror (warm reset; nothing lost)";
    }
    return "?";
}

#if !(defined(PICO_ON_DEVICE) && PICO_ON_DEVICE)
void telemetry_test_power_on(void) {
    memset(s_mirror, 0xA5, sizeof(s_mirror));
}

void telemetry_test_mirror_flip(bool newest, unsigned word, uint32_t mask) {
    int n = mirror_newest();
    unsigned copy = (n < 0) ? 0u : (newest ? (unsigned)n : (unsigned)n ^ 1u);
    if (word >= MIRROR_WORDS) return;
    uint8_t *p = (uint8_t *)&s_mirror[copy] + 4u * word;
    uint32_t v;
    memcpy(&v, p, 4u);
    v ^= mask;
    memcpy(p, &v, 4u);
}
#endif
