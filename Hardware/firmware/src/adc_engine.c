// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "adc_engine.h"
#include "adc_ring_math.h"
#include "board.h"
#include "dma_safe.h"
#include "safe_state.h"

#include "hardware/adc.h"
#include "hardware/clocks.h"    // clock_get_hz(clk_sys), for adc_cycles_per_sample()
#include "hardware/dma.h"
#include "hardware/structs/systick.h"
#include "hardware/sync.h"      // save_and_disable_interrupts(), for the freeze instant
#include "hardware/xip_cache.h" // adc_ring_probe(): the cold-cache pair
#include "pico/stdlib.h"

#include <string.h>

// The RP2350 ADC clock is 48 MHz and a conversion takes 96 cycles -> 500 ksps,
// reached with DIV = 0 (back to back), not DIV = 95 (see set_rate).
#define ADC_CLK_HZ        48000000.0f
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

// Aligned to its own size because the TRIGGERED capture runs the DMA in ring
// mode over it, and the hardware wraps the write address by masking -- which
// only lands back at s_buf[0] if the buffer starts on a 2^RING_BITS boundary.
// Misalign it and the DMA writes over whatever precedes it in BSS.
static uint16_t   s_buf[ADC_CAPTURE_MAX_SAMPLES]
                  __attribute__((aligned(1u << ADC_CAPTURE_RING_BITS)));
static size_t     s_buf_start;      // where valid data begins (ring wrap)
static size_t     s_trig_off;       // trigger offset, or SIZE_MAX if untriggered
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
static uint64_t   s_ring_t0_us;         // when the current mode's conversions began
static uint32_t   s_ring_gen;           // bumped by every ring_start()

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

