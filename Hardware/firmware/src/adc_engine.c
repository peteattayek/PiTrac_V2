// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "adc_engine.h"
#include "board.h"
#include "safe_state.h"

#include "hardware/adc.h"
#include "hardware/dma.h"
#include "pico/stdlib.h"

#include <string.h>

// The RP2350 ADC clock is 48 MHz and a conversion takes 96 cycles -> 500 ksps.
#define ADC_CLK_HZ        48000000.0f
#define ADC_CYCLES        96.0f
#define ADC_MAX_RATE_HZ   500000u

static adc_mode_t s_mode = ADC_MODE_OFF;

// Default +5V_IN scale correction. NOT 1.0 -- measured 2026-07-30 on the first
// board: a true 5.200 V input reads back as 4.893 V uncalibrated, a 5.9% low bias.
// 5.200 / 4.893 = 1.063.
//
// Cause is almost certainly the divider's source impedance. R46/R47 = 100K/100K
// presents 50 kOhm to the ADC, and the RP2350 wants <= 10 kOhm: the sample-and-hold
// cap cannot fully charge through 50 kOhm inside the sampling window, and a few uA
// of input leakage across 50 kOhm is hundreds of mV on its own. That is a property
// of the board design, not of one unit, so this default should be roughly right
// for any board built from these files.
//
// This default MATTERS: without it the firmware reports a good 5.2 V bench supply
// as 4.89 V, which is below V5_MIN_FOR_LATCH (5.05 V), and refuses to close the
// latch at all. `adc5vcal` still trims per-unit on top of this.
#define V5IN_SCALE_DEFAULT  1.063f

static float      s_5vin_scale = V5IN_SCALE_DEFAULT;
static int        s_dma_chan = -1;

static uint16_t   s_buf[ADC_CAPTURE_MAX_SAMPLES];
static size_t     s_buf_count;
static uint       s_buf_mask;
static uint32_t   s_buf_rate;
static bool       s_overran;

// ---------------------------------------------------------------------------

static uint mask_lowest_channel(uint mask) {
    for (uint c = 0; c < 8; c++) if (mask & (1u << c)) return c;
    return 0;
}

static uint mask_popcount(uint mask) {
    uint n = 0;
    for (uint c = 0; c < 8; c++) if (mask & (1u << c)) n++;
    return n;
}

// Set the sample rate. div is in the ADC's 8.8 fixed-point format: the ADC
// starts a conversion every (div+1) clk_adc cycles, so rate = 48e6/(div+1).
static void set_rate(uint32_t rate_hz) {
    if (rate_hz == 0 || rate_hz > ADC_MAX_RATE_HZ) rate_hz = ADC_MAX_RATE_HZ;
    float div = (ADC_CLK_HZ / (float)rate_hz) - 1.0f;
    if (div < ADC_CYCLES - 1.0f) div = ADC_CYCLES - 1.0f;  // can't beat 500 ksps
    adc_set_clkdiv(div);
}

// Stop cleanly and drain. Doing this on EVERY mode change is not paranoia:
// de-interleaving a round-robin capture depends on knowing which channel
// produced the first sample. Leaving a stale sample in the FIFO rotates the
// phase and mislabels everything downstream.
static void adc_quiesce(void) {
    adc_run(false);
    adc_fifo_drain();
    adc_set_round_robin(0);
    // Clear any latched FIFO error.
    adc_fifo_setup(false, false, 0, false, false);
}

// ---------------------------------------------------------------------------

void adc_engine_init(void) {
    adc_init();

    // Only initialise pins that are genuinely analog inputs. GPIO43 and GPIO44
    // are ADC-capable but are driven as digital outputs on this board
    // (RPI5_SHUTDOWN and Threshold_PWM) â€” calling adc_gpio_init() on them would
    // disable their digital drivers and break both features.
    adc_gpio_init(ADC_FIRST_GPIO + ADC_CH_CURRENT);
    adc_gpio_init(ADC_FIRST_GPIO + ADC_CH_5VIN);
    adc_gpio_init(ADC_FIRST_GPIO + ADC_CH_TIA);
    adc_gpio_init(ADC_FIRST_GPIO + ADC_CH_DETECT);
    adc_gpio_init(ADC_FIRST_GPIO + ADC_CH_MIC);

    if (s_dma_chan < 0) s_dma_chan = dma_claim_unused_channel(true);

    adc_quiesce();
    s_mode = ADC_MODE_OFF;
}

