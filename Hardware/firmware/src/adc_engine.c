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
// Continuous DMA ring (ARCHITECTURE.md A1)
//
// ONE channel, in RP2350's ENDLESS transfer mode (TRANS_COUNT MODE = 0xf), with
// the hardware write-address ring wrap. The channel never completes, so there is
// no reload to arrange and no periodic gap -- it simply runs from the moment a
// mode is selected until something deliberately stops it.
//
// An A<->B chain was the first attempt and is WRONG here: a channel's transfer
// count does not reload itself, so after each channel had run its one lap both
// counts sit at zero and the pair stalls. Endless mode is RP2350-only; the
// RP2040 equivalent needs a control channel rewriting the count.
//
// The buffer must be aligned to its own size for the ring wrap to work.
// ---------------------------------------------------------------------------
static uint16_t   s_ring[ADC_RING_SAMPLES]
                      __attribute__((aligned(ADC_RING_SAMPLES * 2)));
static int        s_ring_dma = -1;
static bool       s_ring_on;
static uint       s_ring_mask;          // round-robin set currently in the ring
static uint       s_ring_nch;

// Last good +5V_IN reading and when we took it.
static float      s_5vin_last = 0.0f;
static uint32_t   s_5vin_last_ms;
static bool       s_5vin_valid;

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
// Ring control
// ---------------------------------------------------------------------------

static void ring_stop(void) {
    if (s_ring_dma >= 0) dma_channel_abort(s_ring_dma);
    s_ring_on = false;
}

// Start both chained channels. `mask` is the round-robin set already programmed
// into the ADC; we only record it so the de-interleave knows the channel order.
static void ring_start(uint mask) {
    if (s_ring_dma < 0) return;

    s_ring_mask = mask;
    s_ring_nch  = mask_popcount(mask);
    // The ring size must divide evenly by the channel count or the phase
    // rotates on wrap. Enforced by only ever using 1, 2 or 4 channels.
    if (s_ring_nch == 0 || (ADC_RING_SAMPLES % s_ring_nch) != 0) return;

    // log2(bytes) for the DMA write-address wrap.
    uint bytes = ADC_RING_SAMPLES * sizeof(uint16_t);
    uint ring_bits = 0;
    while ((1u << ring_bits) < bytes) ring_bits++;

    dma_channel_config c = dma_channel_get_default_config(s_ring_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, ring_bits);       // wrap the WRITE address
    channel_config_set_dreq(&c, DREQ_ADC);

    dma_channel_set_config(s_ring_dma, &c, false);
    dma_channel_set_read_addr(s_ring_dma, &adc_hw->fifo, false);
    dma_channel_set_write_addr(s_ring_dma, s_ring, false);
    // ENDLESS: the channel never completes, so it never needs re-arming.
    dma_channel_set_trans_count(s_ring_dma, dma_encode_endless_transfer_count(), true);
    s_ring_on = true;
}

bool adc_ring_running(void) { return s_ring_on; }

// Index of the next slot the DMA will write. Everything before it is valid.
static size_t ring_write_index(void) {
    uintptr_t w = (uintptr_t)dma_hw->ch[s_ring_dma].write_addr;
    return ((w - (uintptr_t)s_ring) / sizeof(uint16_t)) & (ADC_RING_SAMPLES - 1);
}

// Position of `chan` within the ascending round-robin order, or -1.
static int ring_slot_of(uint chan) {
    if (!((s_ring_mask >> chan) & 1u)) return -1;
    int slot = 0;
    for (uint c = 0; c < chan; c++) if ((s_ring_mask >> c) & 1u) slot++;
    return slot;
}

// Newest ring index belonging to `chan`.
static bool ring_newest_of(uint chan, size_t *out) {
    int slot = ring_slot_of(chan);
    if (slot < 0) return false;
    size_t i = (ring_write_index() + ADC_RING_SAMPLES - 1) & (ADC_RING_SAMPLES - 1);
    // Walk back at most one full round-robin cycle to land on this channel.
    for (uint k = 0; k < s_ring_nch; k++) {
        if ((i % s_ring_nch) == (size_t)slot) { *out = i; return true; }
        i = (i + ADC_RING_SAMPLES - 1) & (ADC_RING_SAMPLES - 1);
    }
    return false;
}

