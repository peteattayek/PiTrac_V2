// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// cam_plan.c -- see cam_plan.h. Pure arithmetic: host-tested, and checked
// against the assembled PIO program in tests/cam_pio_test.c.

#include "cam_plan.h"
#include "board.h"

#include <stddef.h>

uint32_t cam_us_to_cycles(uint32_t us, uint32_t sys_hz) {
    uint64_t c = (uint64_t)us * (uint64_t)sys_hz / 1000000u;
    return c > UINT32_MAX ? UINT32_MAX : (uint32_t)c;
}

float cam_cycles_to_us(uint32_t cycles, uint32_t sys_hz) {
    if (sys_hz == 0u) return 0.0f;
    return (float)((double)cycles * 1e6 / (double)sys_hz);
}

// Loops for a span of `cycles` that the program spends `fixed` cycles of on top
// of the loops, rounded to the nearest loop and never below zero.
static uint32_t loops_for(uint32_t cycles, uint32_t fixed) {
    if (cycles <= fixed) return 0u;
    return (cycles - fixed + CAM_PIO_LOOP_CYCLES / 2u) / CAM_PIO_LOOP_CYCLES;
}

cam_plan_err_t cam_plan_make(const cam_req_t *r, uint32_t sys_hz, cam_plan_t *out) {
    if (!r || !out) return CAM_PLAN_TIMEOUT;
    if (sys_hz < CAM_SYS_HZ_MIN || sys_hz > CAM_SYS_HZ_MAX) return CAM_PLAN_BAD_CLOCK;
    if (r->first_delay_us > CAM_FIRST_DELAY_MAX_US) return CAM_PLAN_FIRST_DELAY;
    if (r->timeout_us < CAM_TIMEOUT_MIN_US || r->timeout_us > CAM_TIMEOUT_MAX_US ||
        r->timeout_us < r->first_delay_us) return CAM_PLAN_TIMEOUT;
    if (r->hold_us < CAM_HOLD_MIN_US || r->hold_us > CAM_HOLD_MAX_US) return CAM_PLAN_HOLD;

    cam_plan_t p;
    p.sys_hz = sys_hz;

    // FIRE = 5D + 14: the first delay.
    p.d = loops_for(cam_us_to_cycles(r->first_delay_us, sys_hz), CAM_PIO_FIRE_EARLY_CYCLES);
    // Trigger released at 5D + 5T + 18 when nothing answers: the timeout.
    uint32_t to_cycles = cam_us_to_cycles(r->timeout_us, sys_hz);
    uint32_t used      = CAM_PIO_LOOP_CYCLES * p.d + CAM_PIO_TIMEOUT_RELEASE_CYCLES;
    p.t = to_cycles > used ? loops_for(to_cycles - used, 0u) : 0u;
    // Trigger released at FIRE + 5H + 9: the hold.
    p.h = loops_for(cam_us_to_cycles(r->hold_us, sys_hz), CAM_PIO_HOLD_RELEASE_CYCLES);

    // The limits above keep every count far below this at any clock in range
    // (50 ms at 300 MHz is 3.75 M loops); checked anyway, because the word
    // ranges in cam_decode() depend on it.
    if (p.d > CAM_COUNT_MAX) return CAM_PLAN_FIRST_DELAY;
    if (p.t > CAM_COUNT_MAX) return CAM_PLAN_TIMEOUT;
    if (p.h > CAM_COUNT_MAX) return CAM_PLAN_HOLD;
    *out = p;
    return CAM_PLAN_OK;
}

const char *cam_plan_err_text(cam_plan_err_t e) {
    switch (e) {
        case CAM_PLAN_OK:          return "ok";
        case CAM_PLAN_BAD_CLOCK:   return "the system clock is outside the range the handshake is written for";
        case CAM_PLAN_FIRST_DELAY: return "first delay over the maximum";
        case CAM_PLAN_TIMEOUT:     return "timeout out of range, or shorter than the first delay";
        case CAM_PLAN_HOLD:        return "trigger hold out of range";
    }
    return "?";
}

uint32_t cam_fire_early_cycles(const cam_plan_t *p) {
    return CAM_PIO_LOOP_CYCLES * p->d + CAM_PIO_FIRE_EARLY_CYCLES;
}

uint32_t cam_timeout_release_cycles(const cam_plan_t *p) {
    return CAM_PIO_LOOP_CYCLES * (p->d + p->t) + CAM_PIO_TIMEOUT_RELEASE_CYCLES;
}

uint32_t cam_release_after_fire_cycles(const cam_plan_t *p, uint32_t fire_cycles) {
    return fire_cycles + CAM_PIO_LOOP_CYCLES * p->h + CAM_PIO_HOLD_RELEASE_CYCLES;
}

cam_decoded_t cam_decode(uint32_t word, const cam_plan_t *p) {
    cam_decoded_t d = { CAM_OUT_BAD_WORD, 0u, 0u, 0u, false };
    if (!p) return d;

    if (word == CAM_WORD_TIMEOUT) {
        d.outcome = CAM_OUT_TIMEOUT;
        return d;
    }

    if ((word & 0x80000000u) == 0u) {
        // Early: poll k = D - X saw both strobes; FIRE waits out the first delay.
        if (word > p->d) return d;
        uint32_t k = p->d - word;
        d.outcome       = CAM_OUT_EARLY;
        d.x             = word;
        d.detect_cycles = CAM_PIO_EARLY_POLL0_CYCLES + CAM_PIO_LOOP_CYCLES * k;
        d.fire_cycles   = cam_fire_early_cycles(p);
        return d;
    }

    // Late: poll j = T - X of the late window saw them; FIRE at once.
    uint32_t x = ~word;
    if (x > p->t) return d;
    uint32_t j = p->t - x;
    d.outcome       = CAM_OUT_LATE;
    d.x             = x;
    d.detect_cycles = CAM_PIO_LOOP_CYCLES * p->d + CAM_PIO_LATE_POLL0_CYCLES
                    + CAM_PIO_LOOP_CYCLES * j;
    d.fire_cycles   = d.detect_cycles + CAM_PIO_FIRE_LATE_CYCLES;
    d.delay_clamped = true;
    return d;
}

const char *cam_outcome_text(cam_outcome_t o) {
    switch (o) {
        case CAM_OUT_EARLY:    return "both strobes within the first delay: fired at the first delay";
        case CAM_OUT_LATE:     return "both strobes after the first delay: fired at once (delay clamped to 0)";
        case CAM_OUT_TIMEOUT:  return "TIMEOUT: both strobes not seen; trigger dropped, nothing fired";
        case CAM_OUT_BAD_WORD: return "BAD WORD from the handshake engine";
    }
    return "?";
}
