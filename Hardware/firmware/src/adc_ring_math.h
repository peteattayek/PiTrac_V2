// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// adc_ring_math.h -- the arithmetic of copying out of the ADC DMA ring, kept free
// of SDK headers so the native host tests compile it (tests/strobe_live_test.c).
// adc_engine.c is the only firmware user.

#ifndef PITRAC_ADC_RING_MATH_H
#define PITRAC_ADC_RING_MATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adc_engine.h"     // adc_5vin_status_t; no SDK headers

// How many samples of ONE channel a freeze may copy back from the newest.
//
//   raw_time : raw ring slots the clock says this mode has converted (elapsed x
//              rate, less margin) -- an UPPER bound on what was written.
//   written  : raw ring slots the DMA has ISSUED since the mode began, read from
//              its write index (each issue is one finished conversion). Exact
//              until the ring's first wrap, smaller after it. The freeze reads
//              it only after a completed DMA stop, when every issued write has
//              landed (review rounds 4-5).
//   nch      : round-robin channel count of the mode (1, 2 or 4).
//
// Board 1, 2026-10-12: the clock alone said ~218 slots while the DMA had written
// 122, and the copy returned 86 slots of the previous mode's round-robin as
// ADC0. Taking the smaller of the two can only ever give back fewer samples,
// all of them written in this mode. Whole round-robin groups only, so the walk
// back from the channel's newest slot never crosses index 0.
static inline size_t adc_ring_copy_avail(uint64_t raw_time, size_t written, unsigned nch) {
    if (nch == 0u) return 0u;
    uint64_t raw = raw_time < (uint64_t)written ? raw_time : (uint64_t)written;
    return (size_t)(raw / nch);
}

// The ADC's DIV register for a requested sample rate, in its 16.8 fixed-point
// form (DIV.INT << 8 | DIV.FRAC). Datasheet 12.4.3.2: DIV = 0 runs conversions
// back to back, one per 96 clk_adc = 500 ksps at 48 MHz; DIV.INT = n triggers
// once per n + 1 cycles, and "the ADC ignores this if a conversion is currently
// in progress, so generally n will be >= 96". The firmware used n = 95 for 500
// ksps -- a trigger exactly as each 96-cycle conversion ends -- and every other
// trigger was ignored: every ring mode ran at HALF its stated rate (BURST 250
// ksps, measured on board 1 by `adc ringtest`, 2026-10-12). That was the
// morning's "late ring start": 122 samples where the clock said ~218.
//   rate 0 or >= the maximum : 0 (back to back, the maximum)
//   otherwise                : clk / rate - 1, never below 96.0
static inline uint32_t adc_div_for_rate(float clk_hz, uint32_t max_rate_hz, uint32_t rate_hz) {
    if (rate_hz == 0u || rate_hz >= max_rate_hz) return 0u;
    float div = clk_hz / (float)rate_hz - 1.0f;
    if (div < 96.0f) div = 96.0f;
    return (uint32_t)(div * 256.0f);
}

// SysTick is a 24-bit DOWN-counter, free-running at the processor clock (set up
// by adc_engine_init()). Cycles from an `earlier` reading to a `later` one, valid
// for intervals under 2^24 cycles (111.8 ms at 150 MHz).
static inline uint32_t adc_systick_elapsed(uint32_t earlier, uint32_t later) {
    return (earlier - later) & 0x00FFFFFFu;
}

// The +5V_IN reading out of the IDLE ring -- adc_5vin_read()'s arithmetic,
// kept here so the host tests drive the real code (review round 6 reproduced a
// FRESH 6.853 V from a 4.600 V supply on a copy of the old body).
//   ring, mask : the ring and (its size - 1), a power of two
//   w          : the write index after adc_5vin_read() waited for (n + 2) x nch
//                transfers issued during the call
//   nch, slot  : the round-robin's channel count and ch1's position in it
//   hw_bad     : FCS.OVER or FCS.UNDER since the ring started, or a DMA
//                READ/WRITE_ERROR on its channel
// INVALID if hw_bad -- an overflow LOSES conversions, so the round-robin may have
// rotated and these slots may hold another channel -- or if any averaged sample
// carries the conversion-error bit (datasheet 12.4.3.4: discard them). Else
// FRESH with *code = the mean of the n newest ch1 samples, the newest group
// skipped because its write may still be in flight.
static inline adc_5vin_status_t adc_5vin_from_ring(const uint16_t *ring, size_t mask, size_t w,
                                                   unsigned nch, unsigned slot, unsigned n,
                                                   bool hw_bad, uint16_t *code) {
    if (hw_bad || !ring || nch == 0u || slot >= nch || n == 0u) return ADC_5VIN_INVALID;
    size_t i = (w + mask - nch) & mask;               // w - 1 - nch, mod the ring
    for (unsigned k = 0; k < nch && (i % nch) != slot; k++) i = (i + mask) & mask;
    uint32_t sum = 0;
    for (unsigned k = 0; k < n; k++) {
        const uint16_t v = ring[i];
        if (v & 0x8000u) return ADC_5VIN_INVALID;
        sum += v & 0x0FFFu;
        i = (i + mask + 1u - nch) & mask;
    }
    if (code) *code = (uint16_t)(sum / n);
    return ADC_5VIN_FRESH;
}

#endif // PITRAC_ADC_RING_MATH_H
