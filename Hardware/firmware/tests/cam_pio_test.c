// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// cam_pio_test.c -- runs the ASSEMBLED cam_handshake.pio, strobe_gate.pio and
// strobe_burst.pio (the words pioasm generates for the firmware build) in
// tests/pio_sim.c, and checks every cycle count cam_plan.h claims against them.
//
// The camera is modelled at the pads: each strobe rises a set number of system
// clocks after the trigger's pad rises (the 7a loopback is 0) and falls when the
// trigger falls. The PIO sees every input 2 clocks late (the synchroniser).

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PICO_NO_HARDWARE 1
#include "cam_handshake.pio.h"
#include "strobe_gate.pio.h"
#include "strobe_burst.pio.h"

#include "pio_sim.h"
#include "cam_plan.h"
#include "strobe_plan.h"
#include "board.h"

#ifndef count_of
#define count_of(a) (sizeof(a) / sizeof((a)[0]))
#endif

static const char *test_name;

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, test_name, #c); \
    return false; } } while (0)

#define SYS_HZ 150000000u
#define NEVER  UINT64_MAX

// Where the firmware loads them (camera.c, strobe.c): the camera program at the
// top of PIO1, the burst at the top of PIO0 with the gate immediately below it.
#define CAM_OFFSET    (32u - (unsigned)count_of_cam())
#define BURST_OFFSET  (32u - (unsigned)(sizeof strobe_burst_program_instructions / 2u))
#define GATE_OFFSET   (BURST_OFFSET - 1u)

static size_t count_of_cam(void) { return sizeof cam_handshake_program_instructions / 2u; }

// ---------------------------------------------------------------------------
// The rig: the camera state machine and a model camera.
// ---------------------------------------------------------------------------
typedef struct {
    uint64_t lat0, lat1;      // trigger pad rise -> strobe pad high, cycles (NEVER: no strobe)
    uint64_t s0_width;        // strobe 0 falls again this long after it rose (0: stays up)
    uint64_t s1_width;        // strobe 1 likewise
} cam_model_t;

typedef struct {
    pio_sim_t   cam;
    bool        irq_pio1[8];
    bool        irq_pio0[8];  // the strobe engine's block: the camera's `irq prev`
    cam_model_t model;
    bool        trig_prev;
    int64_t     rise_pad, fall_pad;
    uint32_t    words[8];
    uint64_t    word_cycle[8];
    unsigned    nwords;
    // Optional: the strobe engine on PIO0, sharing irq_pio0.
    bool        with_strobe;
    pio_sim_t   str;
    uint32_t    feed[2u * STROBE_BURST_MAX_PULSES + 1u];
    size_t      feed_n, feed_i;
    bool        pulse_prev;
    uint64_t    pulse_rise[STROBE_BURST_MAX_PULSES + 2u];
    uint64_t    pulse_fall[STROBE_BURST_MAX_PULSES + 2u];
    unsigned    n_rise, n_fall;
    bool        gate_left;    // the strobe SM's PC ever left the gate
} rig_t;

static uint32_t rig_pins(uint64_t cycle, void *ctx) {
    rig_t *r = (rig_t *)ctx;
    bool trig = ((r->cam.out_level >> 10) & 1u) != 0u;
    if (trig && !r->trig_prev) { r->rise_pad = (int64_t)cycle; r->fall_pad = -1; }
    if (!trig && r->trig_prev) r->fall_pad = (int64_t)cycle;
    r->trig_prev = trig;

    uint32_t v = trig ? 1u << 10 : 0u;
    if (r->rise_pad >= 0 && r->fall_pad < 0) {
        uint64_t t = cycle - (uint64_t)r->rise_pad;
        if (r->model.lat0 != NEVER && t >= r->model.lat0 &&
            !(r->model.s0_width && t >= r->model.lat0 + r->model.s0_width)) v |= 1u << 8;
        if (r->model.lat1 != NEVER && t >= r->model.lat1 &&
            !(r->model.s1_width && t >= r->model.lat1 + r->model.s1_width)) v |= 1u << 9;
    }
    return v;
}

