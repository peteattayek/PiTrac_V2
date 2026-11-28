// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// Host tests for firmware updates from the Pi (fw_update.c) and the link's
// request handlers (link_proto.c), on a fake 2 MB flash laid out as
// partition_table.json: A 0x002000, B 0x100000 (1016 KB each), config 0x1FE000.
//
// The fake flash ENFORCES the rules instead of trusting the code: an erase or a
// program outside the partner partition ends the test run (exit 2), and
// programming can only clear bits, as on the real part. It also injects the
// faults review 2026-10-20 (R3-F1) asked about: stuck-low bits, a program that
// stops partway and says so, a reset partway through a program, and programs or
// erases that do nothing. A reboot is a longjmp back to the test, which then
// boots the way main() does (fwu_boot_check()); rom_pick_normal() is which image
// the boot ROM would launch on a normal boot, found by the ROM's own block scanner
// (rom_scan.c, never fw_update.c's parser), and fsm_step() what the power FSM does
// with an accepted power-on request.
#include "fw_update.h"
#include "link_proto.h"
#include "link_frame.h"
#include "sha256.h"
#include "board.h"
#include "power_fsm.h"
#include "config_store.h"
#include "adc_engine.h"
#include "safe_state.h"
#include "telemetry.h"
#include "cli.h"
#include "rom_scan.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <setjmp.h>
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

static bool fail_msg(const char *why) {
    fprintf(stderr, "%s: %s\n", test_name, why);
    return false;
}

#define FLASH_BYTES (2u * 1024u * 1024u)
#define PART_A      0x002000u
#define PART_B      0x100000u
#define PART_SIZE   (1016u * 1024u)
#define CFG_START   0x1FE000u
#define CFG_SIZE    0x2000u

// ---------------------------------------------------------------------------
// The board, as fw_update.c and link_proto.c see it.
// ---------------------------------------------------------------------------
// Words underneath, so the ROM's scanner can read it as it reads flash.
static union { uint8_t b[FLASH_BYTES]; uint32_t w[FLASH_BYTES / 4u]; } s_flash_mem;
#define s_flash (s_flash_mem.b)
static fwu_layout_t s_lay;
static pstate_t     s_state;
static uint32_t     s_state_ms, s_clock_ms;
static bool         s_latch;
static const char  *s_guard;            // cfg_save_blocked_reason()
static bool         s_overlaps;
static adc_5vin_status_t s_adc;
static unsigned     s_shutdown_reqs, s_on_reqs;
static bool         s_on_accepts;
static bool         s_req_on;           // a power-on accepted; fsm_step() acts on it
static bool         s_on_faults;        // ...by faulting (the USB guard), not powering on

// Flash operations, in order: 'E' erase, 'P' program, with the sector offset.
typedef struct { char op; uint32_t offs; } flash_op_t;
static flash_op_t   s_ops[1200];
static unsigned     s_nops;
static int          s_stuck_sector = -1;   // a sector whose programs lose a bit
static int          s_buy_rc;
static unsigned     s_buys;
static int          s_reboot_rc;           // 0: the reboot happens (longjmp)
static uint32_t     s_reboot_start;
static unsigned     s_normal_reboots;
static jmp_buf      s_jmp;
static int          s_last_jump;           // why s_jmp was taken: 1 update reboot, 2 normal, 3 reset
static const char  *s_last_log;
static char         s_logs[4096];          // every log line since reset_fixture()

// Faults injected into the partner's FIRST sector (the IMAGE_DEF's), by the
// order of the programs and erases that touch it, counted from 0. Off: -1.
static unsigned     s_s0_progs, s_s0_erases;
static uint32_t     s_flt_offs;            // the next program over this byte loses...
static uint8_t      s_flt_mask;            // ...these bits (stuck low, once). 0: none
static int          s_part_prog = -1;      // writes s_part_pages pages, then reports failure
static unsigned     s_part_pages;
static int          s_reset_prog = -1;     // a reset after s_reset_bytes bytes of it
static unsigned     s_reset_bytes;
static int          s_dead_progs_from = -1;   // from this one on, programs write nothing...
static bool         s_dead_progs_fail;        // ...and report failure (else success)
static int          s_dead_erases_from = -1;  // the same for erases
static bool         s_dead_erases_fail;
static int          s_skip_prog = -1;      // this one program alone writes nothing...
static bool         s_skip_prog_fail;      // ...and reports failure (else success)

pstate_t power_fsm_state(void)          { return s_state; }
uint32_t power_state_elapsed_ms(void)   { return s_state_ms; }
void     power_request_shutdown(void)   { s_shutdown_reqs++; }
bool     power_request_on(void)         { s_on_reqs++; if (s_on_accepts) s_req_on = true; return s_on_accepts; }
bool     power_pi_present(void)         { return s_state == PS_RUNNING; }
bool     power_rails_ready(void)        { return s_state == PS_RUNNING; }
const char *cfg_save_blocked_reason(void) { return s_guard; }
bool     cfg_image_overlaps(void)       { return s_overlaps; }
adc_5vin_status_t adc_5vin_read(float *v) { if (v) *v = 5.123f; return s_adc; }
bool     gpio_get(unsigned pin)         { (void)pin; return false; }
bool     gpio_get_out_level(unsigned pin) { return pin == PIN_LATCH_CONTROL ? s_latch : false; }
void     gpio_put(unsigned pin, bool v) { (void)pin; (void)v; }
absolute_time_t get_absolute_time(void) { return (uint64_t)s_clock_ms * 1000u; }
uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000u); }
fault_t  fault_current(void)            { return FAULT_NONE; }
uint32_t telemetry_word(unsigned i)     { return 0x1000u + i; }
const char *cli_build_stamp(void)       { return "Oct 19 2026 12:34:56"; }

static void must(bool ok, const char *what, uint32_t offs) {
    if (ok) return;
    fprintf(stderr, "%s: UNSAFE FLASH WRITE: %s at 0x%06lx\n", test_name, what, (unsigned long)offs);
    exit(2);
}

static bool in_partner(uint32_t offs, uint32_t n) {
    return s_lay.partitioned && offs >= s_lay.other_start
        && offs + n <= s_lay.other_start + s_lay.other_size;
}

const fwu_layout_t *fwu_port_layout(void) { return &s_lay; }

bool fwu_port_erase(uint32_t offs) {
    must(offs % FWU_SECTOR == 0u, "unaligned erase", offs);
    must(in_partner(offs, FWU_SECTOR), "erase outside the partner partition", offs);
    must(s_state == PS_STANDBY && !s_latch && s_guard == NULL, "erase while not quiet", offs);
    if (s_nops < sizeof(s_ops) / sizeof(s_ops[0])) s_ops[s_nops++] = (flash_op_t){ 'E', offs };
    if (offs == s_lay.other_start) {
        const int k = (int)s_s0_erases++;
        if (s_dead_erases_from >= 0 && k >= s_dead_erases_from) return !s_dead_erases_fail;
    }
    memset(&s_flash[offs], 0xFF, FWU_SECTOR);
    return true;
}

bool fwu_port_program(uint32_t offs, const uint8_t *src, uint32_t n) {
    must(n && n % FWU_PAGE == 0u && offs % FWU_PAGE == 0u && (offs % FWU_SECTOR) + n <= FWU_SECTOR,
         "bad program shape", offs);
    must(in_partner(offs, n), "program outside the partner partition", offs);
    must(s_state == PS_STANDBY && !s_latch && s_guard == NULL, "program while not quiet", offs);
    if (s_nops < sizeof(s_ops) / sizeof(s_ops[0])) s_ops[s_nops++] = (flash_op_t){ 'P', offs };
    uint32_t len = n;
    bool ok = true;
    if (offs / FWU_SECTOR == s_lay.other_start / FWU_SECTOR) {
        const int k = (int)s_s0_progs++;
        if (s_dead_progs_from >= 0 && k >= s_dead_progs_from) return !s_dead_progs_fail;
        if (k == s_skip_prog) return !s_skip_prog_fail;
        if (k == s_reset_prog) {
            for (uint32_t i = 0; i < s_reset_bytes && i < n; i++) s_flash[offs + i] &= src[i];
            s_last_jump = 3;
            longjmp(s_jmp, 3);
        }
        if (k == s_part_prog) {
            len = s_part_pages * FWU_PAGE < n ? s_part_pages * FWU_PAGE : n;
            ok = false;
        }
    }
    for (uint32_t i = 0; i < len; i++) s_flash[offs + i] &= src[i];       // only clears bits
    if (s_flt_mask && s_flt_offs >= offs && s_flt_offs < offs + len) {
        s_flash[s_flt_offs] &= (uint8_t)~s_flt_mask;                    // stuck low, this once
        s_flt_mask = 0u;
    }
    if (s_stuck_sector >= 0 && offs / FWU_SECTOR == (uint32_t)s_stuck_sector)
        s_flash[offs + 17u] ^= 0x01u;                                 // a bit that went wrong
    return ok;
}

const uint8_t *fwu_port_read(uint32_t offs) { return offs < FLASH_BYTES ? &s_flash[offs] : NULL; }

// The IMAGE_TYPE item's flags in a partition's first sector (first in its block,
// as the SDK writes it), or NULL.
static uint8_t *image_type_flags(uint32_t start) {
    for (uint32_t at = 0; at + 8u <= FWU_SECTOR; at += 4u) {
        uint8_t *p = &s_flash[start + at];
        if (p[0] == 0xd3u && p[1] == 0xdeu && p[2] == 0xffu && p[3] == 0xffu && p[4] == 0x42u) return &p[6];
    }
    return NULL;
}

// rom_explicit_buy(), as the boot ROM does it: the TBYB flag cleared in the
// running image's IMAGE_DEF; the other partition's first sector erased if its
// image's version is not lower, so the bought image stays the one that boots.
int fwu_port_buy(void) {
    s_buys++;
    if (s_buy_rc != 0) return s_buy_rc;
    uint8_t *t = image_type_flags(s_lay.run_start);
    if (t) t[1] &= 0x7Fu;
    const rom_view_t me = rom_scan_partition(&s_flash[s_lay.run_start], s_lay.run_size);
    const rom_view_t other = rom_scan_partition(&s_flash[s_lay.other_start], s_lay.other_size);
    if (me.launchable && other.launchable && other.version >= me.version)
        memset(&s_flash[s_lay.other_start], 0xFF, FWU_SECTOR);
    return 0;
}

int fwu_port_reboot_update(uint32_t start) {
    s_reboot_start = start;
    if (s_reboot_rc == 0) { s_last_jump = 1; longjmp(s_jmp, 1); }
    return s_reboot_rc;
}

void fwu_port_reboot_normal(void) { s_normal_reboots++; s_last_jump = 2; longjmp(s_jmp, 2); }

void fwu_port_log(const char *msg) {
    s_last_log = msg;
    const size_t have = strlen(s_logs), n = strlen(msg);
    if (have + n + 2u <= sizeof(s_logs)) {
        memcpy(&s_logs[have], msg, n);
        s_logs[have + n] = '\n';
        s_logs[have + n + 1u] = '\0';
    }
}

// The boot ROM's choice on a NORMAL boot (no FLASH_UPDATE): a partition is a
// candidate if the ROM's own scanner (rom_scan.c) finds an RP2350 Arm Secure
// executable IMAGE_DEF there that is NOT flagged TBYB -- an unbought trial is
// never launched normally; the higher version wins (varm_flash_boot.c). 0:
// neither. No test relies on equal versions.
static uint32_t rom_pick_normal(void) {
    const rom_view_t a = rom_scan_partition(&s_flash[PART_A], PART_SIZE);
    const rom_view_t b = rom_scan_partition(&s_flash[PART_B], PART_SIZE);
    if (a.launchable && b.launchable) return b.version > a.version ? PART_B : PART_A;
    return a.launchable ? PART_A : (b.launchable ? PART_B : 0u);
}

