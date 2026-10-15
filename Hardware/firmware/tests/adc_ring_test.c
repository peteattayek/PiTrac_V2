// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// Host tests for adc_ring_math.h's +5V_IN reading (adc_5vin_from_ring), the
// arithmetic adc_5vin_read() runs on the IDLE ring. No hardware, no SDK.
// Independent review round 6 (2026-10-12) reproduced, on a copy of the old body,
// a FRESH 6.853 V from a 4.600 V supply after two lost conversions: an overflow
// rotates the round-robin, and "ch1's slots" then hold another channel.
#include "adc_ring_math.h"
#include "adc_engine.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *test_name;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, test_name, #condition); \
        return false; \
    } \
} while (0)

// The IDLE round-robin: {ch1, ch2, ch5, ch7}, started at index 0 with ch1, so
// ring index i holds slot i % 4. ch1 is slot 0.
#define NCH      4u
#define SLOT_CH1 0u
#define MASK     (ADC_RING_SAMPLES - 1u)
#define N        128u

// Codes at the pin: ch1 = 5V_IN / 2, ch2 TIA, ch5 DETECT, ch7 mic bias.
#define CODE_CH1_5V2  3038u     // ~5.2 V supply (as `adc 1` read on 2026-10-12)
#define CODE_CH1_4V6  2700u     // ~4.6 V supply
#define CODE_CH2         7u
#define CODE_CH5        10u
#define CODE_CH7      2055u

static uint16_t ring[ADC_RING_SAMPLES];

static void fill_idle(uint16_t ch1) {
    static const uint16_t other[NCH] = { 0u, CODE_CH2, CODE_CH5, CODE_CH7 };
    for (size_t i = 0; i < ADC_RING_SAMPLES; i++)
        ring[i] = (i % NCH == SLOT_CH1) ? ch1 : other[i % NCH];
}

static bool v5in_fresh(void) {
    fill_idle(CODE_CH1_5V2);
    // Any write index -- on a group boundary or not.
    for (size_t w = 600u; w < 608u; w++) {
        uint16_t code = 0;
        CHECK(adc_5vin_from_ring(ring, MASK, w, NCH, SLOT_CH1, N, false, &code) == ADC_5VIN_FRESH);
        CHECK(code == CODE_CH1_5V2);
    }
    // Across the ring's wrap.
    uint16_t code = 0;
    CHECK(adc_5vin_from_ring(ring, MASK, 100u, NCH, SLOT_CH1, N, false, &code) == ADC_5VIN_FRESH);
    CHECK(code == CODE_CH1_5V2);
    return true;
}

// Any FIFO overflow / underflow or DMA bus error since the ring started voids
// the reading, whatever the samples look like.
static bool v5in_rejects_hw_flags(void) {
    fill_idle(CODE_CH1_5V2);
    uint16_t code = 0xBEEFu;
    CHECK(adc_5vin_from_ring(ring, MASK, 600u, NCH, SLOT_CH1, N, true, &code) == ADC_5VIN_INVALID);
    CHECK(code == 0xBEEFu);                       // nothing reported
    return true;
}

// A conversion-error bit on any AVERAGED ch1 sample voids it (datasheet
// 12.4.3.4: discard them); one on another channel, or on an older ch1 sample
// outside the window, does not.
static bool v5in_rejects_conv_error(void) {
    const size_t w = 600u;
    uint16_t code = 0;
    // The window: the newest ch1 slot at least one group behind w, back N.
    size_t newest = (w - 1u - NCH) & MASK;
    while (newest % NCH != SLOT_CH1) newest = (newest - 1u) & MASK;
    const size_t oldest = (newest - (N - 1u) * NCH) & MASK;

    fill_idle(CODE_CH1_5V2);
    ring[newest] |= 0x8000u;
    CHECK(adc_5vin_from_ring(ring, MASK, w, NCH, SLOT_CH1, N, false, &code) == ADC_5VIN_INVALID);

    fill_idle(CODE_CH1_5V2);
    ring[oldest] |= 0x8000u;
    CHECK(adc_5vin_from_ring(ring, MASK, w, NCH, SLOT_CH1, N, false, &code) == ADC_5VIN_INVALID);

    fill_idle(CODE_CH1_5V2);
    ring[(oldest - NCH) & MASK] |= 0x8000u;       // one ch1 sample older than the window
    ring[newest + 1u] |= 0x8000u;                 // ch2, inside the window
    CHECK(adc_5vin_from_ring(ring, MASK, w, NCH, SLOT_CH1, N, false, &code) == ADC_5VIN_FRESH);
    CHECK(code == CODE_CH1_5V2);
    return true;
}

