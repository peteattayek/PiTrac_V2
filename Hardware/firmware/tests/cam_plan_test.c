// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// cam_plan_test.c -- the camera handshake's arithmetic (src/cam_plan.c): the
// request limits, the loop counts, and decoding the state machine's words. The
// cycle constants themselves are proven against the program in cam_pio_test.c.

#include "cam_plan.h"
#include "board.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *test_name;

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, test_name, #c); \
    return false; } } while (0)

#define HZ 150000000u

static cam_plan_t make(uint32_t first, uint32_t timeout, uint32_t hold, cam_plan_err_t *e) {
    cam_req_t q = { first, timeout, hold };
    cam_plan_t p;
    memset(&p, 0, sizeof p);
    *e = cam_plan_make(&q, HZ, &p);
    return p;
}

static bool request_limits(void) {
    cam_plan_err_t e;
    make(CAM_SHOT_FIRST_DELAY_US, CAM_TIMEOUT_US, 100u, &e);   CHECK(e == CAM_PLAN_OK);
    make(0u, CAM_TIMEOUT_MIN_US, CAM_HOLD_MIN_US, &e);           CHECK(e == CAM_PLAN_OK);
    make(CAM_FIRST_DELAY_MAX_US, CAM_TIMEOUT_MAX_US, CAM_HOLD_MAX_US, &e); CHECK(e == CAM_PLAN_OK);
    make(CAM_FIRST_DELAY_MAX_US + 1u, CAM_TIMEOUT_MAX_US, 100u, &e); CHECK(e == CAM_PLAN_FIRST_DELAY);
    make(0u, CAM_TIMEOUT_MIN_US - 1u, 100u, &e);                 CHECK(e == CAM_PLAN_TIMEOUT);
    make(0u, CAM_TIMEOUT_MAX_US + 1u, 100u, &e);                 CHECK(e == CAM_PLAN_TIMEOUT);
    // The timeout may not be shorter than the first delay: a camera that answers
    // between them would be "late" and "timed out" at once.
    make(300u, 299u, 100u, &e);                                  CHECK(e == CAM_PLAN_TIMEOUT);
    make(300u, 300u, 100u, &e);                                  CHECK(e == CAM_PLAN_OK);
    make(0u, 100u, 0u, &e);                                      CHECK(e == CAM_PLAN_HOLD);
    make(0u, 100u, CAM_HOLD_MAX_US + 1u, &e);                    CHECK(e == CAM_PLAN_HOLD);

    cam_req_t q = { 0u, 100u, 10u };
    cam_plan_t p;
    CHECK(cam_plan_make(&q, CAM_SYS_HZ_MIN - 1u, &p) == CAM_PLAN_BAD_CLOCK);
    CHECK(cam_plan_make(&q, CAM_SYS_HZ_MAX + 1u, &p) == CAM_PLAN_BAD_CLOCK);
    CHECK(cam_plan_make(NULL, HZ, &p) != CAM_PLAN_OK);
    CHECK(cam_plan_make(&q, HZ, NULL) != CAM_PLAN_OK);
    for (int k = CAM_PLAN_OK; k <= CAM_PLAN_HOLD; k++)
        CHECK(strcmp(cam_plan_err_text((cam_plan_err_t)k), "?") != 0);
    return true;
}