// A store hook (fwu_test_store_hook) that resets the board at the n-th store of
// a note commit, counted from when it is installed. 0: count only.
static unsigned s_hook_calls, s_hook_reset_at;
static jmp_buf  s_hook_jmp;
static void hook_reset(void) {
    if (++s_hook_calls == s_hook_reset_at) longjmp(s_hook_jmp, 1);
}

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

static void run_from(uint32_t start, bool trial) {
    memset(&s_lay, 0, sizeof(s_lay));
    s_lay.partitioned = true;
    s_lay.run_start   = start;
    s_lay.run_size    = PART_SIZE;
    s_lay.other_start = start == PART_A ? PART_B : PART_A;
    s_lay.other_size  = PART_SIZE;
    s_lay.cfg_start   = CFG_START;
    s_lay.cfg_size    = CFG_SIZE;
    s_lay.buy_pending = trial;
    s_lay.boot_type   = trial ? 4u : 0u;
    s_lay.boot_partition = start == PART_A ? 0 : 1;
}

static void reset_fixture(void) {
    memset(s_flash, 0xFF, sizeof(s_flash));
    run_from(PART_A, false);
    s_state = PS_RUNNING;
    s_state_ms = 0u;
    s_clock_ms = 10000u;
    s_latch = true;
    s_guard = "a Pi is present";
    s_overlaps = false;
    s_adc = ADC_5VIN_FRESH;
    s_shutdown_reqs = s_on_reqs = 0u;
    s_on_accepts = true;
    s_req_on = s_on_faults = false;
    s_nops = 0u;
    s_stuck_sector = -1;
    s_buy_rc = 0;
    s_buys = 0u;
    s_reboot_rc = 0;
    s_reboot_start = 0u;
    s_normal_reboots = 0u;
    s_last_jump = 0;
    s_last_log = NULL;
    s_logs[0] = '\0';
    s_s0_progs = s_s0_erases = 0u;
    s_flt_offs = 0u;
    s_flt_mask = 0u;
    s_part_prog = s_reset_prog = s_dead_progs_from = s_dead_erases_from = -1;
    s_part_pages = s_reset_bytes = 0u;
    s_dead_progs_fail = s_dead_erases_fail = false;
    s_skip_prog = -1;
    s_skip_prog_fail = false;
    fwu_test_store_hook = NULL;
    s_hook_calls = s_hook_reset_at = 0u;
    fwu_test_power_on();
}

static void put32le(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4u; i++) p[i] = (uint8_t)(v >> (8u * i));
}

// A synthetic image: a vector-table-sized prefix, then the SDK's start block
// (embedded_start_block.inc.S): IMAGE_TYPE, VERSION, LAST, link, end marker.
static uint8_t s_img[FWU_STAGE_BYTES + 4096u];

// The SDK's start block, 28 bytes at `at`: IMAGE_TYPE, VERSION, LAST, the link
// (bytes from this block to the next; 0, a loop of one), the end marker.
static void put_block(uint8_t *p, size_t at, uint16_t type, uint16_t major, uint16_t minor,
                      int32_t link) {
    uint8_t *b = &p[at];
    put32le(&b[0], 0xffffded3u);
    b[4] = 0x42; b[5] = 0x01; b[6] = (uint8_t)type; b[7] = (uint8_t)(type >> 8);
    b[8] = 0x48; b[9] = 0x02; b[10] = 0; b[11] = 0;
    b[12] = (uint8_t)minor; b[13] = (uint8_t)(minor >> 8); b[14] = (uint8_t)major; b[15] = (uint8_t)(major >> 8);
    b[16] = 0xff; b[17] = 3; b[18] = 0; b[19] = 0;          // LAST: 3 words of items before it
    put32le(&b[20], (uint32_t)link);
    put32le(&b[24], 0xab123579u);
}

// The SDK's end block, 20 bytes at `at`: one IGNORED item, LAST, the link back.
static void put_end_block(uint8_t *p, size_t at, int32_t link) {
    uint8_t *b = &p[at];
    put32le(&b[0], 0xffffded3u);
    b[4] = 0xfe; b[5] = 0x01; b[6] = 0; b[7] = 0;
    b[8] = 0xff; b[9] = 1; b[10] = 0; b[11] = 0;
    put32le(&b[12], (uint32_t)link);
    put32le(&b[16], 0xab123579u);
}

static size_t make_image(uint8_t *p, size_t n, uint16_t type, uint16_t major, uint16_t minor,
                         size_t block_at) {
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)((i * 2654435761u) >> 13);
    put_block(p, block_at, type, major, minor, 0);
    return n;
}

#define TYPE_ARM_S_TBYB   (0x1021u | 0x8000u)
#define TYPE_ARM_S        0x1021u

// For the boot ROM model: the image this firmware runs as, bought, version
// 1.<minor>, in its partition; and recognisable bytes in the partition table's
// 8 KB and the config sectors, so a test can see nothing there changed.
static uint8_t s_run_img[2u * FWU_SECTOR];
static void place_running(uint32_t start, uint16_t minor) {
    make_image(s_run_img, sizeof(s_run_img), TYPE_ARM_S, 1, minor, 0x110);
    memcpy(&s_flash[start], s_run_img, sizeof(s_run_img));
    memset(&s_flash[0], 0x5C, PART_A);
    memset(&s_flash[CFG_START], 0xC3, CFG_SIZE);
}

// FNV-1a over all of flash outside the partner partition.
static uint32_t outside_partner(void) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < FLASH_BYTES; i++) {
        if (i >= s_lay.other_start && i < s_lay.other_start + s_lay.other_size) continue;
        h = (h ^ s_flash[i]) * 16777619u;
    }
    return h;
}

static void digest(const uint8_t *p, size_t n, uint8_t out[32]) {
    sha256_t s;
    sha256_init(&s);
    sha256_update(&s, p, n);
    sha256_final(&s, out);
}

// Stage an image the way the Pi does: BEGIN, DATA in 251-byte chunks, END.
static fwu_err_t stage(const uint8_t *p, uint32_t n, uint32_t *version) {
    uint8_t d[32];
    digest(p, n, d);
    fwu_err_t e = fwu_begin(n, d);
    if (e != FWU_OK) return e;
    for (uint32_t at = 0; at < n; ) {
        uint32_t k = n - at < LINK_DATA_MAX ? n - at : LINK_DATA_MAX;
        uint32_t next = 0;
        e = fwu_data(at, &p[at], k, &next);
        if (e != FWU_OK) return e;
        at = next;
    }
    return fwu_end(version);
}

// The board's side of an orderly shutdown after UPD_APPLY: STANDBY, latch open,
// nothing else in the way.
static void go_quiet(uint32_t ms_in_standby) {
    s_state = PS_STANDBY;
    s_state_ms = ms_in_standby;
    s_latch = false;
    s_guard = NULL;
}

// What power_fsm_step() does with an accepted power-on, at its next step: it
// leaves STANDBY, for POWERING_ON (or PS_FAULT: the USB guard). It runs first
// in every superloop pass, before anything else could cancel the request.
static void fsm_step(void) {
    if (s_req_on && s_state == PS_STANDBY) {
        s_state = s_on_faults ? PS_FAULT : PS_POWERING_ON;
        s_state_ms = 0u;
        s_latch = !s_on_faults;
        s_guard = "power state is not STANDBY or BENCH_RUNNING";
    }
    s_req_on = false;
}

// One pass of main()'s superloop, as far as an update goes: 50 ms.
static void superloop_pass(void) {
    fsm_step();
    fwu_service();
    s_clock_ms += 50u;
    s_state_ms += 50u;
}

// Run the superloop until the install reboots or a reset cuts it (true; which,
// in s_last_jump), or `passes` run out.
static bool service_until_reboot(unsigned passes) {
    s_last_jump = 0;
    if (setjmp(s_jmp) != 0) return true;
    for (unsigned i = 0; i < passes; i++) superloop_pass();
    return false;
}

// What main() does after any reboot, up to the superloop.
static int boot_as(uint32_t start, bool trial) {
    fwu_test_reboot();
    run_from(start, trial);
    s_state = PS_STANDBY;
    s_state_ms = 0u;
    s_latch = false;
    s_guard = NULL;
    s_req_on = false;
    s_nops = 0u;
    int j = setjmp(s_jmp);
    if (j == 0) fwu_boot_check();
    return j;       // 2: it rebooted normally (a trial that did not buy)
}

static fwu_status_t status(void) {
    fwu_status_t s;
    fwu_status(&s);
    return s;
}

// A whole update up to the install, from partition A into B.
static bool stage_and_apply(uint32_t n, uint16_t minor) {
    make_image(s_img, n, TYPE_ARM_S_TBYB, 1, minor, 0x110);
    uint32_t v = 0;
    const unsigned reqs = s_shutdown_reqs;
    CHECK(stage(s_img, n, &v) == FWU_OK);
    CHECK(v == FWU_VERSION(1, minor));
    CHECK(fwu_apply() == FWU_OK);
    CHECK(s_shutdown_reqs == reqs + 1u);
    CHECK(status().state == FWU_ARMED);
    return true;
}

static bool stage_this_and_apply(uint32_t n, uint32_t want_version) {
    uint32_t v = 0;
    CHECK(stage(s_img, n, &v) == FWU_OK && v == want_version);
    CHECK(fwu_apply() == FWU_OK);
    return true;
}

static void passes(unsigned n) {
    for (unsigned i = 0; i < n; i++) superloop_pass();
}

// Nothing at all in B the ROM's scanner could start from: no valid first block.
static bool no_image_in_b(void) {
    return !rom_scan_partition(&s_flash[PART_B], PART_SIZE).found;
}

// ---------------------------------------------------------------------------
// The image parser
// ---------------------------------------------------------------------------

static bool parse_image(void) {
    fwu_image_t img;
    make_image(s_img, 8192, TYPE_ARM_S_TBYB, 1, 7, 0x110);
    CHECK(fwu_parse_image(s_img, 8192, &img));
    CHECK(img.found && img.exe_arm_s_rp2350 && img.tbyb && img.has_version);
    CHECK(img.version == FWU_VERSION(1, 7));

    make_image(s_img, 8192, TYPE_ARM_S, 2, 0, 0x110);
    CHECK(fwu_parse_image(s_img, 8192, &img) && !img.tbyb && img.exe_arm_s_rp2350);
    CHECK(img.version == FWU_VERSION(2, 0));

    // RISC-V, RP2040, Non-secure and a data image are not ours.
    static const uint16_t other[] = { 0x1121u | 0x8000u, 0x0021u, 0x1011u, 0x1002u };
    for (size_t k = 0; k < sizeof(other) / sizeof(other[0]); k++) {
        make_image(s_img, 8192, other[k], 1, 1, 0x110);
        CHECK(fwu_parse_image(s_img, 8192, &img) && !img.exe_arm_s_rp2350);
    }

    // Only the first 4 KB count.
    make_image(s_img, 8192, TYPE_ARM_S_TBYB, 1, 1, 4096);
    CHECK(!fwu_parse_image(s_img, 8192, &img));
    make_image(s_img, 8192, TYPE_ARM_S_TBYB, 1, 1, 4096 - 24);     // runs past 4 KB
    CHECK(!fwu_parse_image(s_img, 8192, &img));

    // Malformed: LAST's size wrong, end marker missing, a zero-size item.
    make_image(s_img, 8192, TYPE_ARM_S_TBYB, 1, 1, 0x110);
    s_img[0x110 + 17] = 4;
    CHECK(!fwu_parse_image(s_img, 8192, &img));
    make_image(s_img, 8192, TYPE_ARM_S_TBYB, 1, 1, 0x110);
    s_img[0x110 + 24] ^= 1u;
    CHECK(!fwu_parse_image(s_img, 8192, &img));
    make_image(s_img, 8192, TYPE_ARM_S_TBYB, 1, 1, 0x110);
    s_img[0x110 + 9] = 0;
    CHECK(!fwu_parse_image(s_img, 8192, &img));

    // A block without IMAGE_TYPE before the real one is skipped.
    make_image(s_img, 8192, TYPE_ARM_S_TBYB, 1, 3, 0x200);
    put32le(&s_img[0x110], 0xffffded3u);
    s_img[0x114] = 0xfe; s_img[0x115] = 1; s_img[0x116] = 0; s_img[0x117] = 0;   // IGNORED, 1 word
    s_img[0x118] = 0xff; s_img[0x119] = 1; s_img[0x11a] = 0; s_img[0x11b] = 0;
    put32le(&s_img[0x11c], 0u);
    put32le(&s_img[0x120], 0xab123579u);
    CHECK(fwu_parse_image(s_img, 8192, &img) && img.version == FWU_VERSION(1, 3));

    // Truncated buffers.
    make_image(s_img, 8192, TYPE_ARM_S_TBYB, 1, 1, 0x110);
    CHECK(!fwu_parse_image(s_img, 0x110 + 20, &img));
    CHECK(fwu_parse_image(s_img, 0x110 + 28, &img));
    CHECK(!fwu_parse_image(NULL, 100, &img));
    return true;
}