static void rig_init(rig_t *r, cam_model_t m) {
    memset(r, 0, sizeof(*r));
    r->model = m;
    r->rise_pad = r->fall_pad = -1;

    pio_sim_t *s = &r->cam;
    pio_sim_init(s, r->irq_pio1);
    s->irq_prev = r->irq_pio0;
    pio_sim_load(s, cam_handshake_program_instructions, (unsigned)count_of_cam(), CAM_OFFSET);
    // As cam_handshake_program_init() configures it.
    s->wrap_bottom = CAM_OFFSET + cam_handshake_wrap_target;
    s->wrap_top    = CAM_OFFSET + cam_handshake_wrap;
    s->set_base = PIN_CAM_TRIGGER;  s->set_count = 1;
    s->in_base  = PIN_CAM_STROBE_0; s->in_count = 2;      // both strobes, one read
    s->in_shift_right = false;                            // `in null, 30` keeps the low bits
    s->out_shift_right = true;      s->pull_thresh = 32;
    s->clkdiv = 1;
    s->pins = rig_pins;
    s->pins_ctx = r;
    pio_sim_set_pc(s, CAM_OFFSET);
}

// The strobe engine as strobe.c sets up a camera-gated burst: PC on the gate.
static bool rig_add_strobe(rig_t *r, const strobe_burst_t *b, uint32_t phase) {
    r->with_strobe = true;
    pio_sim_t *s = &r->str;
    pio_sim_init(s, r->irq_pio0);
    pio_sim_load(s, strobe_burst_program_instructions,
                 (unsigned)(sizeof strobe_burst_program_instructions / 2u), BURST_OFFSET);
    pio_sim_load(s, strobe_gate_program_instructions, 1u, GATE_OFFSET);
    s->wrap_bottom = BURST_OFFSET + strobe_burst_wrap_target;
    s->wrap_top    = BURST_OFFSET + strobe_burst_wrap;
    s->set_base = PIN_STROBE_PULSE; s->set_count = 1;
    s->out_shift_right = false;
    s->clkdiv = SYS_HZ / 1000000u;  // strobe_burst_program_init(): 1 MHz
    s->clk_phase = phase;
    r->feed_n = strobe_encode(b, r->feed, count_of(r->feed));
    if (r->feed_n == 0u) return false;
    pio_sim_set_pc(s, GATE_OFFSET);
    s->enabled = true;
    return true;
}

static void rig_step(rig_t *r) {
    if (r->with_strobe) {
        pio_sim_t *s = &r->str;
        // The feed DMA keeps the TX FIFO topped up.
        while (r->feed_i < r->feed_n && pio_sim_tx_push(s, r->feed[r->feed_i])) r->feed_i++;
        pio_sim_step(s);   // first: a flag the camera sets this cycle is seen next cycle
        if (s->pc != GATE_OFFSET) r->gate_left = true;
        bool p = ((s->out_level >> PIN_STROBE_PULSE) & 1u) != 0u;
        if (p && !r->pulse_prev && r->n_rise < count_of(r->pulse_rise)) r->pulse_rise[r->n_rise++] = s->cycle;
        if (!p && r->pulse_prev && r->n_fall < count_of(r->pulse_fall)) r->pulse_fall[r->n_fall++] = s->cycle;
        r->pulse_prev = p;
    }
    pio_sim_step(&r->cam);
    uint32_t w;
    while (pio_sim_rx_pop(&r->cam, &w))
        if (r->nwords < count_of(r->words)) { r->word_cycle[r->nwords] = r->cam.cycle; r->words[r->nwords++] = w; }
}

// The cycle the `set pins, 1` ran: the pad shows it one clock later.
static int64_t set_cycle(const rig_t *r) { return r->rise_pad - 1; }
static int64_t release_cycle(const rig_t *r) { return r->fall_pad < 0 ? -1 : r->fall_pad - 1 - set_cycle(r); }
static int64_t fire_cycle(const rig_t *r) {
    return r->cam.irq_set_count[8u + 4u] ? (int64_t)r->cam.irq_set_cycle[8u + 4u] - set_cycle(r) : -1;
}

