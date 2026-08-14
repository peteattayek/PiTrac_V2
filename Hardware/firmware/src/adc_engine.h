// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// adc_engine.h â€” the RP2350 has ONE SAR at 500 ksps aggregate. Everything that
// wants analog has to share it, so allocation is explicit rather than ad hoc.
//
// Modes (this resolves an ambiguity the .md leaves open â€” its "Mode A"
// round-robin over ch1/ch2/ch5/ch7 is wrong for the armed state, because the
// detect signal and the mic both want to free-run):
//
//   IDLE  : round-robin {5V_IN, TIA, DETECT, MIC} @ 125 ksps each. Health only.
//   ARMED : round-robin {DETECT, MIC}             @ 250 ksps each.
//           DETECT: LPF is 15.9 kHz -> 8x oversampled.
//           MIC:    band-pass to 24 kHz -> 10x oversampled.
//   BURST : {CURRENT} only                        @ 500 ksps.
//           Per-pulse strobe current plateaus across the burst window.

#ifndef PITRAC_ADC_ENGINE_H
#define PITRAC_ADC_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// NOTE: this header deliberately uses plain `unsigned` rather than the SDK's
// `uint` typedef, so it stays self-contained and can be included before any
// pico headers. `uint` IS `unsigned int`, so the .c file's definitions still
// match these declarations exactly.

typedef enum {
    ADC_MODE_OFF = 0,
    ADC_MODE_IDLE,
    ADC_MODE_ARMED,
    ADC_MODE_BURST,
} adc_mode_t;

void       adc_engine_init(void);
void       adc_engine_set_mode(adc_mode_t m);
adc_mode_t adc_engine_mode(void);

// ---------------------------------------------------------------------------
// CONTINUOUS DMA RING  (ARCHITECTURE.md A1)
//
// IDLE / ARMED / BURST all free-run into this ring via DMA, forever. Nothing in
// the steady-state path ever stops the ADC, so the round-robin channel phase is
// never rotated and no samples are ever lost.
//
// This replaces the old arrangement where adc_read_avg() quiesced the ADC on
// every call and the power FSM's supply monitor called it 10x a second. That was
// survivable while nothing else used the ADC; it would have punched holes in the
// Phase 3 pre-trigger history.
//
// Size: 16384 samples = 32 KB, and it MUST stay a power of two -- the DMA
// hardware ring wraps on a 2^n byte boundary. It is also why the round-robin
// channel count is always 1, 2 or 4: the ring size must be a whole multiple of
// the channel count, or the channel phase rotates on every wrap and every
// de-interleaved sample after that is mislabelled.
//
//   IDLE  4 ch @ 125 ksps -> 4096 samples/ch = 32.8 ms of history per channel
//   ARMED 2 ch @ 250 ksps -> 8192 samples/ch = 32.8 ms per channel
//   BURST 1 ch @ 500 ksps -> 16384 samples   = 32.8 ms
//
// 32.8 ms covers a ball transit down to about 1.4 m/s, which spans the whole
// production range (v_min 2.0 m/s). Slower bench balls need `capture`.
// ---------------------------------------------------------------------------

#define ADC_RING_SAMPLES  16384u          // power of two, see above

// Average the most recent `n` samples of `chan` that are ALREADY in the ring.
// Costs no conversions and disturbs nothing.
//
// Returns false if the ring is not running, or if `chan` is not part of the
// current mode's round-robin set -- e.g. ch1 while ARMED, which is {5,7}. The
// caller decides what to do about that; adc_read_5vin_volts() holds its last
// good value.
bool adc_ring_avg(unsigned chan, unsigned n, uint16_t *out_code);

// Copy the most recent `n` samples of `chan` out of the ring, newest first.
// This is the Phase 3 pre-trigger history: after a comparator edge, pull the
// bump back out of the ring and find its own 50 % crossings.
//
// RETURNS THE NUMBER OF SAMPLES THAT ARE ACTUALLY VALID, which may be less than
// `n`. The DMA keeps writing while this copies, and a deep read races it: we
// walk backwards from the newest sample, the writer walks forwards from the same
// place, and on a full-depth read they meet. This function detects that and
// reports only the prefix it can vouch for -- so a short return means "the ring
// lapped me", not an error.
//
// ALWAYS ASK FOR THE DEPTH YOU NEED, NOT THE MAXIMUM. A full 8192-sample read in
// ARMED takes ~0.3-0.5 ms and reliably loses its tail; a few hundred samples is
// never at risk. For a transit, size the request from the measured transit time
// plus margin.
size_t adc_ring_history(unsigned chan, uint16_t *dst, size_t n);

bool adc_ring_running(void);

// ---------------------------------------------------------------------------
// ZERO-COPY RING ACCESS
//
// adc_ring_history() copies, and copying is too slow for the firing path: 8192
// samples of strided modular indexing costs several hundred microseconds, and
// the whole point of doing the ADC refinement during the camera handshake is
// that it has to fit inside a 100-300 us window.
//
// This is the same data with no copy at all -- an O(1) snapshot of where the
// ring currently is, plus an inline accessor. Analysis reads samples in place.
//
// THE SNAPSHOT GOES STALE. The DMA keeps writing, so a view taken now describes
// a ring that is being overwritten from the oldest end. Call
// adc_ring_view_valid() with the oldest index you actually touched, AFTER the
// analysis, and discard the result if it returns false. That is the honest
// guard; there is no way to make the read atomic.
// ---------------------------------------------------------------------------