// ---------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------

static bool begin_checks(void) {
    uint8_t d[32] = { 0 };
    CHECK(fwu_begin(0u, d) == FWU_E_SIZE);
    CHECK(fwu_begin(FWU_STAGE_BYTES + 1u, d) == FWU_E_SIZE);
    s_lay.other_size = 64u * 1024u;
    CHECK(fwu_begin(64u * 1024u + 1u, d) == FWU_E_SIZE);
    CHECK(fwu_begin(64u * 1024u, d) == FWU_OK);
    fwu_abort();
    run_from(PART_A, false);

    s_lay.partitioned = false;
    CHECK(fwu_begin(1000u, d) == FWU_E_NO_SLOT);
    run_from(PART_A, false);
    s_lay.other_size = CFG_START - PART_B + FWU_SECTOR;        // reaches the config sectors
    CHECK(fwu_begin(1000u, d) == FWU_E_NO_SLOT);
    run_from(PART_A, false);
    s_lay.other_start = PART_A + PART_SIZE - FWU_SECTOR;        // overlaps this image
    CHECK(fwu_begin(1000u, d) == FWU_E_NO_SLOT);
    run_from(PART_A, false);
    s_lay.other_start = PART_B + 1u;                             // unaligned
    CHECK(fwu_begin(1000u, d) == FWU_E_NO_SLOT);
    run_from(PART_A, false);

    CHECK(fwu_begin(FWU_STAGE_BYTES, d) == FWU_OK);
    CHECK(status().state == FWU_RECEIVING);
    CHECK(fwu_scratch(NULL) == NULL);                          // no longer free for the ROM
    return true;
}

static bool data_sequence(void) {
    make_image(s_img, 1000, TYPE_ARM_S_TBYB, 1, 1, 0x110);
    uint8_t d[32];
    digest(s_img, 1000, d);
    uint32_t next = 99;
    CHECK(fwu_data(0, s_img, 10, &next) == FWU_E_STATE);        // before BEGIN
    CHECK(fwu_begin(1000, d) == FWU_OK);
    CHECK(fwu_data(0, s_img, 0, &next) == FWU_E_LENGTH);
    CHECK(fwu_data(0, s_img, 400, &next) == FWU_OK && next == 400u);
    // The same chunk again (its answer was lost): accepted, not appended twice.
    CHECK(fwu_data(0, s_img, 400, &next) == FWU_OK && next == 400u);
    CHECK(status().received == 400u);
    // The same offset and length with other bytes: the Pi has lost track.
    uint8_t other[400];
    memcpy(other, s_img, 400);
    other[5] ^= 1u;
    CHECK(fwu_data(0, other, 400, &next) == FWU_E_OFFSET && next == 400u);
    // A gap, and an overlap that is not the last chunk: refused, with the offset wanted.
    CHECK(fwu_data(500, &s_img[500], 100, &next) == FWU_E_OFFSET && next == 400u);
    CHECK(fwu_data(300, &s_img[300], 200, &next) == FWU_E_OFFSET && next == 400u);
    // Past the end.
    CHECK(fwu_data(400, &s_img[400], 601, &next) == FWU_E_LENGTH);
    CHECK(fwu_data(1001, s_img, 1, &next) == FWU_E_LENGTH);
    CHECK(fwu_data(400, &s_img[400], 600, &next) == FWU_OK && next == 1000u);
    uint32_t v = 0;
    CHECK(fwu_end(&v) == FWU_OK && v == FWU_VERSION(1, 1));
    CHECK(fwu_data(400, &s_img[400], 600, &next) == FWU_E_STATE);
    return true;
}

static bool end_checks(void) {
    uint32_t v = 0;
    CHECK(fwu_end(&v) == FWU_E_STATE);
    make_image(s_img, 5000, TYPE_ARM_S_TBYB, 1, 4, 0x110);
    uint8_t d[32];
    digest(s_img, 5000, d);
    CHECK(fwu_begin(5000, d) == FWU_OK);
    uint32_t next;
    CHECK(fwu_data(0, s_img, 4999, &next) == FWU_OK);
    CHECK(fwu_end(&v) == FWU_E_INCOMPLETE);
    CHECK(status().state == FWU_RECEIVING);
    CHECK(fwu_data(4999, &s_img[4999], 1, &next) == FWU_OK);
    CHECK(fwu_end(&v) == FWU_OK && v == FWU_VERSION(1, 4));
    CHECK(status().state == FWU_VERIFIED);
    v = 0;
    CHECK(fwu_end(&v) == FWU_OK && v == FWU_VERSION(1, 4));     // a retry

    // A wrong digest: dropped.
    d[0] ^= 1u;
    CHECK(fwu_begin(5000, d) == FWU_OK);
    CHECK(fwu_data(0, s_img, 5000, &next) == FWU_OK);
    CHECK(fwu_end(&v) == FWU_E_DIGEST);
    CHECK(status().state == FWU_IDLE && status().last_error == FWU_E_DIGEST);

    // Not an RP2350 Arm executable, and not try-before-you-buy.
    make_image(s_img, 5000, 0x1121u | 0x8000u, 1, 4, 0x110);
    CHECK(stage(s_img, 5000, &v) == FWU_E_IMAGE && status().state == FWU_IDLE);
    memset(s_img, 0x00, 5000);
    CHECK(stage(s_img, 5000, &v) == FWU_E_IMAGE);
    make_image(s_img, 5000, TYPE_ARM_S, 1, 4, 0x110);
    CHECK(stage(s_img, 5000, &v) == FWU_E_NOT_TBYB && status().state == FWU_IDLE);
    return true;
}

// ---------------------------------------------------------------------------
// Applying and installing
// ---------------------------------------------------------------------------

static bool apply_needs_running(void) {
    CHECK(fwu_apply() == FWU_E_STATE);
    make_image(s_img, 9000, TYPE_ARM_S_TBYB, 1, 2, 0x110);
    uint32_t v;
    CHECK(stage(s_img, 9000, &v) == FWU_OK);
    static const pstate_t not_running[] = { PS_STANDBY, PS_POWERING_ON, PS_PI_BOOTING,
                                            PS_BENCH_RUNNING, PS_SHUTTING_DOWN, PS_FAULT };
    for (size_t k = 0; k < sizeof(not_running) / sizeof(not_running[0]); k++) {
        s_state = not_running[k];
        CHECK(fwu_apply() == FWU_E_POWER);
        CHECK(status().state == FWU_VERIFIED);
    }
    CHECK(s_shutdown_reqs == 0u);
    CHECK(!status().repower_pending);                           // nothing owed yet
    s_state = PS_RUNNING;
    CHECK(fwu_apply() == FWU_OK && s_shutdown_reqs == 1u);
    CHECK(fwu_apply() == FWU_OK && s_shutdown_reqs == 1u);       // a retry: no second request
    CHECK(status().state == FWU_ARMED);
    // From UPD_APPLY on, the Pi is owed its power back (R3-F2).
    CHECK(status().repower_pending);
    return true;
}

// Nothing is written until STANDBY, the latch read back open, the config guard
// clear and FWU_INSTALL_QUIET_MS in STANDBY. (The fake flash also refuses.)
static bool install_waits_for_quiet(void) {
    CHECK(stage_and_apply(9000, 2));
    s_state = PS_SHUTTING_DOWN;
    for (unsigned i = 0; i < 20; i++) fwu_service();
    s_state = PS_STANDBY; s_state_ms = 5000; s_latch = true; s_guard = NULL;
    for (unsigned i = 0; i < 20; i++) fwu_service();
    s_latch = false; s_guard = "the beam is on";
    for (unsigned i = 0; i < 20; i++) fwu_service();
    s_guard = NULL; s_state_ms = FWU_INSTALL_QUIET_MS - 1u;
    for (unsigned i = 0; i < 20; i++) fwu_service();
    CHECK(s_nops == 0u);
    CHECK(status().state == FWU_ARMED);
    // STANDBY on its own terms: every other state is refused even with the latch
    // read open, the guard clear and the time served.
    s_state_ms = 100000u;
    for (int st = 0; st < (int)PS__COUNT; st++) {
        if ((pstate_t)st == PS_STANDBY) continue;
        s_state = (pstate_t)st;
        for (unsigned i = 0; i < 5; i++) fwu_service();
        CHECK(status().state == FWU_ARMED);
    }
    s_state = PS_STANDBY;
    CHECK(s_nops == 0u);
    s_state_ms = FWU_INSTALL_QUIET_MS;
    fwu_service();
    CHECK(status().state == FWU_INSTALLING && s_nops == 0u);     // starts next pass
    CHECK(s_on_reqs == 0u);                                      // never asked during the wait
    return true;
}

// Sector 0 is erased first and programmed last; each other sector is erased,
// programmed and read back; the note is written; the reboot names partition B.
static bool install_order(void) {
    const uint32_t n = 3u * FWU_SECTOR + 100u;                    // 4 sectors
    CHECK(stage_and_apply(n, 2));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(50));
    CHECK(s_reboot_start == PART_B && s_last_jump == 1);
    static const flash_op_t want[] = {
        { 'E', PART_B },
        { 'E', PART_B + 0x1000u }, { 'P', PART_B + 0x1000u },
        { 'E', PART_B + 0x2000u }, { 'P', PART_B + 0x2000u },
        { 'E', PART_B + 0x3000u }, { 'P', PART_B + 0x3000u },
        { 'P', PART_B },
    };
    CHECK(s_nops == sizeof(want) / sizeof(want[0]));
    for (unsigned i = 0; i < s_nops; i++) CHECK(s_ops[i].op == want[i].op && s_ops[i].offs == want[i].offs);
    // What landed is the image, then erased flash to the end of the last sector.
    CHECK(memcmp(&s_flash[PART_B], s_img, n) == 0);
    for (uint32_t i = n; i < 4u * FWU_SECTOR; i++) CHECK(s_flash[PART_B + i] == 0xFFu);
    // Nothing outside B was touched.
    for (uint32_t i = 0; i < PART_B; i++) CHECK(s_flash[i] == 0xFFu);
    for (uint32_t i = PART_B + 4u * FWU_SECTOR; i < FLASH_BYTES; i++) CHECK(s_flash[i] == 0xFFu);
    CHECK(s_on_reqs == 0u);                                      // never asked during the install
    return true;
}