// One handshake: push the plan, run until the trigger drops (and a little after).
static void rig_run(rig_t *r, const cam_plan_t *p, uint64_t max_cycles) {
    pio_sim_tx_push(&r->cam, p->d);
    pio_sim_tx_push(&r->cam, p->t);
    pio_sim_tx_push(&r->cam, p->h);
    r->cam.enabled = true;
    uint64_t end = r->cam.cycle + max_cycles;
    while (r->cam.cycle < end) {
        rig_step(r);
        if (r->fall_pad >= 0 && r->cam.cycle > (uint64_t)r->fall_pad + 8u &&
            (!r->with_strobe || r->str.irq[0] || !r->gate_left)) break;
    }
}

static cam_plan_t plan_us(uint32_t first, uint32_t timeout, uint32_t hold) {
    cam_req_t q = { first, timeout, hold };
    cam_plan_t p;
    memset(&p, 0, sizeof p);
    if (cam_plan_make(&q, SYS_HZ, &p) != CAM_PLAN_OK) { fprintf(stderr, "bad plan\n"); exit(2); }
    return p;
}

// The first cycle at which the PIO sees BOTH strobes high: pad rise is set+1,
// the strobes follow by their latency, and the synchroniser adds 2.
static uint64_t seen_both(const cam_model_t *m) {
    uint64_t lat = m->lat0 > m->lat1 ? m->lat0 : m->lat1;
    return 1u + lat + PIO_SIM_SYNC_CYCLES;
}

// A complete fired handshake, checked end to end against cam_plan.
static bool check_fired(const rig_t *r, const cam_plan_t *p, cam_outcome_t want) {
    CHECK(!r->cam.error);
    CHECK(r->nwords == 2u);
    cam_decoded_t d = cam_decode(r->words[0], p);
    CHECK(d.outcome == want);
    CHECK(r->words[1] == CAM_WORD_END);
    CHECK(r->cam.irq_set_count[8u + 4u] == 1u);         // FIRE exactly once
    CHECK(fire_cycle(r) == (int64_t)d.fire_cycles);    // where cam_decode says
    uint64_t s = seen_both(&r->model);
    CHECK(d.detect_cycles >= s);                        // never before the strobes
    // Within one loop of the first poll that could see them. There are no polls
    // between the last early one (4D + 1) and the first late one (4D + 8/9), and
    // the late polls carry the +-1 cycle of the path that left the early phase.
    uint64_t first_late = CAM_PIO_LOOP_CYCLES * (uint64_t)p->d + CAM_PIO_LATE_POLL0_CYCLES;
    uint64_t can_see = (want == CAM_OUT_LATE && s < first_late) ? first_late : s;
    CHECK(d.detect_cycles <= can_see + CAM_PIO_LOOP_CYCLES);
    CHECK(release_cycle(r) == (int64_t)cam_release_after_fire_cycles(p, d.fire_cycles));
    if (want == CAM_OUT_EARLY) CHECK(d.fire_cycles == cam_fire_early_cycles(p));
    CHECK(d.delay_clamped == (want == CAM_OUT_LATE));
    // The .md's max(first delay, t_cam): FIRE never before either, never long after.
    uint32_t floor_c = cam_fire_early_cycles(p) > s ? cam_fire_early_cycles(p) : (uint32_t)s;
    CHECK(d.fire_cycles >= floor_c);
    CHECK(d.fire_cycles <= floor_c + 4u * CAM_PIO_LOOP_CYCLES);
    return true;
}

// ---------------------------------------------------------------------------

// The program the firmware ships: its size and wrap (compile-time, so a changed
// program fails the build of this test), and the RP2350-only encodings the
// simulator decodes (cross-checked against pioasm's own disassembly).
_Static_assert(cam_handshake_pio_version == 1, "the camera program needs RP2350 PIO (irq prev)");
_Static_assert(sizeof strobe_gate_program_instructions == 2u, "the gate is one instruction");
// The burst where it has always been (offset 20, the top of PIO0), the gate below.
_Static_assert(32u - sizeof strobe_burst_program_instructions / 2u == 20u, "burst offset");