// Set the sample rate (adc_ring_math.h, adc_div_for_rate). DIV is 16.8 fixed
// point: a nonzero DIV starts a conversion every (DIV + 1) clk_adc cycles. The maximum is DIV = 0,
// conversions back to back; a nonzero DIV below 96 makes the ADC ignore every
// other trigger and HALVES the rate (2026-10-12).
static void set_rate(uint32_t rate_hz) {
    adc_hw->div = adc_div_for_rate(ADC_CLK_HZ, ADC_MAX_RATE_HZ, rate_hz);
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

// A ring channel whose stop did not complete is never restarted: the datasheet
// calls that unsafe. The ring then stays down, which the 5V_IN freshness check
// (adc_5vin_read) reports as STALLED and the power FSM treats as unreadable.
static bool s_ring_dma_unsafe;

// The bench-capture DMA channel's last stop did not complete: never restart it.
static bool s_capture_dma_unsafe;

// A bench capture (adc_capture / adc_capture_triggered) owns the ADC: the ring is
// deliberately down, so 5V_IN is NOT_SAMPLED rather than STALLED. A triggered
// capture can last 30 s with the power FSM serviced through its poll callback.
static bool s_bench_capture;

static void ring_stop(void) {
    if (s_ring_dma >= 0 && s_ring_on) {
        dma_stop_t st = dma_stop_safely((uint)s_ring_dma);
        if (!st.completed) s_ring_dma_unsafe = true;
    }
    s_ring_on = false;
}

// Start the ring channel. `mask` is the round-robin set already programmed into
// the ADC; we only record it so the de-interleave knows the channel order.
static void ring_start(uint mask) {
    if (s_ring_dma < 0 || s_ring_dma_unsafe) return;

    s_ring_gen++;
    s_ring_mask = mask;
    s_ring_nch  = mask_popcount(mask);
    // The ring size must divide evenly by the channel count or the phase
    // rotates on wrap. Enforced by only ever using 1, 2 or 4 channels.
    if (s_ring_nch == 0 || (ADC_RING_SAMPLES % s_ring_nch) != 0) return;

    // log2(bytes) for the DMA write-address wrap.
    uint bytes = ADC_RING_SAMPLES * sizeof(uint16_t);
    uint ring_bits = 0;
    while ((1u << ring_bits) < bytes) ring_bits++;

    // A READ/WRITE_ERROR from the previous run must not be read as this one's.
    dma_clear_errors((uint)s_ring_dma);
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

// The FIFO's sticky flags, OVER and UNDER, are write-1-to-clear; every other FCS
// field is RW or read-only. Writing the register back with those two bits set
// clears them and rewrites the configuration unchanged.
static void fifo_clear_sticky(void) {
    adc_hw->fcs = adc_hw->fcs | ADC_FCS_OVER_BITS | ADC_FCS_UNDER_BITS;
}

// Index of the next slot the DMA will write. Everything before it is valid.
static size_t ring_write_index(void) {
    uintptr_t w = (uintptr_t)dma_hw->ch[s_ring_dma].write_addr;
    return ((w - (uintptr_t)s_ring) / sizeof(uint16_t)) & (ADC_RING_SAMPLES - 1);
}

// ring_start() points the DMA at s_ring[0], so the write index is the number of
// transfers ISSUED in this mode -- until the first wrap, after which it is
// smaller. Issued, not completed (review round 4): WRITE_ADDR advances when a
// transfer enters the DMA's address FIFOs, and with this ring's address wrapping
// the RP2350 does not adjust it for transfers still in flight, so it can lead
// the completed SRAM writes by those few cycles' worth. Each issue consumes one
// DREQ credit, i.e. one finished conversion, so it never counts a conversion
// that has not happened. Readers that need the WRITES complete use a DMA stop
// first (adc_ring_freeze_copy) or skip the newest group (adc_5vin_read).
size_t adc_ring_written(void) {
    if (!s_ring_on || s_ring_dma < 0) return 0u;
    return ring_write_index();
}

// A transfer is issued every 2 us; each pass of the spin is a few cycles, so
// this many passes is tens of microseconds -- ample for one tick, short enough
// to bound an interrupts-off section when the ring is dead.
#define ADC_SNAP_SPIN_MAX  4000u

// In SRAM, and the loop touches only peripheral registers and locals: no flash
// fetch, no call, nothing that can stall between the readings except what the
// bracket itself measures. The EVENT bracketed is a transfer ISSUE (WRITE_ADDR
// advancing), not a completed write (review round 4). The ordering argument:
//   - t_prev is read BEFORE the previous index read, which still saw no change,
//     so the issue came after t_prev;
//   - t_after is read AFTER the index read that saw the change, so the issue
//     came before t_after.
// Volatile accesses to strongly-ordered peripheral space are issued in program
// order on the M33.
bool __not_in_flash_func(adc_ring_snapshot)(adc_ring_snap_t *s) {
    if (!s || !s_ring_on || s_ring_dma < 0) return false;
    volatile uint32_t *const wa  = (volatile uint32_t *)&dma_hw->ch[s_ring_dma].write_addr;
    volatile uint32_t *const cvr = (volatile uint32_t *)&systick_hw->cvr;
    const uint32_t base = (uint32_t)(uintptr_t)s_ring;

    // Synchronise first: let one change go by, then bracket the NEXT. The change
    // measured is then always caught in a steady pass of this loop, at least one
    // sample period after entry, never in the first pass -- where the bracket
    // would also span the function's entry and any instruction fetch the
    // flash-resident caller left outstanding. Board 1, 2026-10-12 (`adc ringtest`):
    // first-pass brackets of 60-226 cycles against 15 in steady state.
    uint32_t a0 = *wa;
    uint32_t k = 0;
    for (; k < ADC_SNAP_SPIN_MAX; k++) {
        const uint32_t a = *wa;
        if (a != a0) { a0 = a; break; }
    }
    if (k == ADC_SNAP_SPIN_MAX) return false;

    uint32_t t_prev = *cvr;
    for (k = 0; k < ADC_SNAP_SPIN_MAX; k++) {
        const uint32_t t = *cvr;
        const uint32_t a = *wa;
        if (a != a0) {
            const uint32_t t_after = *cvr;
            s->tick_lo = t_prev & 0x00FFFFFFu;
            s->bracket = (t_prev - t_after) & 0x00FFFFFFu;
            s->w       = ((a - base) / sizeof(uint16_t)) & (ADC_RING_SAMPLES - 1u);
            s->gen     = s_ring_gen;
            s->t_us    = time_us_64();      // after the bracket: not part of it
            return true;
        }
        t_prev = t;
    }
    return false;
}

uint32_t adc_cycles_per_sample(void) {
    uint32_t sys = clock_get_hz(clk_sys);
    return (sys % ADC_MAX_RATE_HZ) == 0u ? sys / ADC_MAX_RATE_HZ : 0u;
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

    // SysTick as a free-running 24-bit cycle counter for adc_ring_snapshot():
    // processor clock, no exception. Nothing else in this firmware uses it.
    systick_hw->csr = 0u;
    systick_hw->rvr = 0x00FFFFFFu;
    systick_hw->cvr = 0u;                    // any write clears it; reloads next cycle
    systick_hw->csr = M33_SYST_CSR_CLKSOURCE_BITS | M33_SYST_CSR_ENABLE_BITS;

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
    // So FCS.OVER at a freeze means an overflow in THIS mode (adc_ring_freeze_copy).
    fifo_clear_sticky();

    // Ring BEFORE adc_run(), so the first conversion is captured and the
    // channel phase in the buffer matches the round-robin start.
    ring_start(mask);
    adc_run(true);
    s_ring_t0_us = time_us_64();

    s_mode = m;
}

adc_mode_t adc_engine_mode(void) { return s_mode; }

static uint32_t s_freeze_flags;
uint32_t adc_ring_freeze_flags(void) { return s_freeze_flags; }
uint32_t adc_ring_generation(void)   { return s_ring_gen; }

// See the header. Nothing races the copy because nothing is converting.
size_t adc_ring_freeze_copy(uint chan, uint64_t since_us, uint16_t *dst, size_t cap,
                            bool *overran, uint64_t *stop_us, size_t *written) {
    if (overran) *overran = false;
    if (stop_us) *stop_us = 0;
    if (written) *written = 0;
    if (!s_ring_on || !dst || chan > 7 || cap == 0) {
        s_freeze_flags = ADC_FREEZE_NOT_RUNNING;
        if (overran) *overran = true;
        return 0;
    }

    // The stop instant is what anchors every copied sample in time, so nothing
    // may run between reading the clock and stopping the conversions -- a USB
    // interrupt landing there would shift the caller's baseline/pulse split.
    // CS is read in the same breath: the ADC must still have been free-running
    // (START_MANY, EN) at the instant it was stopped (adc_engine.h, "WHY A
    // FREEZE'S RECORD IS COMPLETE").
    uint32_t flags = 0u;
    uint32_t irq = save_and_disable_interrupts();
    const uint32_t cs = adc_hw->cs;
    uint64_t now = time_us_64();
    adc_run(false);
    restore_interrupts(irq);
    if (stop_us) *stop_us = now;
    if ((cs & ADC_CS_START_MANY_BITS) == 0u || (cs & ADC_CS_EN_BITS) == 0u)
        flags |= ADC_FREEZE_NOT_RUNNING;

    // Then, as the datasheet prescribes after clearing START_MANY (12.4.3.5):
    // wait for READY (the last conversion finished), then for the DMA to take
    // what is left in the FIFO. Both bounded; either failing makes the record
    // unusable.
    busy_wait_us(2);
    for (int k = 0; k < 20 && (adc_hw->cs & ADC_CS_READY_BITS) == 0u; k++) busy_wait_us(1);
    if ((adc_hw->cs & ADC_CS_READY_BITS) == 0u) flags |= ADC_FREEZE_NOT_READY;
    for (int k = 0; k < 50 && adc_fifo_get_level() != 0; k++) busy_wait_us(1);
    if (adc_fifo_get_level() != 0u) flags |= ADC_FREEZE_NOT_DRAINED;

    // Completion barrier (independent review round 5, 2026-10-12). WRITE_ADDR
    // advances when a transfer is ISSUED, and with this ring's address wrapping
    // the RP2350 does not adjust it for transfers still in flight -- so the
    // newest slot's write may not have landed yet. A completed safe stop has
    // flushed every in-flight transfer (they "cannot be revoked"); only then is
    // the index final and every slot behind it written. A stop that did not
    // complete, or a bus error on a read or write, makes the record unusable.
    const dma_stop_t st = dma_stop_safely((uint)s_ring_dma);
    if (!st.completed) { s_ring_dma_unsafe = true; flags |= ADC_FREEZE_STOP_FAILED; }
    s_ring_on = false;
    if (st.read_error || st.write_error) flags |= ADC_FREEZE_DMA_ERROR;
    const uint32_t fcs = adc_hw->fcs;
    if (fcs & ADC_FCS_OVER_BITS)  flags |= ADC_FREEZE_FIFO_OVER;
    if (fcs & ADC_FCS_UNDER_BITS) flags |= ADC_FREEZE_FIFO_UNDER;

    const size_t w = ring_write_index();
    if (written) *written = w;

    size_t got = 0;
    size_t newest;
    if (ring_newest_of(chan, &newest)) {
        // Only what THIS mode wrote. The clock gives an upper bound -- the SAR
        // runs at ADC_MAX_RATE_HZ aggregate, so at most (now - t0) x rate raw
        // slots, less two of margin, never the whole ring. The DMA's write index
        // gives a lower bound. The clock ALONE is not enough: on 2026-10-12 the
        // BURST ring had written 122 slots where the clock said ~218, and the
        // copy returned the previous mode's round-robin as ADC0.
        uint64_t raw = ((now - s_ring_t0_us) * (uint64_t)ADC_MAX_RATE_HZ) / 1000000u;
        raw = raw > 2u ? raw - 2u : 0u;
        if (raw > ADC_RING_SAMPLES - 1u) raw = ADC_RING_SAMPLES - 1u;
        size_t avail = adc_ring_copy_avail(raw, w, s_ring_nch);
        // And only back to since_us.
        uint64_t per_ch_hz = (uint64_t)ADC_MAX_RATE_HZ / s_ring_nch;
        size_t want = since_us < now ? (size_t)(((now - since_us) * per_ch_hz) / 1000000u) : 0u;
        got = want < avail ? want : avail;
        if (got > cap) got = cap;

        // Bit 15 of a FIFO word is that conversion's ERROR flag (FCS.ERR): the
        // result is undefined and must be discarded (12.4.3.4).
        size_t i = newest;
        for (size_t k = 0; k < got; k++) {
            uint16_t v = s_ring[i];
            if (v & 0x8000u) flags |= ADC_FREEZE_CONV_ERROR;
            dst[got - 1u - k] = v & 0x0FFFu;          // oldest first
            i = (i + ADC_RING_SAMPLES - s_ring_nch) & (ADC_RING_SAMPLES - 1);
        }
    }
    s_freeze_flags = flags;
    if (overran) *overran = flags != 0u;

    ring_stop();
    adc_quiesce();
    s_mode = ADC_MODE_OFF;
    return got;
}

// ---------------------------------------------------------------------------
// `adc ringtest`. The switch live mode makes before every firing, watched from
// outside: IDLE -> BURST, then poll the DMA's write index for a fixed window.
// Board 1 (2026-10-12) showed ~200 us of BURST with nothing written; this says
// whether that is the switch itself, and how long it lasts.
// ---------------------------------------------------------------------------
static const uint32_t k_probe_at_us[ADC_PROBE_POINTS] = { 0u, 10u, 25u, 50u, 100u, 200u, 400u, 800u };

bool adc_ring_probe(adc_ring_probe_t *p) {
    if (!p) return false;
    memset(p, 0, sizeof(*p));
    p->first_us = p->ready_us = UINT32_MAX;
    if (s_mode != ADC_MODE_IDLE || !s_ring_on || s_ring_dma < 0) return false;

    uint64_t t_call = time_us_64();
    adc_engine_set_mode(ADC_MODE_BURST);
    uint64_t t0 = time_us_64();
    p->switch_us = (uint32_t)(t0 - t_call);

    uint32_t next = 0;
    for (;;) {
        uint32_t dt = (uint32_t)(time_us_64() - t0);
        size_t   w  = ring_write_index();
        if (w > 0u && p->first_us == UINT32_MAX) p->first_us = dt;
        if (w >= STROBE_LIVE_RING_READY_SAMPLES && p->ready_us == UINT32_MAX) p->ready_us = dt;
        while (next < ADC_PROBE_POINTS && dt >= k_probe_at_us[next]) {
            p->at_us[next] = dt;            // when it was actually read
            p->written_at[next] = w;
            next++;
        }
        if (dt >= ADC_PROBE_WINDOW_US) { p->written_end = w; p->window_us = dt; break; }
    }
    p->over = (adc_hw->fcs & ADC_FCS_OVER_BITS) != 0u;
    for (size_t k = 0; k < ADC_PROBE_HEAD; k++) {
        p->head[k] = s_ring[k] & 0x0FFFu;
        if (s_ring[k] & 0x8000u) p->conv_err = true;
    }

    // The live coverage relation, measured with nothing fired: pairs of snapshots
    // taken exactly as run_live() takes them, ADC_PROBE_PAIR_US apart -- warm,
    // then with the XIP cache invalidated just before the first one.
    for (int c = 0; c < 2; c++) {
        adc_ring_pair_t *q = &p->pair[c];
        adc_ring_snap_t a, b;
        if (c == 1) xip_cache_invalidate_all();
        uint32_t irq = save_and_disable_interrupts();
        bool ok_a = adc_ring_snapshot(&a);
        restore_interrupts(irq);
        busy_wait_us(ADC_PROBE_PAIR_US);
        irq = save_and_disable_interrupts();
        bool ok_b = adc_ring_snapshot(&b);
        restore_interrupts(irq);
        q->cps = adc_cycles_per_sample();
        q->ok  = ok_a && ok_b && a.gen == b.gen && b.w >= a.w;
        if (q->ok) {
            q->samples = b.w - a.w;
            q->span_lo = adc_systick_elapsed(a.tick_lo, b.tick_lo);
            q->b1      = a.bracket;
            q->b2      = b.bracket;
        }
    }

    adc_engine_set_mode(ADC_MODE_IDLE);
    return true;
}

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

static void v5in_record(float v) {
    s_5vin_last    = v;
    s_5vin_last_ms = to_ms_since_boot(get_absolute_time());
    s_5vin_valid   = true;
}

// One reading's ch1 samples, ALL of them written during the call that takes
// them, and how long that may take before the ring is called stalled. 128 at
// 125 ksps per channel is ~1 ms of ring.
#define V5IN_FRESH_SAMPLES    128u
#define V5IN_FRESH_TIMEOUT_US 3000u

adc_5vin_status_t adc_5vin_read(float *volts) {
    if (volts) *volts = s_5vin_last;          // the last good value, for display

    // A bench capture owns the ADC: deliberately not sampling ch1.
    if (s_bench_capture) return ADC_5VIN_NOT_SAMPLED;

    // No ring at all (boot, or between modes): a blocking read is fresh by
    // construction -- it commands its own conversions.
    // A conversion error in any of its samples (the sticky CS.ERR_STICKY, cleared
    // first: write-1-to-clear, START_ONCE masked so nothing is started) voids it.
    if (s_mode == ADC_MODE_OFF) {
        adc_hw->cs = (adc_hw->cs & ~ADC_CS_START_ONCE_BITS) | ADC_CS_ERR_STICKY_BITS;
        float v = adc_read_volts(ADC_CH_5VIN, 256) * V5IN_DIVIDER * s_5vin_scale;
        if (adc_hw->cs & ADC_CS_ERR_STICKY_BITS) return ADC_5VIN_INVALID;
        v5in_record(v);
        if (volts) *volts = v;
        return ADC_5VIN_FRESH;
    }
    // A mode is selected but its ring is not running (a DMA stop that did not
    // complete leaves it down): nothing is being converted into the ring.
    if (!s_ring_on) return ADC_5VIN_STALLED;
    // ch1 is not in this mode's round-robin -- ARMED is {5,7}, BURST is {0}.
    const int slot = ring_slot_of(ADC_CH_5VIN);
    if (slot < 0) return ADC_5VIN_NOT_SAMPLED;

    // Liveness and freshness in one (independent review round 5, 2026-10-12):
    // wait until the DMA has issued enough slots DURING THIS CALL, then average
    // only those. A ring that stopped -- for whatever reason -- times out here
    // instead of handing back old samples, and nothing from before a mode switch
    // can be included. Two groups of margin: the walk back to ch1's slot, and the
    // newest issued write, which may still be in flight.
    const size_t   nch  = s_ring_nch;
    const size_t   need = ((size_t)V5IN_FRESH_SAMPLES + 2u) * nch;
    const size_t   mask = ADC_RING_SAMPLES - 1u;
    const size_t   w0   = ring_write_index();
    const uint64_t t0   = time_us_64();
    size_t w;
    for (;;) {
        w = ring_write_index();
        if (((w - w0) & mask) >= need) break;
        if (time_us_64() - t0 > V5IN_FRESH_TIMEOUT_US) return ADC_5VIN_STALLED;
    }

    // Integrity (review round 6): a FIFO overflow since the ring started has LOST
    // conversions, so the round-robin may have rotated and "ch1's slots" may hold
    // another channel; an underflow or a DMA bus error makes the ring untrusted
    // too. Read the sticky flags AFTER the samples were issued, so they cover them.
    const uint32_t fcs  = adc_hw->fcs;
    const uint32_t ctrl = dma_hw->ch[s_ring_dma].ctrl_trig;
    const bool hw_bad =
        (fcs & (ADC_FCS_OVER_BITS | ADC_FCS_UNDER_BITS)) != 0u ||
        (ctrl & (DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS)) != 0u;

    uint16_t code = 0;
    const adc_5vin_status_t st = adc_5vin_from_ring(s_ring, mask, w, (unsigned)nch, (unsigned)slot,
                                                    V5IN_FRESH_SAMPLES, hw_bad, &code);
    if (st != ADC_5VIN_FRESH) {
        // Re-establish the channel sequence -- a restart clears the sticky flags
        // and starts the round-robin from a known channel -- so a later reading
        // can be trusted. This one is not: the caller sees INVALID.
        if (hw_bad) {
            const adc_mode_t m = s_mode;
            s_mode = ADC_MODE_OFF;
            adc_engine_set_mode(m);
        }
        return st;
    }
    const float v = adc_code_to_volts(code) * V5IN_DIVIDER * s_5vin_scale;
    v5in_record(v);
    if (volts) *volts = v;
    return ADC_5VIN_FRESH;
}

float adc_read_5vin_volts(void) {
    float v = 0.0f;
    (void)adc_5vin_read(&v);
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
    if (s_dma_chan < 0 || s_capture_dma_unsafe) return 0;

    // Round down to a whole number of round-robin cycles so the caller never
    // gets a partial interleave group at the end. Checked BEFORE the ring is
    // stopped, so an early return cannot leave it down.
    uint nch = mask_popcount(chan_mask);
    n_samples -= (n_samples % nch);
    if (n_samples == 0) return 0;

    // Bench command: deliberately disruptive. It takes the ADC for itself, so
    // the ring stops and is restarted by the set_mode(IDLE) at the end. While
    // it owns the ADC, 5V_IN is NOT_SAMPLED, not STALLED (adc_5vin_read).
    ring_stop();
    s_bench_capture = true;

    adc_quiesce();
    adc_select_input(mask_lowest_channel(chan_mask));
    adc_set_round_robin(chan_mask);
    set_rate(rate_hz);
    // dreq_en, threshold 1, err_in_fifo, no 8-bit shift (we want 12 bits).
    adc_fifo_setup(true, true, 1, true, false);
    fifo_clear_sticky();
    dma_clear_errors((uint)s_dma_chan);

    dma_channel_config c = dma_channel_get_default_config(s_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_ADC);

    dma_channel_configure(s_dma_chan, &c, s_buf, &adc_hw->fifo, n_samples, true);

    adc_run(true);
    // Wait for the DMA, watching the FIFO's sticky flags WHILE it still needs
    // samples: once it has its count the ADC keeps converting into a FIFO nobody
    // drains, and an OVER set after that is not a loss in the capture. Busy and
    // FCS are read together, interrupts off, so "busy, then OVER" cannot be
    // confused with "done, then OVER" (eight more conversions, >= 16 us).
    bool lost = false;
    for (;;) {
        uint32_t irq = save_and_disable_interrupts();
        const bool     busy = dma_channel_is_busy(s_dma_chan);
        const uint32_t f    = adc_hw->fcs;
        restore_interrupts(irq);
        if (f & (ADC_FCS_OVER_BITS | ADC_FCS_UNDER_BITS)) lost = true;
        if (!busy) break;
    }
    adc_run(false);
    adc_fifo_drain();
    const uint32_t ctrl = dma_channel_hw_addr(s_dma_chan)->ctrl_trig;
    if (ctrl & (DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS)) lost = true;

    // "Overran" = the capture cannot be trusted: conversions lost to a FIFO
    // overflow (which also rotates a round-robin's phase), a read of an empty
    // FIFO, a DMA bus error, or a sample carrying the conversion-error bit (bit
    // 15: an undefined result). Bit 15 alone used to be read as "overrun"; it is
    // not one (review round 2), and a real overflow went unflagged.
    s_overran = lost;
    for (size_t i = 0; i < n_samples; i++) {
        if (s_buf[i] & 0x8000u) { s_overran = true; }
        s_buf[i] &= 0x0FFFu;
    }
    if (s_overran) fault_raise(FAULT_ADC_OVERRUN);

    s_buf_count = n_samples;
    s_buf_mask  = chan_mask;
    s_buf_rate  = rate_hz;
    s_buf_start = 0;            // linear fill: the same modulo walk still works
    s_trig_off  = SIZE_MAX;     // "this one was not triggered"

    s_bench_capture = false;
    s_mode = ADC_MODE_OFF;
    adc_engine_set_mode(ADC_MODE_IDLE);
    return n_samples;
}

// ---------------------------------------------------------------------------
// Triggered capture. See the long note in adc_engine.h for why this has to run
// the DMA continuously rather than starting it when the signal gets loud.
// ---------------------------------------------------------------------------

adc_trig_result_t adc_capture_triggered(uint chan, size_t n_samples,
                                        uint32_t rate_hz, uint16_t threshold,
                                        float pre_frac, uint32_t timeout_ms,
                                        adc_wait_poll_fn poll,
                                        uint16_t *out_base, size_t *out_trig) {
    if (chan > 7 || !((ADC_VALID_MASK >> chan) & 1u)) return ADC_TRIG_ERROR;
    if (s_dma_chan < 0 || s_capture_dma_unsafe) return ADC_TRIG_ERROR;
    if (n_samples == 0 || n_samples > ADC_CAPTURE_MAX_SAMPLES)
        n_samples = ADC_CAPTURE_MAX_SAMPLES;
    if (pre_frac < 0.0f) pre_frac = 0.0f;
    if (pre_frac > 0.9f) pre_frac = 0.9f;

    size_t pre  = (size_t)(n_samples * pre_frac);
    size_t post = n_samples - pre;
    if (post == 0) return ADC_TRIG_ERROR;

    // Baseline BEFORE arming. Taking it from the ring afterwards would race the
    // wrap -- at 500 ksps the whole buffer is only 32.8 ms deep.
    uint16_t base = adc_read_avg(chan, 256);
    if (out_base) *out_base = base;

    // Up to timeout_ms with the ADC taken -- and poll() services the power FSM
    // meanwhile -- so 5V_IN must read NOT_SAMPLED, not STALLED (adc_5vin_read).
    ring_stop();
    s_bench_capture = true;
    adc_quiesce();
    adc_select_input(chan);
    adc_set_round_robin(0);          // single channel: no interleave
    set_rate(rate_hz);
    adc_fifo_setup(true, true, 1, true, false);
    fifo_clear_sticky();
    dma_clear_errors((uint)s_dma_chan);

    dma_channel_config c = dma_channel_get_default_config(s_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_ADC);
    // Wrap the WRITE address over the whole buffer. s_buf is aligned to its own
    // size for exactly this reason.
    channel_config_set_ring(&c, true, ADC_CAPTURE_RING_BITS);

    // Run "forever" and stop it ourselves. 0x0FFFFFFF samples is 9 minutes at
    // 500 ksps; the timeout below fires long before that.
    const uint32_t COUNT0 = 0x0FFFFFFFu;
    dma_channel_configure(s_dma_chan, &c, s_buf, &adc_hw->fifo, COUNT0, true);
    adc_run(true);

    const size_t      CAP     = ADC_CAPTURE_MAX_SAMPLES;
    uint32_t          t0      = to_ms_since_boot(get_absolute_time());
    uint64_t          scanned = 0;        // absolute sample index already checked
    uint64_t          trig    = UINT64_MAX;
    adc_trig_result_t res     = ADC_TRIG_TIMEOUT;

    for (;;) {
        uint64_t written = COUNT0 - dma_channel_hw_addr(s_dma_chan)->transfer_count;

        if (trig == UINT64_MAX) {
            // Never scan a sample the DMA has already overwritten. If we fell a
            // whole buffer behind, that history is gone -- skip forward rather
            // than trigger on data that is no longer there.
            if (written > CAP && scanned < written - CAP) scanned = written - CAP;

            for (uint64_t i = scanned; i < written; i++) {
                uint16_t v = s_buf[i % CAP] & 0x0FFFu;
                int      d = (int)v - (int)base;
                if (d < 0) d = -d;
                if (d >= (int)threshold) { trig = i; break; }
            }
            scanned = written;

            // Do not arm until the pre-trigger history actually exists, or a
            // sound in the first few ms returns a buffer padded with whatever
            // was in SRAM.
            if (trig != UINT64_MAX && trig < pre) trig = UINT64_MAX;
        }

        if (trig != UINT64_MAX && written >= trig + post) { res = ADC_TRIG_OK; break; }
        if (to_ms_since_boot(get_absolute_time()) - t0 > timeout_ms) break;
        if (poll && poll()) { res = ADC_TRIG_ABORTED; break; }
    }

    // The FIFO's sticky flags BEFORE the DMA stops: after it, the ADC fills a FIFO
    // nobody drains and sets OVER without any loss in the capture.
    const uint32_t fcs_run = adc_hw->fcs;
    const dma_stop_t st = dma_stop_safely((uint)s_dma_chan);   // RP2350-E5 sequence
    adc_run(false);
    adc_fifo_drain();

    // An unfinished stop leaves the channel -- and so the buffer -- in an unknown
    // state: do not process the buffer, report an error, and never restart that
    // channel (review round 6). A bus error voids the capture too.
    if (!st.completed) {
        s_capture_dma_unsafe = true;
        res = ADC_TRIG_ERROR;
    } else if (st.read_error || st.write_error) {
        res = ADC_TRIG_ERROR;
    }

    if (res == ADC_TRIG_OK) {
        s_buf_start = (size_t)((trig - pre) % CAP);
        s_buf_count = pre + post;
        s_trig_off  = pre;
        if (out_trig) *out_trig = pre;

        // As adc_capture(): lost conversions, a phantom read, or a conversion
        // error anywhere in the buffer.
        s_overran = (fcs_run & (ADC_FCS_OVER_BITS | ADC_FCS_UNDER_BITS)) != 0u;
        for (size_t i = 0; i < CAP; i++) {
            if (s_buf[i] & 0x8000u) s_overran = true;
            s_buf[i] &= 0x0FFFu;
        }
        if (s_overran) fault_raise(FAULT_ADC_OVERRUN);

        s_buf_mask = (1u << chan);
        s_buf_rate = rate_hz;
    }

    s_bench_capture = false;
    s_mode = ADC_MODE_OFF;
    adc_engine_set_mode(ADC_MODE_IDLE);
    return res;
}

const uint16_t *adc_capture_buffer(void) { return s_buf; }
size_t          adc_capture_count(void)  { return s_buf_count; }
uint            adc_capture_mask(void)   { return s_buf_mask; }
uint32_t        adc_capture_rate(void)   { return s_buf_rate; }
size_t          adc_capture_start(void)  { return s_buf_start; }
size_t          adc_capture_trig_offset(void) { return s_trig_off; }
bool            adc_capture_overran(void){ return s_overran; }