// Until the last step, partition B holds no image the boot ROM could launch --
// even if a valid older image was there before.
static bool no_bootable_image_until_last_step(void) {
    // An older, bought image already in B.
    make_image(s_img, 2u * FWU_SECTOR, TYPE_ARM_S, 1, 0, 0x110);
    memcpy(&s_flash[PART_B], s_img, 2u * FWU_SECTOR);
    const uint32_t n = 5u * FWU_SECTOR;
    CHECK(stage_and_apply(n, 3));
    go_quiet(FWU_INSTALL_QUIET_MS);
    fwu_service();                                               // ARMED -> INSTALLING
    for (unsigned step = 0; step < 5u; step++) {                 // erase 0, then sectors 1..4
        fwu_service();
        CHECK(no_image_in_b());
    }
    CHECK(service_until_reboot(5));
    const rom_view_t v = rom_scan_partition(&s_flash[PART_B], PART_SIZE);
    CHECK(v.found && v.trial_only && !v.launchable);              // 1.3, TBYB-flagged
    return true;
}

static bool install_verify_fails(void) {
    CHECK(stage_and_apply(4u * FWU_SECTOR, 2));
    go_quiet(FWU_INSTALL_QUIET_MS);
    s_stuck_sector = (int)((PART_B + 2u * FWU_SECTOR) / FWU_SECTOR);
    CHECK(!service_until_reboot(50));
    fwu_status_t s = status();
    CHECK(s.state == FWU_VERIFIED && s.last_error == FWU_E_FLASH);
    CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_FLASH);
    CHECK(no_image_in_b());
    // Sector 0 was erased already; it is cleared all the same, as after any
    // failed step (R3-F1): zeros, not the erased 0xFF.
    for (uint32_t i = 0; i < FWU_SECTOR; i++) CHECK(s_flash[PART_B + i] == 0x00u);
    // The board shut the Pi down for this: it powered it again, once, from the
    // same superloop (already a second into STANDBY).
    CHECK(s_on_reqs == 1u && !s.repower_pending);
    return true;
}

// Sector 0, written last, is read back too: a bad one means no reboot, and the
// sector cleared again.
static bool install_verify_fails_sector0(void) {
    CHECK(stage_and_apply(4u * FWU_SECTOR, 2));
    go_quiet(FWU_INSTALL_QUIET_MS);
    s_stuck_sector = (int)(PART_B / FWU_SECTOR);
    CHECK(!service_until_reboot(50));
    CHECK(status().state == FWU_VERIFIED && status().last_error == FWU_E_FLASH);
    CHECK(s_reboot_start == 0u);                                 // never rebooted
    CHECK(no_image_in_b());
    return true;
}

// Someone powers the board up mid-install: it stops, and nothing installs later
// behind anyone's back.
static bool install_interrupted(void) {
    CHECK(stage_and_apply(6u * FWU_SECTOR, 2));
    go_quiet(FWU_INSTALL_QUIET_MS);
    fwu_service();
    fwu_service();
    fwu_service();
    const unsigned ops = s_nops;
    CHECK(ops > 0u);
    s_state = PS_POWERING_ON;
    s_latch = true;
    fwu_service();
    fwu_status_t s = status();
    CHECK(s.state == FWU_VERIFIED && s.last_error == FWU_E_INTERRUPTED);
    CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_INTERRUPTED);
    go_quiet(100000u);
    for (unsigned i = 0; i < 50; i++) fwu_service();
    CHECK(s_nops == ops);
    CHECK(no_image_in_b());
    // Whoever powered the board met the obligation: nothing more is asked.
    CHECK(!status().repower_pending && s_on_reqs == 0u);
    return true;
}

// The board never reached STANDBY: the Pi never went down, so nothing is owed.
static bool arm_timeout(void) {
    CHECK(stage_and_apply(9000, 2));
    s_state = PS_SHUTTING_DOWN;
    s_clock_ms += FWU_ARM_TIMEOUT_MS;
    fwu_service();
    CHECK(status().state == FWU_ARMED);
    s_clock_ms += 1u;
    fwu_service();
    fwu_status_t s = status();
    CHECK(s.state == FWU_VERIFIED && s.last_error == FWU_E_ARM_TIMEOUT);
    CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_ARM_TIMEOUT);
    CHECK(!s.repower_pending);
    go_quiet(100000u);
    for (unsigned i = 0; i < 20; i++) fwu_service();
    CHECK(s_nops == 0u && s_on_reqs == 0u);
    return true;
}

// ARMED in STANDBY with the gate shut until the timeout: the Pi did go down, so
// it is owed its power.
static bool arm_timeout_in_standby_repowers(void) {
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(5000u);
    s_guard = "the beam is on";
    passes(5);
    CHECK(status().state == FWU_ARMED && s_on_reqs == 0u);
    s_clock_ms += FWU_ARM_TIMEOUT_MS;
    passes(5);
    fwu_status_t s = status();
    CHECK(s.state == FWU_VERIFIED && s.last_error == FWU_E_ARM_TIMEOUT);
    CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_ARM_TIMEOUT);
    CHECK(s_on_reqs == 1u && !s.repower_pending && s_nops == 0u);
    return true;
}

static bool reboot_refused(void) {
    place_running(PART_A, 1);
    CHECK(stage_and_apply(2u * FWU_SECTOR, 2));
    go_quiet(FWU_INSTALL_QUIET_MS);
    s_reboot_rc = -4;
    CHECK(!service_until_reboot(20));
    CHECK(s_reboot_start == PART_B);
    CHECK(status().state == FWU_VERIFIED && status().last_error == FWU_E_REBOOT);
    CHECK(s_on_reqs == 1u);                                      // the Pi powered again
    // The complete image is cleared all the same (R3-F1).
    CHECK(no_image_in_b() && rom_pick_normal() == PART_A);
    CHECK(boot_as(PART_A, false) == 0);
    fwu_status_t s = status();
    CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_REBOOT);
    CHECK(!s.result_this_boot && !s.repower_pending);
    return true;
}

static bool abort_anytime(void) {
    CHECK(stage_and_apply(6u * FWU_SECTOR, 2));
    go_quiet(FWU_INSTALL_QUIET_MS);
    fwu_service();
    fwu_service();
    const unsigned ops = s_nops;
    CHECK(fwu_abort() == FWU_OK);
    fwu_status_t s = status();
    CHECK(s.state == FWU_IDLE);
    CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_INTERRUPTED);
    CHECK(!s.repower_pending);                                   // `upd abort` cancels the power-on
    for (unsigned i = 0; i < 50; i++) fwu_service();
    CHECK(s_nops == ops && s_on_reqs == 0u);
    uint8_t d[32] = { 0 };
    CHECK(fwu_begin(100, d) == FWU_OK);
    CHECK(fwu_abort() == FWU_OK && status().state == FWU_IDLE);
    CHECK(fwu_scratch(NULL) != NULL);
    return true;
}

static bool begin_refused_while_installing(void) {
    CHECK(stage_and_apply(6u * FWU_SECTOR, 2));
    go_quiet(FWU_INSTALL_QUIET_MS);
    fwu_service();
    uint8_t d[32] = { 0 };
    CHECK(fwu_begin(100, d) == FWU_E_STATE);
    CHECK(status().state == FWU_INSTALLING);
    return true;
}

// ---------------------------------------------------------------------------
// R3-F1: a failed install leaves nothing the boot ROM would launch
// ---------------------------------------------------------------------------

// The review's case: a 1.2 image over 1.1, every byte right but one bit of the
// last sector's IMAGE_TYPE stuck low -- TBYB cleared: a valid image a normal
// boot WOULD launch, with no trial. The read-back catches it, the zeros clear it,
// the boot ROM can only take A, nothing outside B changed, and the board says
// FLASH, not CLEANUP.
static bool final_header_tbyb_fault(void) {
    place_running(PART_A, 1);
    const uint32_t outside = outside_partner();
    const uint32_t n = 4u * FWU_SECTOR + 100u;
    CHECK(stage_and_apply(n, 2));
    CHECK(s_img[0x117] == 0x90u);                                 // IMAGE_TYPE 0x9021: TBYB
    s_flt_offs = PART_B + 0x117u;
    s_flt_mask = 0x80u;
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(!service_until_reboot(60));
    CHECK(s_flt_mask == 0u);                                      // the fault happened
    fwu_status_t s = status();
    CHECK(s.state == FWU_VERIFIED && s.last_error == FWU_E_FLASH);
    CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_FLASH);
    CHECK(no_image_in_b() && rom_pick_normal() == PART_A);
    for (uint32_t i = 0; i < FWU_SECTOR; i++) CHECK(s_flash[PART_B + i] == 0x00u);   // zeroed
    CHECK(memcmp(&s_flash[PART_B + FWU_SECTOR], &s_img[FWU_SECTOR], n - FWU_SECTOR) == 0);
    CHECK(outside_partner() == outside);
    CHECK(strstr(s_logs, "was cleared") != NULL && strstr(s_logs, "could NOT") == NULL);
    CHECK(s_on_reqs == 1u && s_reboot_start == 0u);
    // So it stays, across a reset.
    CHECK(boot_as(rom_pick_normal(), false) == 0);
    CHECK(status().result == FWU_RESULT_INSTALL_FAILED && status().result_detail == FWU_E_FLASH);
    return true;
}

// One stuck-low bit, a byte at a time through the block's metadata, then in
// sector 0's other bytes, then in the body sectors. Each is caught by a
// read-back, and each ends the same: nothing in B the boot ROM would launch,
// nothing outside B changed, FLASH.
static bool final_header_faults(void) {
    static const uint32_t at[] = {
        0x110u, 0x111u, 0x112u, 0x113u,               // start marker
        0x114u, 0x115u, 0x116u, 0x117u,               // IMAGE_TYPE: item header, flags
        0x118u, 0x119u, 0x11Cu, 0x11Eu,               // VERSION: item header, minor, major
        0x120u, 0x121u,                               // LAST
        0x128u, 0x129u, 0x12Au, 0x12Bu,               // end marker
        0x000u, 0x004u, 0x0FFu, 0x800u, 0xFFCu,       // the rest of sector 0
        0x1005u, 0x2777u, 0x4063u,                    // body sectors 1, 2 and 4 (the last)
    };
    const uint32_t n = 4u * FWU_SECTOR + 100u;
    for (size_t k = 0; k < sizeof(at) / sizeof(at[0]); k++) {
        reset_fixture();
        place_running(PART_A, 1);
        const uint32_t outside = outside_partner();
        CHECK(stage_and_apply(n, 2));
        // The lowest 1 bit there (a 0 cannot be lost); body bytes are random, so
        // the next byte that has one.
        uint32_t b = at[k];
        while (s_img[b] == 0u) b++;
        CHECK(b < n);
        s_flt_offs = PART_B + b;
        s_flt_mask = (uint8_t)(s_img[b] & (uint8_t)(0u - s_img[b]));
        go_quiet(FWU_INSTALL_QUIET_MS);
        CHECK(!service_until_reboot(60));
        CHECK(s_flt_mask == 0u);
        CHECK(status().last_error == FWU_E_FLASH && status().result == FWU_RESULT_INSTALL_FAILED);
        CHECK(no_image_in_b() && rom_pick_normal() == PART_A);
        CHECK(outside_partner() == outside && s_reboot_start == 0u);
    }
    return true;
}

// The last write stops partway and says so, after any number of pages -- all 16
// included (written, yet reported failed). Cleared every time.
static bool final_header_partial_program(void) {
    for (unsigned pages = 0; pages <= FWU_SECTOR / FWU_PAGE; pages++) {
        reset_fixture();
        place_running(PART_A, 1);
        CHECK(stage_and_apply(3u * FWU_SECTOR, 2));
        s_part_prog = 0;
        s_part_pages = pages;
        go_quiet(FWU_INSTALL_QUIET_MS);
        CHECK(!service_until_reboot(60));
        CHECK(status().last_error == FWU_E_FLASH);
        CHECK(no_image_in_b() && rom_pick_normal() == PART_A);
    }
    return true;
}