static bool encodings(void) {
    // Read through volatiles: MSVC rejects a check on a compile-time constant (C4127).
    volatile int wrap_target = cam_handshake_wrap_target, wrap = cam_handshake_wrap;
    CHECK(count_of_cam() == 29u);
    CHECK(wrap_target == 0 && wrap == 26);
    CHECK(cam_handshake_program_instructions[3]  == 0xe001);  // set pins, 1
    CHECK(cam_handshake_program_instructions[4]  == 0xa0c8);  // mov isr, ~pins (one sample)
    CHECK(cam_handshake_program_instructions[5]  == 0x407e);  // in null, 30
    CHECK(cam_handshake_program_instructions[11] == 0xa0d1);  // mov isr, ::x
    CHECK(cam_handshake_program_instructions[13] == 0x6020);  // out x, 32
    CHECK(cam_handshake_program_instructions[20] == 0xc00c);  // irq prev nowait 4
    CHECK(strobe_gate_program_instructions[0] == 0x20c4);     // wait 1 irq, 4
    return true;
}

// 7a: J4.3 jumpered to J4.7 and J4.8. The strobes follow the trigger.
static bool loopback(void) {
    rig_t r;
    rig_init(&r, (cam_model_t){ 0u, 0u, 0u });
    cam_plan_t p = plan_us(CAM_SHOT_FIRST_DELAY_US, CAM_TIMEOUT_US, 100u);
    rig_run(&r, &p, 2000000u);
    if (!check_fired(&r, &p, CAM_OUT_EARLY)) return false;
    cam_decoded_t d = cam_decode(r.words[0], &p);
    // Seen at the second poll: the first samples before the synchroniser catches up.
    CHECK(d.detect_cycles == 6u);
    CHECK(cam_cycles_to_us(d.detect_cycles, SYS_HZ) < 0.05f);
    CHECK(fire_cycle(&r) == 29999);    // 199.993 us: 5 x 5997 + 14, the nearest to 200 us
    return true;
}

// 7b: the strobes answer 50 / 150 us after the trigger, within the first delay.
static bool delayed_early(void) {
    const uint32_t lat_us[2] = { 50u, 150u };
    for (int i = 0; i < 2; i++) {
        uint64_t lat = (uint64_t)lat_us[i] * (SYS_HZ / 1000000u);
        rig_t r;
        rig_init(&r, (cam_model_t){ lat, lat, 0u });
        cam_plan_t p = plan_us(200u, CAM_TIMEOUT_US, 100u);
        rig_run(&r, &p, 2000000u);
        if (!check_fired(&r, &p, CAM_OUT_EARLY)) return false;
        float t = cam_cycles_to_us(cam_decode(r.words[0], &p).detect_cycles, SYS_HZ);
        CHECK(t >= (float)lat_us[i] && t < (float)lat_us[i] + 0.05f);
    }
    return true;
}

// 7b: 300 us, past the 200 us first delay: FIRE at once, the delay clamped to 0.
static bool delayed_late(void) {
    uint64_t lat = 300u * (SYS_HZ / 1000000u);
    rig_t r;
    rig_init(&r, (cam_model_t){ lat, lat, 0u });
    cam_plan_t p = plan_us(200u, CAM_TIMEOUT_US, 100u);
    rig_run(&r, &p, 2000000u);
    if (!check_fired(&r, &p, CAM_OUT_LATE)) return false;
    cam_decoded_t d = cam_decode(r.words[0], &p);
    CHECK(d.fire_cycles == d.detect_cycles + CAM_PIO_FIRE_LATE_CYCLES);
    float t = cam_cycles_to_us(d.detect_cycles, SYS_HZ);
    CHECK(t >= 300.0f && t < 300.05f);
    return true;
}

// The boundary, one cycle at a time: t_cam from well inside the first delay to
// well past it. Every case FIREs at max(first delay, t_cam) + a few cycles, the
// phase flips exactly once, and nothing underflows.
static bool boundary_sweep(void) {
    cam_plan_t p = plan_us(20u, 200u, 2u);
    uint32_t fe = cam_fire_early_cycles(&p);
    int flips = 0;
    cam_outcome_t prev = CAM_OUT_EARLY;
    for (uint64_t lat = fe - 40u; lat < fe + 40u; lat++) {
        rig_t r;
        rig_init(&r, (cam_model_t){ lat, lat, 0u });
        rig_run(&r, &p, 200000u);
        cam_outcome_t o = cam_decode(r.words[0], &p).outcome;
        if (!check_fired(&r, &p, o)) { fprintf(stderr, "  at latency %llu\n", (unsigned long long)lat); return false; }
        CHECK(o == CAM_OUT_EARLY || o == CAM_OUT_LATE);
        if (o != prev) flips++;
        prev = o;
    }
    CHECK(flips == 1);
    CHECK(prev == CAM_OUT_LATE);
    return true;
}