void adc_engine_set_mode(adc_mode_t m) {
    if (m == s_mode) return;
    adc_quiesce();

    uint mask;
    uint32_t rate;
    switch (m) {
        case ADC_MODE_IDLE:
            mask = (1u << ADC_CH_5VIN) | (1u << ADC_CH_TIA) |
                   (1u << ADC_CH_DETECT) | (1u << ADC_CH_MIC);
            rate = ADC_MAX_RATE_HZ;   // 125 ksps per channel
            break;
        case ADC_MODE_ARMED:
            mask = (1u << ADC_CH_DETECT) | (1u << ADC_CH_MIC);
            rate = ADC_MAX_RATE_HZ;   // 250 ksps per channel
            break;
        case ADC_MODE_BURST:
            mask = (1u << ADC_CH_CURRENT);
            rate = ADC_MAX_RATE_HZ;   // 500 ksps
            break;
        default:
            s_mode = ADC_MODE_OFF;
            return;
    }

    // Start the sequence on a known channel so the interleave phase is defined.
    adc_select_input(mask_lowest_channel(mask));
    adc_set_round_robin(mask);
    set_rate(rate);
    // err_in_fifo=true: a sample flagged with the error bit tells us the FIFO
    // overran, which we treat as a hard fault rather than letting the channel
    // phase silently rotate.
    adc_fifo_setup(true, false, 0, true, false);
    adc_run(true);

    s_mode = m;
}

adc_mode_t adc_engine_mode(void) { return s_mode; }

// ---------------------------------------------------------------------------

uint16_t adc_read_avg(uint chan, uint n) {
    if (chan > 7 || !((ADC_VALID_MASK >> chan) & 1u)) return 0;
    if (n < 1) n = 1;
    if (n > 4096) n = 4096;

    adc_mode_t prev = s_mode;
    adc_quiesce();
    adc_select_input(chan);
    set_rate(ADC_MAX_RATE_HZ);

    uint32_t sum = 0;
    for (uint i = 0; i < n; i++) sum += adc_read();

    s_mode = ADC_MODE_OFF;
    if (prev != ADC_MODE_OFF) adc_engine_set_mode(prev);

    return (uint16_t)(sum / n);
}

float adc_read_volts(uint chan, uint n) {
    return adc_code_to_volts(adc_read_avg(chan, n));
}

float adc_read_5vin_volts(void) {
    // 256x oversampling: we are discriminating 2.30 V from 2.60 V and this
    // decision gates whether a Pi 5 gets powered. Worth the ~0.5 ms.
    float v_pin = adc_read_volts(ADC_CH_5VIN, 256);
    return v_pin * V5IN_DIVIDER * s_5vin_scale;
}

void  adc_set_5vin_scale(float s) { if (s > 0.5f && s < 2.0f) s_5vin_scale = s; }
float adc_get_5vin_scale(void)    { return s_5vin_scale; }
float adc_default_5vin_scale(void){ return V5IN_SCALE_DEFAULT; }

// ---------------------------------------------------------------------------

size_t adc_capture(uint chan_mask, size_t n_samples, uint32_t rate_hz) {
    chan_mask &= ADC_VALID_MASK;
    if (chan_mask == 0) return 0;
    if (n_samples == 0) return 0;
    if (n_samples > ADC_CAPTURE_MAX_SAMPLES) n_samples = ADC_CAPTURE_MAX_SAMPLES;
    if (s_dma_chan < 0) return 0;

    // Round down to a whole number of round-robin cycles so the caller never
    // gets a partial interleave group at the end.
    uint nch = mask_popcount(chan_mask);
    n_samples -= (n_samples % nch);
    if (n_samples == 0) return 0;

    adc_quiesce();
    adc_select_input(mask_lowest_channel(chan_mask));
    adc_set_round_robin(chan_mask);
    set_rate(rate_hz);
    // dreq_en, threshold 1, err_in_fifo, no 8-bit shift (we want 12 bits).
    adc_fifo_setup(true, true, 1, true, false);

    dma_channel_config c = dma_channel_get_default_config(s_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_ADC);

    dma_channel_configure(s_dma_chan, &c, s_buf, &adc_hw->fifo, n_samples, true);

    adc_run(true);
    dma_channel_wait_for_finish_blocking(s_dma_chan);
    adc_run(false);
    adc_fifo_drain();

    // Bit 15 of a FIFO word is the conversion error flag. Any set bit means the
    // FIFO overran and the round-robin phase is no longer trustworthy.
    s_overran = false;
    for (size_t i = 0; i < n_samples; i++) {
        if (s_buf[i] & 0x8000u) { s_overran = true; }
        s_buf[i] &= 0x0FFFu;
    }
    if (s_overran) fault_raise(FAULT_ADC_OVERRUN);

    s_buf_count = n_samples;
    s_buf_mask  = chan_mask;
    s_buf_rate  = rate_hz;

    s_mode = ADC_MODE_OFF;
    adc_engine_set_mode(ADC_MODE_IDLE);
    return n_samples;
}

const uint16_t *adc_capture_buffer(void) { return s_buf; }
size_t          adc_capture_count(void)  { return s_buf_count; }
uint            adc_capture_mask(void)   { return s_buf_mask; }
uint32_t        adc_capture_rate(void)   { return s_buf_rate; }
bool            adc_capture_overran(void){ return s_overran; }