// A reset partway through the last write, after any number of bytes (each one
// across the block). Programming only clears bits and TBYB is a 1 in the image,
// as in erased flash, so no prefix is an image a normal boot launches; the next
// boot settles the update as cut short and still owes the Pi. Then any SUBSET of
// the write's bit changes, not only prefixes (cells half-programmed when the
// power went): the same.
static bool final_program_interrupted(void) {
    for (uint32_t k = 0; k <= FWU_SECTOR; k += (k >= 0x100u && k < 0x140u) ? 1u : 64u) {
        reset_fixture();
        place_running(PART_A, 1);
        CHECK(stage_and_apply(3u * FWU_SECTOR, 2));
        s_reset_prog = 0;
        s_reset_bytes = k;
        go_quiet(FWU_INSTALL_QUIET_MS);
        CHECK(service_until_reboot(60) && s_last_jump == 3);
        CHECK(rom_pick_normal() == PART_A);
        CHECK(boot_as(PART_A, false) == 0);
        fwu_status_t s = status();
        CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_INTERRUPTED);
        CHECK(s.repower_pending);
    }
    reset_fixture();
    place_running(PART_A, 1);
    make_image(s_img, FWU_SECTOR, TYPE_ARM_S_TBYB, 1, 2, 0x110);
    uint32_t r = 0x2545F491u;
    unsigned parsed = 0;
    for (unsigned t = 0; t < 1500u; t++) {
        // Which of the image's 0 bits did not make it: about half, down to none.
        const unsigned density = t % 6u;
        for (uint32_t i = 0; i < FWU_SECTOR; i++) {
            uint8_t keep = 0xFFu;
            for (unsigned d = 0; d <= density; d++) {
                r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                keep &= (uint8_t)r;
            }
            s_flash[PART_B + i] = (uint8_t)(s_img[i] | (density == 5u ? 0u : keep));
        }
        fwu_image_t img;
        if (fwu_parse_image(&s_flash[PART_B], FWU_SECTOR, &img)) parsed++;
        CHECK(rom_pick_normal() == PART_A);
    }
    CHECK(parsed >= 250u);           // well-formed blocks were among them: all TBYB-flagged
    return true;
}

// The zeros do not take (programs that "succeed" and write nothing); the erase
// does. Cleared, verified so: FLASH. In that order.
static bool cleanup_by_erase(void) {
    place_running(PART_A, 1);
    CHECK(stage_and_apply(3u * FWU_SECTOR, 2));
    s_flt_offs = PART_B + 0x117u;
    s_flt_mask = 0x80u;
    s_dead_progs_from = 1;
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(!service_until_reboot(60));
    CHECK(status().last_error == FWU_E_FLASH);
    for (uint32_t i = 0; i < FWU_SECTOR; i++) CHECK(s_flash[PART_B + i] == 0xFFu);
    CHECK(rom_pick_normal() == PART_A);
    // E 0, E/P 1, E/P 2, the last write, 16 pages of zeros, the erase.
    CHECK(s_nops == 1u + 4u + 1u + 16u + 1u);
    CHECK(s_ops[5].op == 'P' && s_ops[5].offs == PART_B);
    for (unsigned p = 0; p < 16u; p++) CHECK(s_ops[6u + p].op == 'P' && s_ops[6u + p].offs == PART_B + p * FWU_PAGE);
    CHECK(s_ops[22].op == 'E' && s_ops[22].offs == PART_B);
    return true;
}

// Nothing clears it: the programs and the erase report failure. The board says
// so -- FWU_E_CLEANUP, never "this image stays" -- still powers the Pi, so it can
// be told, and keeps the image staged. What the warning is about: a reset now
// launches the new image with no trial, and that boot says exactly that.
static bool cleanup_fails(void) {
    place_running(PART_A, 0);                                    // 1.0: B's 1.1 would win
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    s_flt_offs = PART_B + 0x117u;
    s_flt_mask = 0x80u;
    s_dead_progs_from = 1;
    s_dead_progs_fail = true;
    s_dead_erases_from = 1;
    s_dead_erases_fail = true;
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(!service_until_reboot(60));
    fwu_status_t s = status();
    CHECK(s.state == FWU_VERIFIED && s.last_error == FWU_E_CLEANUP);
    CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_CLEANUP);
    CHECK(strstr(s_logs, "could NOT be cleared") != NULL && strstr(s_logs, "stays the one") == NULL);
    CHECK(strstr(fwu_detail_name(s.result, s.result_detail), "reflash") != NULL);
    CHECK(s_on_reqs == 1u);
    CHECK(rom_pick_normal() == PART_B);
    CHECK(boot_as(PART_B, false) == 0);
    s = status();
    CHECK(s.result == FWU_RESULT_INSTALLED && s.result_detail == FWU_INSTALLED_NO_TRIAL);
    CHECK(s.result_this_boot && !s.trial);
    return true;
}

// The one window fw_update.h admits: a faulty last write (TBYB cleared) AND a
// reset before the zeros reach the block, in page 1. A normal boot then launches
// the new image without a trial, and the board says exactly that. One page
// later, the block is zeros and A runs.
static bool two_faults_reported_honestly(void) {
    for (int prog = 2; prog <= 3; prog++) {
        reset_fixture();
        place_running(PART_A, 0);
        CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
        s_flt_offs = PART_B + 0x117u;
        s_flt_mask = 0x80u;
        s_reset_prog = prog;                                     // 0: the last write; 1, 2...: the zeros
        s_reset_bytes = 0u;
        go_quiet(FWU_INSTALL_QUIET_MS);
        CHECK(service_until_reboot(60) && s_last_jump == 3);
        const uint32_t start = rom_pick_normal();
        CHECK(start == (prog == 2 ? PART_B : PART_A));
        CHECK(boot_as(start, false) == 0);
        fwu_status_t s = status();
        if (prog == 2) CHECK(s.result == FWU_RESULT_INSTALLED && s.result_detail == FWU_INSTALLED_NO_TRIAL);
        else           CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_INTERRUPTED);
        CHECK(s.repower_pending);
    }
    return true;
}

// Step 0's erase fails, over an older, bought image in B. The zeros take it out.
static bool first_erase_fails(void) {
    place_running(PART_A, 1);
    make_image(s_img, 2u * FWU_SECTOR, TYPE_ARM_S, 1, 0, 0x110);
    memcpy(&s_flash[PART_B], s_img, 2u * FWU_SECTOR);
    CHECK(rom_pick_normal() == PART_A);
    CHECK(stage_and_apply(3u * FWU_SECTOR, 2));
    s_dead_erases_from = 0;
    s_dead_erases_fail = true;
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(!service_until_reboot(60));
    CHECK(status().last_error == FWU_E_FLASH);
    CHECK(no_image_in_b() && rom_pick_normal() == PART_A);
    return true;
}

// The reboot refused, and nothing clears the complete image: CLEANUP. (It is
// TBYB-flagged, so a normal boot would not take it; the board does not lean on
// that.)
static bool reboot_refused_cleanup_fails(void) {
    place_running(PART_A, 1);
    CHECK(stage_and_apply(2u * FWU_SECTOR, 2));
    s_reboot_rc = -4;
    s_dead_progs_from = 1;
    s_dead_progs_fail = true;
    s_dead_erases_from = 1;
    s_dead_erases_fail = true;
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(!service_until_reboot(30));
    CHECK(status().last_error == FWU_E_CLEANUP && status().result_detail == FWU_E_CLEANUP);
    CHECK(rom_pick_normal() == PART_A);
    return true;
}

// ---------------------------------------------------------------------------
// R4-F1: "cleared" means no block start the boot ROM could find
// ---------------------------------------------------------------------------

// The oracle itself (rom_scan.c, the ROM's scanner), on layouts whose answer
// the scanner's source settles.
static bool rom_oracle_semantics(void) {
    rom_view_t v;
    fwu_image_t img;
    // Erased, or zeroed: no block, whatever the next sector holds.
    make_image(s_img, FWU_SECTOR, TYPE_ARM_S, 1, 2, 0x110);
    memcpy(&s_flash[PART_B + FWU_SECTOR], s_img, FWU_SECTOR);     // a valid block at 0x1110
    CHECK(!rom_scan_partition(&s_flash[PART_B], PART_SIZE).found);
    memset(&s_flash[PART_B], 0x00, FWU_SECTOR);
    CHECK(!rom_scan_partition(&s_flash[PART_B], PART_SIZE).found);
    // An SDK image: launchable only without TBYB.
    make_image(s_img, FWU_SECTOR, TYPE_ARM_S_TBYB, 1, 2, 0x110);
    memcpy(&s_flash[PART_B], s_img, FWU_SECTOR);
    v = rom_scan_partition(&s_flash[PART_B], PART_SIZE);
    CHECK(v.found && v.first == 0x110u && v.loop_closed && v.trial_only && !v.launchable);
    s_flash[PART_B + 0x117u] &= 0x7Fu;
    v = rom_scan_partition(&s_flash[PART_B], PART_SIZE);
    CHECK(v.launchable && v.version == FWU_VERSION(1, 2));
    // R4-F1: a block that STARTS in the first sector's last 16 bytes and ends in
    // the next one. The ROM finishes it; fwu_parse_image() wants it whole inside
    // the 4 KB and finds nothing.
    memset(&s_flash[PART_B], 0x00, 2u * FWU_SECTOR);
    put_block(&s_flash[PART_B], 0xff0u, TYPE_ARM_S, 1, 2, 0);
    v = rom_scan_partition(&s_flash[PART_B], PART_SIZE);
    CHECK(v.found && v.first == 0xff0u && v.launchable && v.version == FWU_VERSION(1, 2));
    CHECK(!fwu_parse_image(&s_flash[PART_B], FWU_SECTOR, &img));
    // A first block linking to an end block further on (the SDK's layout): one
    // closed loop of two.
    memset(&s_flash[PART_B], 0x00, 6u * FWU_SECTOR);
    put_block(&s_flash[PART_B], 0x110u, TYPE_ARM_S, 1, 2, (int32_t)(0x5000 - 0x110));
    put_end_block(&s_flash[PART_B], 0x5000u, -(int32_t)(0x5000 - 0x110));
    v = rom_scan_partition(&s_flash[PART_B], PART_SIZE);
    CHECK(v.found && v.first == 0x110u && v.blocks == 2 && v.loop_closed && v.launchable);
    // After an invalid candidate the ROM searches on through its buffer, past the
    // 4 KB: a start marker at 0xf00 whose first item runs off the buffer, then a
    // valid block at 0x1100 is the first block.
    memset(&s_flash[PART_B], 0x00, 2u * FWU_SECTOR);
    put32le(&s_flash[PART_B + 0xf00u], 0xffffded3u);
    put32le(&s_flash[PART_B + 0xf04u], 0x0000ff01u);              // an item of 255 words
    put_block(&s_flash[PART_B], 0x1100u, TYPE_ARM_S, 1, 2, 0);
    v = rom_scan_partition(&s_flash[PART_B], PART_SIZE);
    CHECK(v.found && v.first == 0x1100u && v.launchable);
    return true;
}

// The image for the boundary cases: the usual TBYB block at 0x110, and a second,
// self-contained TBYB block at 0xff0 whose last 12 bytes are in sector 1 (the
// review's fixture). The updater accepts it: its parser takes the first block.
static bool stage_boundary_image(void) {
    const uint32_t n = 3u * FWU_SECTOR;
    make_image(s_img, n, TYPE_ARM_S_TBYB, 1, 2, 0x110);
    put_block(s_img, 0xff0u, TYPE_ARM_S_TBYB, 1, 2, 0);
    CHECK(stage_this_and_apply(n, FWU_VERSION(1, 2)));
    // The last write clears the second block's TBYB bit; read-back catches it.
    s_flt_offs = PART_B + 0xff7u;
    s_flt_mask = 0x80u;
    return true;
}