// The two strobes at different times: it waits for the later one.
static bool staggered(void) {
    rig_t r;
    rig_init(&r, (cam_model_t){ 100u * 150u, 150u * 150u, 0u });
    cam_plan_t p = plan_us(200u, CAM_TIMEOUT_US, 10u);
    rig_run(&r, &p, 2000000u);
    if (!check_fired(&r, &p, CAM_OUT_EARLY)) return false;
    float t = cam_cycles_to_us(cam_decode(r.words[0], &p).detect_cycles, SYS_HZ);
    CHECK(t >= 150.0f && t < 150.05f);
    return true;
}

// Strobe 0 pulses and is gone before strobe 1 arrives: never both at once.
static bool never_both(void) {
    rig_t r;
    rig_init(&r, (cam_model_t){ 10u * 150u, 50u * 150u, 10u * 150u });
    cam_plan_t p = plan_us(20u, 100u, 10u);
    rig_run(&r, &p, 200000u);
    CHECK(!r.cam.error);
    CHECK(r.nwords == 1u);
    CHECK(r.words[0] == CAM_WORD_TIMEOUT);
    CHECK(r.cam.irq_set_count[8u + 4u] == 0u);
    return true;
}

// 7a timeout: one jumper removed. The trigger drops at the timeout, one word, and
// no FIRE -- so the strobe engine's gate never opens.
static bool timeout_one_strobe(void) {
    const uint64_t lat1[2] = { NEVER, 0u };
    for (int i = 0; i < 2; i++) {
        rig_t r;
        rig_init(&r, (cam_model_t){ i ? NEVER : 0u, lat1[i], 0u });
        cam_plan_t p = plan_us(200u, 1000u, 100u);
        rig_run(&r, &p, 2000000u);
        CHECK(!r.cam.error);
        CHECK(r.nwords == 1u);
        CHECK(cam_decode(r.words[0], &p).outcome == CAM_OUT_TIMEOUT);
        CHECK(r.cam.irq_set_count[8u + 4u] == 0u);
        int64_t rel = release_cycle(&r), want = (int64_t)cam_timeout_release_cycles(&p);
        CHECK(rel == want);
        CHECK(cam_cycles_to_us((uint32_t)rel, SYS_HZ) > 999.9f);
        CHECK(cam_cycles_to_us((uint32_t)rel, SYS_HZ) < 1000.1f);
    }
    return true;
}

// Small counts, where every fixed cycle shows: the exact constants.
static bool exact_small_counts(void) {
    for (uint32_t d = 0; d < 4u; d++)
    for (uint32_t t = 0; t < 3u; t++) {
        cam_plan_t p = { SYS_HZ, d, t, 1u };
        rig_t r;
        rig_init(&r, (cam_model_t){ NEVER, NEVER, 0u });
        rig_run(&r, &p, 10000u);
        CHECK(r.nwords == 1u && r.words[0] == CAM_WORD_TIMEOUT);
        int64_t rel = release_cycle(&r), want = (int64_t)cam_timeout_release_cycles(&p);
        CHECK(rel == want);

        // Strobes already high at the first look: early, unless D = 0 left no early poll.
        for (uint64_t lat = 0; lat < 24u; lat++) {
            rig_init(&r, (cam_model_t){ lat, lat, 0u });
            rig_run(&r, &p, 10000u);
            cam_outcome_t o = cam_decode(r.words[0], &p).outcome;
            if (o == CAM_OUT_TIMEOUT) {
                // Only when they came after the last late poll.
                CHECK(seen_both(&r.model) + 10u > cam_timeout_release_cycles(&p));
                CHECK(r.cam.irq_set_count[8u + 4u] == 0u);
                continue;
            }
            if (!check_fired(&r, &p, o)) {
                fprintf(stderr, "  D %u T %u latency %llu\n", (unsigned)d, (unsigned)t,
                        (unsigned long long)lat);
                return false;
            }
        }
    }
    return true;
}