uint32_t adc_ring_channel_rate_hz(void) {
    // The SAR runs at a fixed ADC_MAX_RATE_HZ aggregate in every mode; the
    // round-robin divides it between channels.
    return s_ring_nch ? (ADC_MAX_RATE_HZ / s_ring_nch) : 0u;
}

size_t adc_ring_depth(void) {
    return s_ring_nch ? (ADC_RING_SAMPLES / s_ring_nch) : 0u;
}

bool adc_ring_view(uint chan, adc_ring_view_t *v) {
    if (!s_ring_on || !v || chan > 7) return false;
    size_t newest;
    if (!ring_newest_of(chan, &newest)) return false;
    v->base   = s_ring;
    v->mask   = ADC_RING_SAMPLES - 1u;
    v->newest = newest;
    v->stride = s_ring_nch;
    v->rate_hz = adc_ring_channel_rate_hz();
    // The REAL write index, not newest+1. ring_newest_of() can walk back up to
    // stride-1 slots to land on this channel, so inferring the write position
    // from `newest` under-counts how far the DMA has gone -- which biases
    // adc_ring_view_valid() toward calling lapped data valid.
    v->w_at_snap = ring_write_index();
    return true;
}

bool adc_ring_view_valid(const adc_ring_view_t *v, size_t oldest_k) {
    if (!v || !s_ring_on) return false;
    // Raw distance from the newest sample back to the oldest one we touched.
    size_t reached = oldest_k * v->stride;
    // Raw distance the writer has advanced since the snapshot.
    size_t advanced = (ring_write_index() - v->w_at_snap) & v->mask;
    // They collide once the two together span the whole ring.
    return (reached + advanced) < ADC_RING_SAMPLES;
}

bool adc_ring_avg(uint chan, uint n, uint16_t *out_code) {
    if (!s_ring_on || chan > 7 || n == 0) return false;
    if (n * s_ring_nch > ADC_RING_SAMPLES) n = ADC_RING_SAMPLES / s_ring_nch;
    size_t i;
    if (!ring_newest_of(chan, &i)) return false;

    uint32_t sum = 0;
    for (uint k = 0; k < n; k++) {
        sum += s_ring[i] & 0x0FFFu;
        i = (i + ADC_RING_SAMPLES - s_ring_nch) & (ADC_RING_SAMPLES - 1);
    }
    if (out_code) *out_code = (uint16_t)(sum / n);
    return true;
}

size_t adc_ring_history(uint chan, uint16_t *dst, size_t n) {
    if (!s_ring_on || !dst || chan > 7 || n == 0) return 0;
    if (n * s_ring_nch > ADC_RING_SAMPLES) n = ADC_RING_SAMPLES / s_ring_nch;
    size_t i;
    if (!ring_newest_of(chan, &i)) return 0;

    // The DMA does not stop while we copy, so a deep read RACES THE WRITER.
    //
    // We walk backwards from the newest sample; the DMA writes forwards from the
    // same point. The two meet at the far side of the ring. Copying the full
    // 8192-sample depth takes roughly 0.3-0.5 ms, in which the DMA advances
    // ~165-275 slots and overwrites the OLDEST region -- which is exactly the
    // tail we are still copying. Those samples come back as fresh data wearing
    // an old timestamp, which is worse than missing data: it is a plausible
    // waveform that never happened.
    //
    // So: note where the writer was, copy, then check how far it got. If it
    // crossed into what we read, report only the part that is provably intact.
    // Callers already treat the return value as the sample count.
    size_t w_before = ring_write_index();

    for (size_t k = 0; k < n; k++) {
        dst[k] = s_ring[i] & 0x0FFFu;            // newest first
        i = (i + ADC_RING_SAMPLES - s_ring_nch) & (ADC_RING_SAMPLES - 1);
    }

    // Raw slots the writer advanced during the copy (all channels interleaved).
    size_t advanced = (ring_write_index() - w_before) & (ADC_RING_SAMPLES - 1);

    // Raw slots we reached back through. Anything within `advanced` of the far
    // end was overwritten mid-copy.
    size_t reached = n * s_ring_nch;
    if (advanced == 0 || reached + advanced <= ADC_RING_SAMPLES) return n;

    size_t lost_raw = reached + advanced - ADC_RING_SAMPLES;
    size_t lost     = (lost_raw + s_ring_nch - 1) / s_ring_nch;   // round up
    return (lost >= n) ? 0 : (n - lost);
}