// The review's case: the cleanup's last page of zeros (0xf00, program 16) does
// not take -- it reports failure, or reports success and writes nothing. Zeros
// everywhere else are not "cleared": the ROM would finish the block at 0xff0
// from sector 1. So the sector is erased and read back all 0xFF: FLASH, and the
// ROM can only take A.
static bool boundary_block_cleanup_erases(void) {
    for (int says_ok = 0; says_ok < 2; says_ok++) {
        reset_fixture();
        place_running(PART_A, 1);
        const uint32_t outside = outside_partner();
        CHECK(stage_boundary_image());
        s_skip_prog = 16;
        s_skip_prog_fail = says_ok == 0;
        go_quiet(FWU_INSTALL_QUIET_MS);
        CHECK(!service_until_reboot(60));
        CHECK(s_flt_mask == 0u);
        CHECK(status().last_error == FWU_E_FLASH && status().result_detail == FWU_E_FLASH);
        CHECK(s_s0_erases == 2u);                                 // step 0's, and the cleanup's
        for (uint32_t i = 0; i < FWU_SECTOR; i++) CHECK(s_flash[PART_B + i] == 0xFFu);
        CHECK(no_image_in_b() && rom_pick_normal() == PART_A);
        CHECK(outside_partner() == outside);
    }
    return true;
}

// The same, and the erase fails too: nothing proves the sector clear, so
// CLEANUP. No false alarm: the ROM's scanner finds the 1.2 block at 0xff0,
// without TBYB, and would launch it over A's 1.1. The parser r4 trusted saw
// nothing there.
static bool boundary_block_cleanup_fails(void) {
    place_running(PART_A, 1);
    CHECK(stage_boundary_image());
    s_skip_prog = 16;
    s_skip_prog_fail = true;
    s_dead_erases_from = 1;
    s_dead_erases_fail = true;
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(!service_until_reboot(60));
    fwu_status_t s = status();
    CHECK(s.last_error == FWU_E_CLEANUP);
    CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_CLEANUP);
    const rom_view_t v = rom_scan_partition(&s_flash[PART_B], PART_SIZE);
    CHECK(v.found && v.first == 0xff0u && v.launchable && v.version == FWU_VERSION(1, 2));
    CHECK(rom_pick_normal() == PART_B);
    fwu_image_t img;
    CHECK(!fwu_parse_image(&s_flash[PART_B], FWU_SECTOR, &img));
    CHECK(s_on_reqs == 1u);                                       // the Pi powered, to be told
    return true;
}