// A zero first delay: every answer is "late", and fires 11 cycles after it is seen.
static bool first_delay_zero(void) {
    rig_t r;
    rig_init(&r, (cam_model_t){ 0u, 0u, 0u });
    cam_plan_t p = plan_us(0u, 100u, 10u);
    CHECK(p.d == 0u);
    rig_run(&r, &p, 200000u);
    return check_fired(&r, &p, CAM_OUT_LATE);
}

// Two handshakes in a row on the same state machine: the second starts clean
// (the phase flag, X, the FIFOs).
static bool back_to_back(void) {
    rig_t r;
    rig_init(&r, (cam_model_t){ 600u, 600u, 0u });
    cam_plan_t p = plan_us(20u, 200u, 5u);
    rig_run(&r, &p, 200000u);
    if (!check_fired(&r, &p, CAM_OUT_EARLY)) return false;
    // Second: the strobes are late this time.
    uint64_t base = r.cam.cycle;
    r.model.lat0 = r.model.lat1 = 40u * 150u;
    r.nwords = 0;
    r.cam.irq_set_count[12] = 0;
    r.rise_pad = r.fall_pad = -1;
    rig_run(&r, &p, 200000u);
    CHECK(r.rise_pad > (int64_t)base);
    return check_fired(&r, &p, CAM_OUT_LATE);
}

// End to end with the strobe engine in PIO0: the gate holds the burst until FIRE,
// the first rising edge comes 4-5 us after it, and the burst itself is the burst
// 6a measured (width N + 2 us, period width + gap) -- unchanged by the gate.
static bool gate_then_burst(void) {
    strobe_burst_t b = { 14u, 596u, 3u };
    for (uint32_t phase = 0; phase < 150u; phase += 37u) {
        rig_t r;
        rig_init(&r, (cam_model_t){ 150u * 150u, 150u * 150u, 0u });
        CHECK(rig_add_strobe(&r, &b, phase));
        cam_plan_t p = plan_us(200u, CAM_TIMEOUT_US, strobe_burst_duration_us(&b) + CAM_HOLD_MARGIN_US);
        rig_run(&r, &p, 3000000u);
        if (!check_fired(&r, &p, CAM_OUT_EARLY)) return false;
        CHECK(!r.str.error);
        CHECK(r.n_rise == b.count && r.n_fall == b.count);
        int64_t fire_abs = (int64_t)r.cam.irq_set_cycle[12];
        int64_t lag = (int64_t)r.pulse_rise[0] - fire_abs;
        CHECK(lag >= 4 * 150 && lag <= 5 * 150);
        CHECK(lag <= (int64_t)(STROBE_PIO_GATE_TO_EDGE_US + 1u) * 150);
        for (unsigned i = 0; i < b.count; i++)
            CHECK(r.pulse_fall[i] - r.pulse_rise[i] == (uint64_t)b.width_us * 150u);
        for (unsigned i = 1; i < b.count; i++)
            CHECK(r.pulse_rise[i] - r.pulse_rise[i - 1u] == (uint64_t)(b.width_us + b.gap_us) * 150u);
        CHECK(r.str.irq[0]);                                // the burst's completion flag
        CHECK(!r.irq_pio0[4]);                              // the gate consumed FIRE
        // The trigger was still high for the whole burst.
        CHECK(r.fall_pad > (int64_t)r.pulse_fall[b.count - 1u]);
    }
    return true;
}