// ---------------------------------------------------------------------------

void adc_engine_init(void) {
    adc_init();

    // Only initialise pins that are genuinely analog inputs. GPIO43 and GPIO44
    // are ADC-capable but are driven as digital outputs on this board
    // (RPI5_SHUTDOWN and Threshold_PWM) -- calling adc_gpio_init() on them would
    // disable their digital drivers and break both features.
    adc_gpio_init(ADC_FIRST_GPIO + ADC_CH_CURRENT);
    adc_gpio_init(ADC_FIRST_GPIO + ADC_CH_5VIN);
    adc_gpio_init(ADC_FIRST_GPIO + ADC_CH_TIA);
    adc_gpio_init(ADC_FIRST_GPIO + ADC_CH_DETECT);
    adc_gpio_init(ADC_FIRST_GPIO + ADC_CH_MIC);

    if (s_dma_chan < 0) s_dma_chan = dma_claim_unused_channel(true);
    if (s_ring_dma < 0) s_ring_dma = dma_claim_unused_channel(true);

    ring_stop();
    adc_quiesce();
    s_mode = ADC_MODE_OFF;
}

void adc_engine_set_mode(adc_mode_t m) {
    if (m == s_mode) return;
    ring_stop();
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
    // dreq_en=true and threshold 1 so the DMA ring is fed. err_in_fifo=true: a
    // sample flagged with the error bit tells us the FIFO overran, which we
    // treat as a hard fault rather than letting the channel phase rotate.
    adc_fifo_setup(true, true, 1, true, false);

    // Ring BEFORE adc_run(), so the first conversion is captured and the
    // channel phase in the buffer matches the round-robin start.
    ring_start(mask);
    adc_run(true);

    s_mode = m;
}

adc_mode_t adc_engine_mode(void) { return s_mode; }

// ---------------------------------------------------------------------------

// DISRUPTIVE. Stops the ring, polls, restarts. CLI only -- everything in the
// steady-state path should use adc_ring_avg() instead. See ARCHITECTURE.md A1.
uint16_t adc_read_avg(uint chan, uint n) {
    if (chan > 7 || !((ADC_VALID_MASK >> chan) & 1u)) return 0;
    if (n < 1) n = 1;
    if (n > 4096) n = 4096;

    adc_mode_t prev = s_mode;
    ring_stop();
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
    // Read ch1 straight out of the ring -- no conversions commanded, nothing
    // disturbed. The power FSM calls this every 100 ms while the latch is
    // closed, which is precisely why it must not touch the ADC (A1).
    //
    // 256 samples of oversampling: we are discriminating 2.30 V from 2.60 V and
    // the decision gates whether a Pi 5 gets powered.
    uint16_t code;
    if (adc_ring_avg(ADC_CH_5VIN, 256, &code)) {
        s_5vin_last    = adc_code_to_volts(code) * V5IN_DIVIDER * s_5vin_scale;
        s_5vin_last_ms = to_ms_since_boot(get_absolute_time());
        s_5vin_valid   = true;
        return s_5vin_last;
    }

    // ch1 is not in the current round-robin -- ARMED is {5,7}, BURST is {0}.
    // Hold the last good value rather than stopping the ADC to go and get one.
    // adc_5vin_age_ms() tells the caller how stale this is.
    if (s_5vin_valid) return s_5vin_last;

    // Nothing cached yet and the ring cannot supply it (mode OFF, or called
    // before the first IDLE). Fall back to the blocking path exactly once.
    float v = adc_read_volts(ADC_CH_5VIN, 256) * V5IN_DIVIDER * s_5vin_scale;
    s_5vin_last    = v;
    s_5vin_last_ms = to_ms_since_boot(get_absolute_time());
    s_5vin_valid   = true;
    return v;
}

uint32_t adc_5vin_age_ms(void) {
    if (!s_5vin_valid) return UINT32_MAX;
    return to_ms_since_boot(get_absolute_time()) - s_5vin_last_ms;
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

    // Bench command: deliberately disruptive. It takes the ADC for itself, so
    // the ring stops and is restarted by the set_mode(IDLE) at the end.
    ring_stop();

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