// The counts put FIRE, the timeout release and the hold release within half a
// loop (2 cycles) of what was asked, at every value, and never below zero.
static bool counts_round_trip(void) {
    cam_plan_err_t e;
    for (uint32_t us = 0; us <= 2000u; us += 7u) {
        cam_plan_t p = make(us, us + 100u, us + 1u, &e);
        CHECK(e == CAM_PLAN_OK);
        long want = (long)cam_us_to_cycles(us, HZ);
        if (want >= (long)CAM_PIO_FIRE_EARLY_CYCLES)
            CHECK(labs((long)cam_fire_early_cycles(&p) - want) <= 2);
        else
            CHECK(p.d == 0u);
        long to = (long)cam_us_to_cycles(us + 100u, HZ);
        CHECK(labs((long)cam_timeout_release_cycles(&p) - to) <= 2);
        long hold = (long)cam_us_to_cycles(us + 1u, HZ);
        CHECK(labs((long)cam_release_after_fire_cycles(&p, 0u) - hold) <= 2);
    }
    // The defaults: 200 us at 150 MHz is 30000 cycles; the nearest FIRE the 5-cycle
    // loop can make is 5 x 5997 + 14 = 29999 (199.993 us).
    cam_plan_t p = make(200u, 5000u, 100u, &e);
    CHECK(p.d == 5997u && cam_fire_early_cycles(&p) == 29999u);
    // Largest values stay inside the word ranges at the fastest clock.
    cam_req_t q = { CAM_FIRST_DELAY_MAX_US, CAM_TIMEOUT_MAX_US, CAM_HOLD_MAX_US };
    CHECK(cam_plan_make(&q, CAM_SYS_HZ_MAX, &p) == CAM_PLAN_OK);
    CHECK(p.d <= CAM_COUNT_MAX && p.t <= CAM_COUNT_MAX && p.h <= CAM_COUNT_MAX);
    return true;
}

static bool conversions(void) {
    CHECK(cam_us_to_cycles(1u, HZ) == 150u);
    CHECK(cam_us_to_cycles(0xFFFFFFFFu, CAM_SYS_HZ_MAX) == 0xFFFFFFFFu);   // saturates
    CHECK(fabsf(cam_cycles_to_us(150u, HZ) - 1.0f) < 1e-6f);
    CHECK(cam_cycles_to_us(5u, 0u) == 0.0f);
    return true;
}

// Every word the program can send, and the ones it cannot.
static bool decode_words(void) {
    cam_plan_t p = { HZ, 100u, 50u, 10u };

    cam_decoded_t d = cam_decode(CAM_WORD_TIMEOUT, &p);
    CHECK(d.outcome == CAM_OUT_TIMEOUT && !d.delay_clamped);

    // Early: X from D (seen at the first poll) down to 0 (the last).
    d = cam_decode(100u, &p);
    CHECK(d.outcome == CAM_OUT_EARLY && d.detect_cycles == 1u && d.fire_cycles == 514u);
    d = cam_decode(0u, &p);
    CHECK(d.outcome == CAM_OUT_EARLY && d.detect_cycles == 501u && d.fire_cycles == 514u);
    CHECK(!d.delay_clamped);
    CHECK(cam_decode(101u, &p).outcome == CAM_OUT_BAD_WORD);         // X > D

    // Late: ~X, from T at the first late poll down to 0.
    d = cam_decode(~50u, &p);
    CHECK(d.outcome == CAM_OUT_LATE && d.detect_cycles == 509u && d.fire_cycles == 522u);
    CHECK(d.delay_clamped);
    d = cam_decode(~0u, &p);   // == CAM_WORD_END: as a FIRST word it is a late ready
    CHECK(d.outcome == CAM_OUT_LATE && d.detect_cycles == 509u + 250u);
    CHECK(cam_decode(~51u, &p).outcome == CAM_OUT_BAD_WORD);         // X > T

    // Between the ranges: nothing the program sends.
    CHECK(cam_decode(0x40000000u, &p).outcome == CAM_OUT_BAD_WORD);
    CHECK(cam_decode(0x80000001u, &p).outcome == CAM_OUT_BAD_WORD);
    CHECK(cam_decode(0u, NULL).outcome == CAM_OUT_BAD_WORD);

    // A late FIRE is never earlier than the first delay would have been.
    for (uint32_t x = 0; x <= p.t; x++)
        CHECK(cam_decode(~x, &p).fire_cycles > cam_fire_early_cycles(&p));

    for (int k = CAM_OUT_EARLY; k <= CAM_OUT_BAD_WORD; k++)
        CHECK(strcmp(cam_outcome_text((cam_outcome_t)k), "?") != 0);
    return true;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(n) { #n, n }
        TEST(request_limits), TEST(counts_round_trip), TEST(conversions), TEST(decode_words),
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