// The 7a timeout with the strobe engine armed: the gate never opens, GPIO25
// never rises, and the schedule is still in the FIFO.
static bool gate_holds_on_timeout(void) {
    strobe_burst_t b = { 14u, 596u, 3u };
    rig_t r;
    rig_init(&r, (cam_model_t){ 0u, NEVER, 0u });
    CHECK(rig_add_strobe(&r, &b, 0u));
    cam_plan_t p = plan_us(200u, 1000u, 2000u);
    rig_run(&r, &p, 3000000u);
    CHECK(r.nwords == 1u && r.words[0] == CAM_WORD_TIMEOUT);
    CHECK(!r.gate_left);
    CHECK(r.str.pc == GATE_OFFSET);
    CHECK(r.n_rise == 0u);
    CHECK(r.str.tx.count == PIO_SIM_FIFO_DEPTH);           // nothing pulled
    CHECK(!r.str.irq[0]);
    return true;
}

// ---------------------------------------------------------------------------
// H7-F1 (independent review 2026-11-10): image 1.4 read strobe 0 and strobe 1 one
// clock apart, so a hand-over from one to the other between the two reads was
// taken for "both exposing" and FIREd. Now each poll is one sample of both.
// ---------------------------------------------------------------------------

// The cycles (from the trigger's `set`) at which the program samples the strobes,
// as cam_plan.h states them: early polls 1 + 5k for k = 0..D, late polls
// 5D + 9 + 5j for j = 0..T. The PIO sees a pad 2 clocks late, and the pad rose at
// cycle 1, so a strobe window [a, b) on the pad (from the pad rise) is high in
// the sample at s exactly when s - 3 is in [a, b).
static bool high_at(uint64_t lat, uint64_t width, uint64_t s) {
    if (lat == NEVER || s < 1u + PIO_SIM_SYNC_CYCLES) return false;
    uint64_t t = s - 1u - PIO_SIM_SYNC_CYCLES;
    return t >= lat && (width == 0u || t < lat + width);
}

// The outcome a coherent sampler must give: the first sample with both high.
static cam_outcome_t predicted(const cam_model_t *m, const cam_plan_t *p, uint32_t *at) {
    for (uint32_t k = 0; k <= p->d; k++) {
        uint64_t s = CAM_PIO_EARLY_POLL0_CYCLES + (uint64_t)CAM_PIO_LOOP_CYCLES * k;
        if (high_at(m->lat0, m->s0_width, s) && high_at(m->lat1, m->s1_width, s)) {
            *at = (uint32_t)s;
            return CAM_OUT_EARLY;
        }
    }
    for (uint32_t j = 0; j <= p->t; j++) {
        uint64_t s = (uint64_t)CAM_PIO_LOOP_CYCLES * p->d + CAM_PIO_LATE_POLL0_CYCLES
                   + (uint64_t)CAM_PIO_LOOP_CYCLES * j;
        if (high_at(m->lat0, m->s0_width, s) && high_at(m->lat1, m->s1_width, s)) {
            *at = (uint32_t)s;
            return CAM_OUT_LATE;
        }
    }
    return CAM_OUT_TIMEOUT;
}

// The reviewer's case, then a hand-over at every one of five consecutive cycles
// (every poll phase), in both orders, in the early and in the late window. The
// two strobes are never high in the same clock: every one must time out.
static bool nonoverlap_handover(void) {
    {
        rig_t r;
        rig_init(&r, (cam_model_t){ 0u, 1499u, 1499u, 0u });   // the review's probe
        cam_plan_t p = plan_us(200u, 1000u, 100u);
        rig_run(&r, &p, 300000u);
        CHECK(!r.cam.error);
        CHECK(r.nwords == 1u && r.words[0] == CAM_WORD_TIMEOUT);
        CHECK(r.cam.irq_set_count[8u + 4u] == 0u);
    }
    const cam_plan_t p = { SYS_HZ, 40u, 40u, 1u };
    const uint64_t early_b = 60u, late_b = 5u * 40u + 60u;     // well inside each window
    for (int phase_window = 0; phase_window < 2; phase_window++)
    for (int order = 0; order < 2; order++)
    for (uint64_t off = 0; off < 2u * CAM_PIO_LOOP_CYCLES; off++) {
        uint64_t b = (phase_window ? late_b : early_b) + off;  // the hand-over, on the pad
        cam_model_t m;
        memset(&m, 0, sizeof m);
        // The first strobe: high from 2 until b. The second: from b on.
        if (order == 0) { m.lat0 = 2u; m.s0_width = b - 2u; m.lat1 = b; }
        else            { m.lat1 = 2u; m.s1_width = b - 2u; m.lat0 = b; }
        rig_t r;
        rig_init(&r, m);
        rig_run(&r, &p, 20000u);
        if (r.nwords != 1u || r.words[0] != CAM_WORD_TIMEOUT || r.cam.irq_set_count[12]) {
            fprintf(stderr, "  %s window, strobe %d first, hand-over at pad cycle %llu: "
                    "%u words, word0 %08lx, FIRE %u\n", phase_window ? "late" : "early",
                    order, (unsigned long long)b, r.nwords, (unsigned long)r.words[0],
                    r.cam.irq_set_count[12]);
            CHECK(false);
        }
    }
    return true;
}