// The newest group's write may still be in flight: its ch1 slot is skipped.
static bool v5in_skips_newest_group(void) {
    const size_t w = 600u;                        // a group boundary: slot 0 at 596
    fill_idle(CODE_CH1_5V2);
    ring[596] = 0u;                               // the newest ch1 slot, maybe not landed
    uint16_t code = 0;
    CHECK(adc_5vin_from_ring(ring, MASK, w, NCH, SLOT_CH1, N, false, &code) == ADC_5VIN_FRESH);
    CHECK(code == CODE_CH1_5V2);
    return true;
}

// Round 6's reproduction. The supply is 4.6 V. Two conversions were lost to a
// FIFO overflow at index D, so from D on ring[i] holds conversion i + 2: the
// round-robin is rotated by two and "slot 0" now holds ch5. Read as if nothing
// happened, the old arithmetic reports a confident wrong value; with the
// overflow flag (which the hardware sets whenever a conversion is lost) it is
// INVALID.
static bool v5in_phase_rotation(void) {
    static const uint16_t by_slot[NCH] = { CODE_CH1_4V6, CODE_CH2, CODE_CH5, CODE_CH7 };
    const size_t D = 100u, w = 900u;
    for (size_t i = 0; i < ADC_RING_SAMPLES; i++) {
        const size_t conversion = i < D ? i : i + 2u;
        ring[i] = by_slot[conversion % NCH];
    }
    uint16_t code = 0;
    CHECK(adc_5vin_from_ring(ring, MASK, w, NCH, SLOT_CH1, N, false, &code) == ADC_5VIN_FRESH);
    CHECK(code != CODE_CH1_4V6);                  // the hazard: a wrong channel, "fresh"
    CHECK(adc_5vin_from_ring(ring, MASK, w, NCH, SLOT_CH1, N, true, &code) == ADC_5VIN_INVALID);
    return true;
}

// The ADC rate (2026-10-12, measured on board 1: every ring mode ran at half its
// stated rate). DIV = 0 is the 500 ksps maximum; any nonzero DIV must be >= 96,
// or the ADC ignores the triggers that land during a conversion.
static bool adc_rate_div(void) {
    const float clk = 48000000.0f;
    const uint32_t max = 500000u;
    CHECK(adc_div_for_rate(clk, max, 500000u) == 0u);          // the bug: was 95 << 8
    CHECK(adc_div_for_rate(clk, max, 0u) == 0u);
    CHECK(adc_div_for_rate(clk, max, 600000u) == 0u);
    CHECK(adc_div_for_rate(clk, max, 250000u) == 191u << 8);   // 192 cycles
    CHECK(adc_div_for_rate(clk, max, 100000u) == 479u << 8);
    CHECK(adc_div_for_rate(clk, max, 1000u) == 47999u << 8);   // the datasheet's example
    CHECK(adc_div_for_rate(clk, max, 499000u) == 96u << 8);    // would be 95.19: clamped
    // No request may yield a nonzero DIV below 96.
    for (uint32_t r = 1000u; r < max; r += 997u) {
        const uint32_t d = adc_div_for_rate(clk, max, r);
        CHECK(d == 0u || (d >> 8) >= 96u);
    }
    return true;
}

static bool v5in_bad_args(void) {
    fill_idle(CODE_CH1_5V2);
    uint16_t code = 0;
    CHECK(adc_5vin_from_ring(NULL, MASK, 600u, NCH, SLOT_CH1, N, false, &code) == ADC_5VIN_INVALID);
    CHECK(adc_5vin_from_ring(ring, MASK, 600u, 0u, 0u, N, false, &code) == ADC_5VIN_INVALID);
    CHECK(adc_5vin_from_ring(ring, MASK, 600u, NCH, NCH, N, false, &code) == ADC_5VIN_INVALID);
    CHECK(adc_5vin_from_ring(ring, MASK, 600u, NCH, SLOT_CH1, 0u, false, &code) == ADC_5VIN_INVALID);
    return true;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(name) {#name, name}
        TEST(v5in_fresh), TEST(v5in_rejects_hw_flags), TEST(v5in_rejects_conv_error),
        TEST(v5in_skips_newest_group), TEST(v5in_phase_rotation), TEST(v5in_bad_args),
        TEST(adc_rate_div)
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