// A first block that links to an end block further on, as the SDK's images do
// (the 2026-10-20 build: 0x138 -> 0x35d28 -> 0x138), its TBYB bit cleared by the
// last write. Zeros over the first sector break the loop at its start: nothing
// found, FLASH. With the zeros failing on the block's page and the erase failing
// too: CLEANUP, and the scanner does find the non-TBYB loop.
static bool linked_block_cleared(void) {
    for (int broken = 0; broken < 2; broken++) {
        reset_fixture();
        place_running(PART_A, 1);
        const uint32_t n = 6u * FWU_SECTOR;
        make_image(s_img, n, TYPE_ARM_S_TBYB, 1, 2, 0x110);
        put_block(s_img, 0x110u, TYPE_ARM_S_TBYB, 1, 2, (int32_t)(0x5000 - 0x110));
        put_end_block(s_img, 0x5000u, -(int32_t)(0x5000 - 0x110));
        CHECK(stage_this_and_apply(n, FWU_VERSION(1, 2)));
        s_flt_offs = PART_B + 0x117u;
        s_flt_mask = 0x80u;
        if (broken) {
            s_skip_prog = 2;                                      // page 0x100, the block's
            s_skip_prog_fail = true;
            s_dead_erases_from = 1;
            s_dead_erases_fail = true;
        }
        go_quiet(FWU_INSTALL_QUIET_MS);
        CHECK(!service_until_reboot(80));
        const rom_view_t v = rom_scan_partition(&s_flash[PART_B], PART_SIZE);
        if (!broken) {
            CHECK(status().last_error == FWU_E_FLASH);
            for (uint32_t i = 0; i < FWU_SECTOR; i++) CHECK(s_flash[PART_B + i] == 0x00u);
            CHECK(!v.found && rom_pick_normal() == PART_A);
        } else {
            CHECK(status().last_error == FWU_E_CLEANUP);
            CHECK(v.found && v.first == 0x110u && v.blocks == 2 && v.loop_closed && v.launchable);
            CHECK(rom_pick_normal() == PART_B);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// The boots after an install
// ---------------------------------------------------------------------------

static bool install_then_buy(void) {
    // Version 1.1, as this build: the image in B is "this firmware".
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    // The boot ROM launches B on trial.
    CHECK(boot_as(PART_B, true) == 0);
    CHECK(s_buys == 1u && s_normal_reboots == 0u);
    fwu_status_t s = status();
    CHECK(s.trial && s.bought);
    CHECK(s.result == FWU_RESULT_INSTALLED && s.result_detail == FWU_INSTALLED_ON_TRIAL);
    CHECK(s.result_this_boot);
    CHECK(s.result_from == FWU_VERSION(1, 1) && s.result_to == FWU_VERSION(1, 1));
    CHECK(s.repower_pending);
    // The Pi goes back on once, from the superloop, after FWU_REPOWER_DELAY_MS.
    s_state_ms = FWU_REPOWER_DELAY_MS - 1u;
    fwu_service();
    CHECK(s_on_reqs == 0u);
    s_state_ms = FWU_REPOWER_DELAY_MS;
    fwu_service();
    fwu_service();                                               // accepted: not asked again
    CHECK(s_on_reqs == 1u && status().repower_pending);          // owed until the FSM acts
    superloop_pass();                                            // it does: POWERING_ON
    CHECK(s_on_reqs == 1u && !status().repower_pending);
    // A later warm reset: the result stays readable, nothing is repeated.
    CHECK(boot_as(PART_B, false) == 0);
    s = status();
    CHECK(s.result == FWU_RESULT_INSTALLED && !s.result_this_boot && !s.repower_pending);
    passes(60);
    CHECK(s_buys == 1u && s_on_reqs == 1u);
    // A power cycle: gone.
    fwu_test_power_on();
    CHECK(boot_as(PART_B, false) == 0);
    CHECK(status().result == FWU_RESULT_NONE);
    return true;
}

static bool trial_unhealthy_rolls_back(void) {
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    s_adc = ADC_5VIN_STALLED;
    CHECK(boot_as(PART_B, true) == 2);                           // rebooted normally
    CHECK(s_buys == 0u && s_normal_reboots == 1u);
    CHECK(s_on_reqs == 0u);
    // The ROM runs A again; it reports the rollback and why, and powers the Pi.
    s_adc = ADC_5VIN_FRESH;
    CHECK(boot_as(PART_A, false) == 0);
    fwu_status_t s = status();
    CHECK(s.result == FWU_RESULT_ROLLED_BACK && s.result_detail == FWU_TRIAL_ADC);
    CHECK(s.repower_pending);
    s_state_ms = FWU_REPOWER_DELAY_MS;
    fwu_service();
    CHECK(s_on_reqs == 1u);
    return true;
}

static bool trial_layout_or_buy_fails(void) {
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    s_overlaps = true;
    CHECK(boot_as(PART_B, true) == 2 && s_buys == 0u);
    s_overlaps = false;
    CHECK(boot_as(PART_A, false) == 0);
    CHECK(status().result == FWU_RESULT_ROLLED_BACK && status().result_detail == FWU_TRIAL_LAYOUT);

    // Then a second update from A, whose explicit buy fails.
    s_state = PS_RUNNING; s_latch = true; s_guard = "a Pi is present";
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    CHECK(s_reboot_start == PART_B);
    s_buy_rc = -12;
    CHECK(boot_as(PART_B, true) == 2 && s_buys == 1u);
    s_buy_rc = 0;
    CHECK(boot_as(PART_A, false) == 0);
    CHECK(status().result == FWU_RESULT_ROLLED_BACK && status().result_detail == FWU_TRIAL_BUY_FAILED);
    return true;
}

// The new image hung or crashed before buying: the ROM's trial watchdog fired and
// the next (normal) boot ran the old image. No trial result was recorded.
static bool hang_rolls_back(void) {
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    CHECK(boot_as(PART_A, false) == 0);
    fwu_status_t s = status();
    CHECK(s.result == FWU_RESULT_ROLLED_BACK && s.result_detail == FWU_TRIAL_OK);
    CHECK(s.result_to == FWU_VERSION(1, 1) && s.repower_pending);
    return true;
}

// The image in the target partition but not the version written there: not this
// update's image.
static bool wrong_version_is_rollback(void) {
    CHECK(stage_and_apply(3u * FWU_SECTOR, 9));                  // 1.9, this build is 1.1
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    CHECK(boot_as(PART_B, false) == 0);
    CHECK(status().result == FWU_RESULT_ROLLED_BACK);
    return true;
}

// A UF2 loaded over USB: a trial with no note. It buys; nothing else happens.
static bool usb_trial_no_note(void) {
    CHECK(boot_as(PART_A, true) == 0);
    CHECK(s_buys == 1u && status().bought);
    CHECK(status().result == FWU_RESULT_NONE && !status().repower_pending);
    s_state_ms = 100000u;
    fwu_service();
    CHECK(s_on_reqs == 0u);
    return true;
}

// The button got there first, in the first pass after the boot: the obligation
// is met, and nothing powers the Pi on later.
static bool repower_only_from_standby(void) {
    CHECK(stage_and_apply(2u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    CHECK(boot_as(PART_B, true) == 0);
    CHECK(status().repower_pending);
    s_state = PS_POWERING_ON;
    fwu_service();
    CHECK(!status().repower_pending);
    s_state = PS_STANDBY;
    s_state_ms = 100000u;
    fwu_service();
    CHECK(s_on_reqs == 0u);
    return true;
}

// The note is two copies (R3-F2). A word gone wrong in the older one changes
// nothing. In the newest one (a torn commit), the commit before it rules: the
// update is still reported and the Pi still owed. In both: nothing is reported,
// and nothing is powered on the strength of it. (Word 4 is the target version.)
static bool torn_note_rejected(void) {
    for (unsigned which = 0; which < 3u; which++) {
        reset_fixture();
        CHECK(stage_and_apply(2u * FWU_SECTOR, 1));             // commit: ARMED
        go_quiet(FWU_INSTALL_QUIET_MS);
        CHECK(service_until_reboot(30));                        // commit: TRYING, the newest
        if (which != 1u) fwu_test_note_flip(false, 4u, 0x00010000u);   // older first: see the hook
        if (which != 0u) fwu_test_note_flip(true, 4u, 0x00010000u);
        CHECK(boot_as(PART_A, false) == 0);
        fwu_status_t s = status();
        if (which == 0u)      CHECK(s.result == FWU_RESULT_ROLLED_BACK && s.repower_pending);
        else if (which == 1u) CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.repower_pending);
        else                  CHECK(s.result == FWU_RESULT_NONE && !s.repower_pending);
        passes(60);
        CHECK(s_on_reqs == (which == 2u ? 0u : 1u));
    }
    return true;
}

static bool noise_is_no_note(void) {
    fwu_test_power_on();
    CHECK(boot_as(PART_A, false) == 0);
    CHECK(status().result == FWU_RESULT_NONE && !status().repower_pending);
    passes(60);
    CHECK(s_on_reqs == 0u);
    return true;
}

// ---------------------------------------------------------------------------
// R3-F2: the Pi's power is owed until it is given back, across resets
// ---------------------------------------------------------------------------

// The review's case: the trial hangs, the ROM's watchdog runs A again, A settles
// ROLLED BACK -- and a second reset comes inside the delay, before the Pi was
// asked for. It is still owed: powered once. After that, resets power nothing.
static bool repower_after_reset_in_delay(void) {
    place_running(PART_A, 1);
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    CHECK(boot_as(PART_A, false) == 0);
    CHECK(status().result == FWU_RESULT_ROLLED_BACK && status().repower_pending);
    passes(10);                                                  // 0.5 s: inside the delay
    CHECK(s_on_reqs == 0u);
    CHECK(boot_as(PART_A, false) == 0);                          // the second reset
    fwu_status_t s = status();
    CHECK(s.result == FWU_RESULT_ROLLED_BACK && !s.result_this_boot && s.repower_pending);
    passes(60);
    CHECK(s_on_reqs == 1u && !status().repower_pending);
    for (unsigned k = 0; k < 3u; k++) {                          // later resets
        CHECK(boot_as(PART_A, false) == 0);
        passes(60);
    }
    CHECK(s_on_reqs == 1u && status().result == FWU_RESULT_ROLLED_BACK);
    return true;
}

// The same after an install that bought itself.
static bool repower_after_reset_in_delay_installed(void) {
    place_running(PART_A, 1);
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    CHECK(boot_as(PART_B, true) == 0);
    CHECK(status().result == FWU_RESULT_INSTALLED && status().repower_pending);
    passes(10);
    CHECK(s_on_reqs == 0u);
    CHECK(rom_pick_normal() == PART_B);                          // bought: A's first sector erased
    CHECK(boot_as(PART_B, false) == 0);
    fwu_status_t s = status();
    CHECK(s.result == FWU_RESULT_INSTALLED && s.result_detail == FWU_INSTALLED_ON_TRIAL);
    CHECK(!s.trial && s.repower_pending);
    passes(60);
    CHECK(s_on_reqs == 1u && !status().repower_pending);
    CHECK(boot_as(PART_B, false) == 0);
    passes(60);
    CHECK(s_on_reqs == 1u);
    return true;
}

// A reset after the buy, before the result is settled: the next boot is a normal
// one, and still reports a bought trial, not "launched without a trial".
static bool reset_before_settling_a_buy(void) {
    place_running(PART_A, 1);
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    fwu_test_store_hook = hook_reset;
    s_hook_calls = 0u;
    s_hook_reset_at = 1u;                                        // the settling commit's first store
    if (setjmp(s_hook_jmp) == 0) {
        (void)boot_as(PART_B, true);
        return fail_msg("the trial boot made no note commit");
    }
    fwu_test_store_hook = NULL;
    CHECK(s_buys == 1u && rom_pick_normal() == PART_B);
    CHECK(boot_as(PART_B, false) == 0);
    fwu_status_t s = status();
    CHECK(s.result == FWU_RESULT_INSTALLED && s.result_detail == FWU_INSTALLED_ON_TRIAL);
    CHECK(s.result_this_boot && s.repower_pending);
    passes(60);
    CHECK(s_on_reqs == 1u);
    return true;
}

// Around the request. Accepted, then a reset before the FSM acted: owed still,
// asked again. The FSM acted (POWERING_ON) but the note not yet updated, then a
// reset: the reset cut the Pi's power, so owed still. Met and recorded, then a
// reset: nothing.
static bool repower_around_acceptance(void) {
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    CHECK(boot_as(PART_A, false) == 0);
    s_state_ms = FWU_REPOWER_DELAY_MS;
    fwu_service();
    CHECK(s_on_reqs == 1u && s_req_on && status().repower_pending);
    CHECK(boot_as(PART_A, false) == 0);                          // before the FSM's step
    CHECK(!s_req_on && status().repower_pending);
    s_state_ms = FWU_REPOWER_DELAY_MS;
    fwu_service();
    CHECK(s_on_reqs == 2u);
    fsm_step();
    CHECK(s_state == PS_POWERING_ON && status().repower_pending);
    CHECK(boot_as(PART_A, false) == 0);                          // after it, before the note
    CHECK(status().repower_pending);
    passes(60);
    CHECK(s_on_reqs == 3u && !status().repower_pending);
    CHECK(boot_as(PART_A, false) == 0);                          // after the note
    passes(60);
    CHECK(s_on_reqs == 3u);
    return true;
}

// Refused requests are made again every pass, across resets, until one is
// accepted; then once.
static bool repower_refused_is_retried(void) {
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    CHECK(boot_as(PART_A, false) == 0);
    s_on_accepts = false;
    passes(40);                                                  // 2 s: refused from 1 s on
    CHECK(s_on_reqs == 20u && s_state == PS_STANDBY && status().repower_pending);
    CHECK(boot_as(PART_A, false) == 0);
    CHECK(status().repower_pending);
    passes(30);
    CHECK(s_on_reqs == 30u);
    s_on_accepts = true;
    passes(10);
    CHECK(s_on_reqs == 31u && s_state == PS_POWERING_ON && !status().repower_pending);
    return true;
}

// The USB guard refuses the power-on with a fault: the FSM left STANDBY, so the
// obligation is met (a policy: no retry into a fault). Clearing the fault
// powers nothing.
static bool repower_fault_ends_it(void) {
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    CHECK(boot_as(PART_A, false) == 0);
    s_on_faults = true;
    passes(30);
    CHECK(s_on_reqs == 1u && s_state == PS_FAULT && !status().repower_pending);
    go_quiet(0u);                                                // the fault acknowledged
    passes(60);
    CHECK(s_on_reqs == 1u);
    return true;
}

// `upd abort` cancels a pending power-on, in one commit, and it stays cancelled
// across resets; the result stays. During ARMED it also settles the update as
// cut short -- in the same single commit.
static bool abort_cancels_repower(void) {
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    CHECK(service_until_reboot(30));
    CHECK(boot_as(PART_A, false) == 0);
    CHECK(status().repower_pending);
    CHECK(fwu_abort() == FWU_OK && !status().repower_pending);
    CHECK(status().result == FWU_RESULT_ROLLED_BACK);
    passes(60);
    CHECK(boot_as(PART_A, false) == 0);
    passes(60);
    CHECK(s_on_reqs == 0u && status().result == FWU_RESULT_ROLLED_BACK);

    reset_fixture();
    fwu_test_store_hook = hook_reset;
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    const unsigned one_commit = s_hook_calls;
    s_hook_calls = 0u;
    CHECK(fwu_abort() == FWU_OK);
    CHECK(one_commit > 0u && s_hook_calls == one_commit);
    fwu_test_store_hook = NULL;
    fwu_status_t s = status();
    CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_INTERRUPTED);
    CHECK(!s.repower_pending);
    go_quiet(100000u);
    passes(60);
    CHECK(s_on_reqs == 0u && s_nops == 0u);
    return true;
}

// A reset while the board shuts the Pi down for an install cuts the Pi's power:
// the next boot settles the update as cut short and powers the Pi again.
static bool reset_during_shutdown_repowers(void) {
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    s_state = PS_SHUTTING_DOWN;
    passes(5);
    CHECK(boot_as(PART_A, false) == 0);
    fwu_status_t s = status();
    CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_INTERRUPTED);
    CHECK(s.result_this_boot && s.repower_pending);
    passes(60);
    CHECK(s_on_reqs == 1u && !status().repower_pending && s_nops == 0u);
    return true;
}

// The same in the middle of an install; and B holds nothing the ROM would launch.
static bool reset_during_install_repowers(void) {
    place_running(PART_A, 1);
    CHECK(stage_and_apply(5u * FWU_SECTOR, 2));
    go_quiet(FWU_INSTALL_QUIET_MS);
    passes(4);                                                   // INSTALLING; sector 0 erased
    CHECK(status().state == FWU_INSTALLING && s_on_reqs == 0u);
    CHECK(rom_pick_normal() == PART_A);
    CHECK(boot_as(PART_A, false) == 0);
    fwu_status_t s = status();
    CHECK(s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_INTERRUPTED);
    CHECK(s.repower_pending);
    passes(60);
    CHECK(s_on_reqs == 1u && !status().repower_pending);
    return true;
}

// A reset at every store of every note commit a scenario makes. The note is then
// as before the commit or as after it, never lost: where the Pi was owed its power
// either way, the next boot powers it exactly once; where it was owed neither
// way, never; and a later reset repeats nothing. Once UPD_APPLY's commit is made,
// no warm reset loses the update's result.
typedef enum { SC_HANG, SC_TRIAL_FAILS, SC_BOUGHT, SC_FLASH_FAILS, SC_ABORTED, SC__COUNT } scenario_t;

// Whether the Pi is owed its power after each commit, in order (none before).
static const char *const k_owed[SC__COUNT] = {
    [SC_HANG]        = "1110",    // ARMED, TRYING, ROLLED BACK, met
    [SC_TRIAL_FAILS] = "11110",   // ARMED, TRYING, TRIAL_FAILED, ROLLED BACK, met
    [SC_BOUGHT]      = "1110",    // ARMED, TRYING, INSTALLED, met
    [SC_FLASH_FAILS] = "110",     // ARMED, INSTALL FAILED, met
    [SC_ABORTED]     = "1110",    // ARMED, TRYING, ROLLED BACK, cancelled by `upd abort`
};

// From UPD_APPLY on A to the Pi powered again (or the power-on cancelled), then a
// later reset, as the board runs it.
static bool scenario(scenario_t sc) {
    place_running(PART_A, 1);
    if (sc == SC_FLASH_FAILS) s_stuck_sector = (int)((PART_B + FWU_SECTOR) / FWU_SECTOR);
    CHECK(stage_and_apply(3u * FWU_SECTOR, 1));
    go_quiet(FWU_INSTALL_QUIET_MS);
    if (sc == SC_FLASH_FAILS) {
        CHECK(!service_until_reboot(60));                        // fails, and powers the Pi
    } else {
        CHECK(service_until_reboot(60) && s_last_jump == 1);
        if (sc == SC_TRIAL_FAILS) s_adc = ADC_5VIN_STALLED;
        if (sc == SC_TRIAL_FAILS || sc == SC_BOUGHT)             // a hang never gets that far
            CHECK(boot_as(PART_B, true) == (sc == SC_BOUGHT ? 0 : 2));
        s_adc = ADC_5VIN_FRESH;
        if (sc != SC_BOUGHT) CHECK(boot_as(rom_pick_normal(), false) == 0);
        if (sc == SC_ABORTED) CHECK(fwu_abort() == FWU_OK);
        passes(60);
    }
    const unsigned want = sc == SC_ABORTED ? 0u : 1u;
    CHECK(s_on_reqs == want && !status().repower_pending);
    CHECK(boot_as(rom_pick_normal(), false) == 0);
    passes(60);
    CHECK(s_on_reqs == want && status().result != FWU_RESULT_NONE);
    return true;
}

static unsigned s_sweep_k;         // static: it must survive the longjmp

static bool sweep(scenario_t sc) {
    // How many stores one commit makes: UPD_APPLY makes one.
    reset_fixture();
    fwu_test_store_hook = hook_reset;
    CHECK(stage_and_apply(FWU_SECTOR, 1));
    const unsigned per = s_hook_calls;
    CHECK(per >= 2u);
    // The scenario whole: its commits as the table says.
    reset_fixture();
    fwu_test_store_hook = hook_reset;
    CHECK(scenario(sc));
    const unsigned commits = (unsigned)strlen(k_owed[sc]);
    CHECK(s_hook_calls == commits * per);
    for (s_sweep_k = 1u; s_sweep_k <= commits * per; s_sweep_k++) {
        reset_fixture();
        fwu_test_store_hook = hook_reset;
        s_hook_reset_at = s_sweep_k;
        if (setjmp(s_hook_jmp) == 0) {
            (void)scenario(sc);
            return fail_msg("the scenario ran past the store it should have been reset at");
        }
        fwu_test_store_hook = NULL;
        const unsigned c = (s_sweep_k - 1u) / per, pos = (s_sweep_k - 1u) % per;
        const bool before = c > 0u && k_owed[sc][c - 1u] == '1';
        const bool after = k_owed[sc][c] == '1';
        const bool owed = pos == per - 1u ? after : before;    // made only by its last store
        // The reset: requests and rails gone; the boot ROM picks the image.
        const uint32_t start = rom_pick_normal();
        CHECK(start != 0u);
        const unsigned reqs = s_on_reqs;
        CHECK(boot_as(start, false) == 0);
        passes(60);
        CHECK(s_on_reqs - reqs == (owed ? 1u : 0u));
        CHECK(!status().repower_pending);
        if (c > 0u || pos == per - 1u) CHECK(status().result != FWU_RESULT_NONE);
        CHECK(boot_as(rom_pick_normal(), false) == 0);
        passes(60);
        CHECK(s_on_reqs - reqs == (owed ? 1u : 0u));
    }
    return true;
}

static bool note_commit_resets(void) {
    for (int sc = 0; sc < (int)SC__COUNT; sc++) CHECK(sweep((scenario_t)sc));
    return true;
}

// ---------------------------------------------------------------------------
// The link's requests (link_proto.c)
// ---------------------------------------------------------------------------

static size_t req(uint8_t type, const uint8_t *p, size_t n, uint8_t *rsp) {
    return link_proto_handle(type, p, n, rsp, LINK_MAX_PAYLOAD);
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool proto_shapes(void) {
    uint8_t r[LINK_MAX_PAYLOAD], p[300] = { 0 };
    CHECK(req(0x7F, NULL, 0, r) == 1u && r[0] == LINK_E_UNKNOWN);
    CHECK(req(0x00, NULL, 0, r) == 1u && r[0] == LINK_E_UNKNOWN);
    // A frame with bit 7 set is never answered: no conversation with an echo.
    for (unsigned t = 0x80; t <= 0xFF; t++) CHECK(req((uint8_t)t, NULL, 0, r) == 0u);
    static const uint8_t no_payload[] = { LINK_PING, LINK_INFO, LINK_STATUS, LINK_HIST,
                                          LINK_UPD_END, LINK_UPD_APPLY, LINK_UPD_ABORT, LINK_UPD_STATUS };
    for (size_t k = 0; k < sizeof(no_payload); k++)
        CHECK(req(no_payload[k], p, 1, r) == 1u && r[0] == LINK_E_LENGTH);
    CHECK(req(LINK_UPD_BEGIN, p, 35, r) == 1u && r[0] == LINK_E_LENGTH);
    CHECK(req(LINK_UPD_BEGIN, p, 37, r) == 1u && r[0] == LINK_E_LENGTH);
    CHECK(req(LINK_UPD_DATA, p, 4, r) == 1u && r[0] == LINK_E_LENGTH);
    CHECK(req(LINK_UPD_DATA, p, 4 + LINK_DATA_MAX + 1, r) == 1u && r[0] == LINK_E_LENGTH);
    CHECK(link_proto_handle(LINK_PING, NULL, 0, r, 10) == 0u);       // too small to answer into
    CHECK(status().state == FWU_IDLE);                                // none of it did anything
    return true;
}

// Protocol 4 (image 1.16; 3 in the rejected 1.14 and 1.15). The CLI family -- CLI,
// CLI_ABORT, CLI_BEGIN -- is pi_link.c's, through link_cli.c's book
// (tests/link_test.c), and never reaches this function: if one ever did it is
// refused, so nothing can run or be cancelled by that path. CLI_OUT is never a
// request.
_Static_assert(LINK_PROTO_VERSION == 4u, "image 1.16 speaks link protocol 4");
// The link's statuses stay clear of every fwu_err_t, and of each other.
_Static_assert(LINK_E_MISMATCH >= FWU_E__COUNT, "a LINK_E_* collides with a fwu_err_t");
_Static_assert(LINK_E_MISMATCH < LINK_E_STALE && LINK_E_STALE < LINK_E_SESSION &&
               LINK_E_SESSION < LINK_E_PI_RULE && LINK_E_PI_RULE < LINK_E_LENGTH &&
               LINK_E_LENGTH < LINK_E_UNKNOWN, "the LINK_E_* are distinct");

static bool proto_cli_requests(void) {
    uint8_t r[LINK_MAX_PAYLOAD], p[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 'i', 'd', 0, 0 };
    CHECK(req(LINK_CLI, p, 10, r) == 1u && r[0] == LINK_E_UNKNOWN);
    CHECK(req(LINK_CLI_OUT, p, 10, r) == 1u && r[0] == LINK_E_UNKNOWN);
    CHECK(req(LINK_CLI_ABORT, p, 8, r) == 1u && r[0] == LINK_E_UNKNOWN);
    CHECK(req(LINK_CLI_ABORT, NULL, 0, r) == 1u && r[0] == LINK_E_UNKNOWN);
    CHECK(req(LINK_CLI_BEGIN, NULL, 0, r) == 1u && r[0] == LINK_E_UNKNOWN);
    CHECK(req((uint8_t)(LINK_CLI_ABORT | LINK_RESPONSE), NULL, 0, r) == 0u);   // an echo
    CHECK(status().state == FWU_IDLE);
    return true;
}

static bool proto_info_fields(void) {
    uint8_t r[LINK_MAX_PAYLOAD];
    size_t n = req(LINK_PING, NULL, 0, r);
    CHECK(n == 6u && r[0] == 0 && r[1] == LINK_PROTO_VERSION && le32(&r[2]) == PITRAC_FW_VERSION);
    n = req(LINK_INFO, NULL, 0, r);
    CHECK(n == 48u && r[0] == 0);
    CHECK(le32(&r[1]) == PITRAC_FW_VERSION);
    CHECK(memcmp(&r[5], "Oct 19 2026 12:34:56", 20) == 0);
    CHECK(r[25] == 0u && r[26] == 0u && r[27] == 1u);                  // normal boot, partition 0, partitioned
    CHECK(le32(&r[28]) == PART_A && le32(&r[32]) == PART_SIZE);
    CHECK(le32(&r[36]) == PART_B && le32(&r[40]) == PART_SIZE);
    CHECK(le32(&r[44]) == FWU_STAGE_BYTES);
    n = req(LINK_STATUS, NULL, 0, r);
    CHECK(n == 14u && r[0] == 0 && r[1] == (uint8_t)PS_RUNNING && r[2] == 0u);
    CHECK((r[3] | (r[4] << 8)) == 5123);
    CHECK(r[5] == (1u | 2u | 4u));
    s_adc = ADC_5VIN_NOT_SAMPLED;
    n = req(LINK_STATUS, NULL, 0, r);
    CHECK((r[3] | (r[4] << 8)) == 0xFFFF);
    n = req(LINK_HIST, NULL, 0, r);
    CHECK(n == 65u && r[0] == 0 && le32(&r[1]) == 0x1000u && le32(&r[61]) == 0x100Fu);
    // Unpartitioned: no partner reported.
    s_lay.partitioned = false;
    n = req(LINK_INFO, NULL, 0, r);
    CHECK(r[27] == 0u && le32(&r[36]) == 0u && le32(&r[40]) == 0u);
    return true;
}

// The whole update through the request handler, as the Pi drives it.
static bool proto_update_flow(void) {
    const uint32_t n = 2u * FWU_SECTOR + 77u;
    make_image(s_img, n, TYPE_ARM_S_TBYB, 1, 5, 0x110);
    uint8_t r[LINK_MAX_PAYLOAD], p[LINK_MAX_PAYLOAD];
    put32le(p, n);
    digest(s_img, n, &p[4]);
    CHECK(req(LINK_UPD_BEGIN, p, 36, r) == 5u && r[0] == FWU_OK && le32(&r[1]) == LINK_DATA_MAX);
    for (uint32_t at = 0; at < n; ) {
        uint32_t k = n - at < LINK_DATA_MAX ? n - at : LINK_DATA_MAX;
        put32le(p, at);
        memcpy(&p[4], &s_img[at], k);
        CHECK(req(LINK_UPD_DATA, p, 4u + k, r) == 5u && r[0] == FWU_OK);
        at = le32(&r[1]);
    }
    // A stray chunk: refused with the offset wanted.
    put32le(p, 3);
    CHECK(req(LINK_UPD_DATA, p, 10, r) == 5u && r[0] == FWU_E_OFFSET && le32(&r[1]) == n);
    CHECK(req(LINK_UPD_END, NULL, 0, r) == 5u && r[0] == FWU_OK && le32(&r[1]) == FWU_VERSION(1, 5));
    CHECK(req(LINK_UPD_STATUS, NULL, 0, r) == 25u && r[0] == FWU_OK);
    CHECK(r[1] == FWU_VERIFIED && r[2] == FWU_OK && le32(&r[3]) == n && le32(&r[7]) == n);
    CHECK(le32(&r[11]) == FWU_VERSION(1, 5));
    s_state = PS_BENCH_RUNNING;
    CHECK(req(LINK_UPD_APPLY, NULL, 0, r) == 1u && r[0] == FWU_E_POWER);
    s_state = PS_RUNNING;
    CHECK(req(LINK_UPD_APPLY, NULL, 0, r) == 1u && r[0] == FWU_OK && s_shutdown_reqs == 1u);
    CHECK(req(LINK_UPD_ABORT, NULL, 0, r) == 1u && r[0] == FWU_OK && status().state == FWU_IDLE);
    // The result and its detail, as the Pi reads them.
    CHECK(req(LINK_UPD_STATUS, NULL, 0, r) == 25u);
    CHECK(r[15] == FWU_RESULT_INSTALL_FAILED && r[24] == FWU_E_INTERRUPTED);
    CHECK(le32(&r[16]) == PITRAC_FW_VERSION && le32(&r[20]) == FWU_VERSION(1, 5));
    return true;
}

// ---------------------------------------------------------------------------

typedef bool (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } test_case;
#define TEST(name) { #name, name }

int main(int argc, char **argv) {
    static const test_case tests[] = {
        TEST(parse_image), TEST(begin_checks), TEST(data_sequence), TEST(end_checks),
        TEST(apply_needs_running), TEST(install_waits_for_quiet), TEST(install_order),
        TEST(no_bootable_image_until_last_step), TEST(install_verify_fails),
        TEST(install_verify_fails_sector0), TEST(torn_note_rejected),
        TEST(install_interrupted), TEST(arm_timeout), TEST(arm_timeout_in_standby_repowers),
        TEST(reboot_refused), TEST(abort_anytime), TEST(begin_refused_while_installing),
        TEST(final_header_tbyb_fault), TEST(final_header_faults), TEST(final_header_partial_program),
        TEST(final_program_interrupted), TEST(cleanup_by_erase), TEST(cleanup_fails),
        TEST(two_faults_reported_honestly), TEST(first_erase_fails), TEST(reboot_refused_cleanup_fails),
        TEST(rom_oracle_semantics), TEST(boundary_block_cleanup_erases),
        TEST(boundary_block_cleanup_fails), TEST(linked_block_cleared),
        TEST(install_then_buy), TEST(trial_unhealthy_rolls_back), TEST(trial_layout_or_buy_fails),
        TEST(hang_rolls_back), TEST(wrong_version_is_rollback), TEST(usb_trial_no_note),
        TEST(repower_only_from_standby), TEST(noise_is_no_note),
        TEST(repower_after_reset_in_delay), TEST(repower_after_reset_in_delay_installed),
        TEST(reset_before_settling_a_buy), TEST(repower_around_acceptance),
        TEST(repower_refused_is_retried), TEST(repower_fault_ends_it), TEST(abort_cancels_repower),
        TEST(reset_during_shutdown_repowers), TEST(reset_during_install_repowers),
        TEST(note_commit_resets),
        TEST(proto_shapes), TEST(proto_cli_requests), TEST(proto_info_fields), TEST(proto_update_flow),
    };
    if (argc != 2) {
        fprintf(stderr, "usage: %s <test>\n", argv[0]);
        return 2;
    }
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (strcmp(argv[1], tests[i].name) == 0) {
            test_name = tests[i].name;
            reset_fixture();
            return tests[i].fn() ? 0 : 1;
        }
    }
    fprintf(stderr, "unknown test: %s\n", argv[1]);
    return 2;
}

// The boot ROM's scanner, for rom_pick_normal() and the cleanup tests. Included,
// not linked, so a probe that includes this file needs no extra source.
#include "rom_scan.c"