// The whole claim, over many waveforms: FIRE happens exactly when one poll's single
// sample saw both strobes high, and t_cam is that sample. Windows of every length
// from 1 cycle, overlapping or not, in either order, early or late (a fixed LCG,
// so the run is the same every time).
static bool coherent_property(void) {
    uint32_t seed = 0x2611100u;
    unsigned fired = 0, timed_out = 0;
    for (unsigned n = 0; n < 4000u; n++) {
        seed = seed * 1664525u + 1013904223u;
        cam_plan_t p = { SYS_HZ, (seed >> 4) % 9u, (seed >> 9) % 9u, 1u };
        cam_model_t m;
        memset(&m, 0, sizeof m);
        uint32_t span = CAM_PIO_LOOP_CYCLES * (p.d + p.t) + 30u;
        seed = seed * 1664525u + 1013904223u; m.lat0     = (seed >> 8) % span;
        seed = seed * 1664525u + 1013904223u; m.s0_width = 1u + (seed >> 8) % 40u;
        if ((seed >> 24) % 4u == 0u) m.s0_width = 0u;           // sometimes stays up
        seed = seed * 1664525u + 1013904223u; m.lat1     = (seed >> 8) % span;
        seed = seed * 1664525u + 1013904223u; m.s1_width = 1u + (seed >> 8) % 40u;
        if ((seed >> 24) % 4u == 0u) m.s1_width = 0u;
        uint32_t at = 0;
        cam_outcome_t want = predicted(&m, &p, &at);
        rig_t r;
        rig_init(&r, m);
        rig_run(&r, &p, 20000u);
        CHECK(!r.cam.error);
        cam_decoded_t d = cam_decode(r.words[0], &p);
        if (d.outcome != want || (want != CAM_OUT_TIMEOUT && d.detect_cycles != at)) {
            fprintf(stderr, "  case %u: D %u T %u, s0 [%llu,+%llu) s1 [%llu,+%llu): want %d at %u,"
                    " got %d at %u\n", n, (unsigned)p.d, (unsigned)p.t,
                    (unsigned long long)m.lat0, (unsigned long long)m.s0_width,
                    (unsigned long long)m.lat1, (unsigned long long)m.s1_width,
                    (int)want, (unsigned)at, (int)d.outcome, (unsigned)d.detect_cycles);
            CHECK(false);
        }
        if (want == CAM_OUT_TIMEOUT) {
            timed_out++;
            CHECK(r.nwords == 1u && r.cam.irq_set_count[12] == 0u);
        } else {
            fired++;
            CHECK(r.nwords == 2u && r.cam.irq_set_count[12] == 1u);
            CHECK(fire_cycle(&r) == (int64_t)d.fire_cycles);
        }
    }
    printf("  %u fired, %u timed out\n", fired, timed_out);
    CHECK(fired > 500u && timed_out > 500u);       // both outcomes well exercised
    return true;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(n) { #n, n }
        TEST(encodings), TEST(loopback), TEST(delayed_early), TEST(delayed_late),
        TEST(boundary_sweep), TEST(staggered), TEST(never_both), TEST(timeout_one_strobe),
        TEST(exact_small_counts), TEST(first_delay_zero), TEST(back_to_back),
        TEST(gate_then_burst), TEST(gate_holds_on_timeout),
        TEST(nonoverlap_handover), TEST(coherent_property),
#undef TEST
    };
    if (argc == 2) {
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
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