typedef struct {
    const uint16_t *base;
    size_t          mask;      // ADC_RING_SAMPLES - 1
    size_t          newest;    // raw ring index of the newest sample of `chan`
    unsigned        stride;    // round-robin channel count
    uint32_t        rate_hz;   // PER-CHANNEL sample rate in the current mode
} adc_ring_view_t;

// false if the ring is stopped or `chan` is not in the current round-robin set.
bool adc_ring_view(unsigned chan, adc_ring_view_t *v);

// k = 0 is the newest sample of the channel; k increases backwards in time.
static inline uint16_t adc_ring_view_at(const adc_ring_view_t *v, size_t k) {
    return v->base[(v->newest - k * v->stride) & v->mask] & 0x0FFFu;
}

// Has the DMA lapped far enough to have overwritten sample `oldest_k`?
bool adc_ring_view_valid(const adc_ring_view_t *v, size_t oldest_k);

// Per-channel sample rate for the current mode: 125k IDLE, 250k ARMED, 500k
// BURST. Recorded per pass, because a pass captured in IDLE and one captured in
// ARMED have different time bases and the host must not have to guess.
uint32_t adc_ring_channel_rate_hz(void);

// Samples of `chan` currently held. ADC_RING_SAMPLES / stride.
size_t adc_ring_depth(void);

// ---------------------------------------------------------------------------
// One-shot blocking reads. These STOP the ring, take the sample(s), and restart
// it -- so they are disruptive by construction. Fine for the CLI's `adc <ch>`;
// never call them from the armed or firing path.
// ---------------------------------------------------------------------------

// Oversampled mean of one channel. n is clamped to [1, 4096].
uint16_t adc_read_avg(unsigned chan, unsigned n);

// Same, converted to volts at the pin (does NOT undo any divider).
float    adc_read_volts(unsigned chan, unsigned n);

// +5V_IN in volts, undoing the R46/R47 divider and applying cal.adc1_scale.
// This is the discriminator that decides whether we are allowed to close the
// +5V latch: ~2.30-2.35 V at the pin on USB-C power vs ~2.60 V on the Meanwell.
//
// NON-DISRUPTIVE since the A1 fix: it averages ch1 samples already sitting in
// the ring and commands no conversions of its own. The power FSM calls this
// every 100 ms while latched, which is exactly why it must not touch the ADC.
//
// ch1 is NOT in the ARMED round-robin ({5,7}), so while armed this returns the
// last good value. That is deliberate -- see adc_5vin_age_ms(). The natural
// state flow re-checks it after every shot, because FIRING drops to BURST and
// then back to IDLE, and IDLE is {1,2,5,7}.
float    adc_read_5vin_volts(void);

// Milliseconds since adc_read_5vin_volts() last got a genuinely fresh reading.
// 0 means fresh. Grows while ARMED or BURST, where ch1 is not being sampled.
// The CLI shows it so a stale reading is never mistaken for a live one.
uint32_t adc_5vin_age_ms(void);

// Trim for the +5V_IN read path. The default is NOT 1.0 -- it is 1.063, because
// R46/R47 present 50 kOhm to an ADC that wants <=10 kOhm and the raw reading comes
// back ~5.9% low (measured: true 5.200 V reads as 4.893 V). Without that default
// the firmware mistakes a good bench supply for USB power and refuses to latch.
// `adc5vcal` trims per-unit on top. See adc_engine.c for the full note.
void     adc_set_5vin_scale(float s);
float    adc_get_5vin_scale(void);
float    adc_default_5vin_scale(void);

// ---------------------------------------------------------------------------
// Block capture â€” the bench instrument.
//
// Free-runs `chan_mask` into a RAM buffer via DMA, then hands it back for the
// CLI to dump as CSV. With tools/scope.py on the other end this turns the board
// into a logging DSO, which is what makes phases 3-5 tractable.
//
// If chan_mask has more than one bit set, samples are round-robin interleaved
// in ascending channel order starting at the lowest channel in the mask.
// ---------------------------------------------------------------------------

#define ADC_CAPTURE_MAX_SAMPLES 16384   // 32 KB of SRAM

// Returns the number of samples captured, or 0 on error (bad mask, bad rate).
// Blocking. Restores ADC_MODE_IDLE afterwards.
size_t adc_capture(unsigned chan_mask, size_t n_samples, uint32_t rate_hz);

// Access the buffer filled by the last adc_capture().
const uint16_t *adc_capture_buffer(void);
size_t          adc_capture_count(void);
unsigned        adc_capture_mask(void);
uint32_t        adc_capture_rate(void);

// True if the ADC FIFO overran during the last capture. An overrun rotates the
// round-robin channel phase, which silently mislabels every subsequent sample â€”
// so this is a hard error, not a warning.
bool adc_capture_overran(void);

// Convert a raw code to volts at the pin.
static inline float adc_code_to_volts(uint16_t code) {
    return (float)code * (3.3f / 4095.0f);
}

#endif // PITRAC_ADC_ENGINE_H
