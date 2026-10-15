// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// adc_engine.h -- the RP2350 has ONE SAR at 500 ksps aggregate. Everything that
// wants analog has to share it, so allocation is explicit rather than ad hoc.
//
// Modes (this resolves an ambiguity the .md leaves open -- its "Mode A"
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
// FREEZE AND COPY -- the strobe's per-firing readback (6c).
//
// Stops the conversions, lets the one in flight land, and copies every sample of
// `chan` taken from `since_us` up to the stop, OLDEST FIRST, into dst (at most
// `cap`). Nothing races the copy, which is the point: a 15,000-sample
// adc_ring_history() at 500 ksps would lose its tail to the writer.
//
// *stop_us (may be NULL) is the instant the conversions were stopped, read with
// interrupts disabled. The newest sample is within one conversion period of it,
// so it -- not a clock read by the caller beforehand -- is what anchors the
// samples in time (review finding 2026-10-07: a USB interrupt between a caller's
// clock read and the stop shifted a baseline/pulse split by its own length).
//
// Only samples written since the CURRENT MODE STARTED are returned -- beyond
// that the ring still holds the previous mode's interleave, which would decode
// as a plausible waveform that never happened. So the return can be short.
//
// "Written since the current mode started" is bounded twice: by the clock
// (elapsed x rate, an upper bound) AND by the DMA's own write index (a lower
// bound). On board 1, 2026-10-12, the BURST ring had written 122 samples where
// the clock said ~218; the clock alone returned 86 samples of the previous
// mode's round-robin as ADC0 (adc_ring_math.h).
//
// LEAVES THE ENGINE OFF (adc_engine_mode() == ADC_MODE_OFF): the caller restores
// what it wants with adc_engine_set_mode(). *overran (may be NULL) is set if the
// FIFO overflowed in this mode (FCS.OVER) or any copied sample carried its
// conversion-error flag (bit 15). *written (may be NULL) is the ring's write
// index at the stop: raw slots written in this mode, the newest copied sample
// being the last of them.
// ---------------------------------------------------------------------------
size_t adc_ring_freeze_copy(unsigned chan, uint64_t since_us, uint16_t *dst, size_t cap,
                            bool *overran, uint64_t *stop_us, size_t *written);

// WHY A FREEZE'S RECORD IS COMPLETE (review round 6). No conversion is missing
// from the ring when ALL of these hold, by the datasheet alone, with no timing
// argument: the ADC converts at regular intervals while CS.START_MANY is set
// (12.4.3.2) and only this firmware clears it; a conversion that finds the FIFO
// full is lost AND sets the sticky FCS.OVER (12.4.3.4); a DMA read of an empty
// FIFO sets FCS.UNDER; a failed bus transfer sets READ/WRITE_ERROR. So:
//   START_MANY and EN still set at the stop instant, READY after it, the FIFO
//   drained by the DMA, the DMA stop completed, no READ/WRITE_ERROR, no OVER, no
//   UNDER, and (the caller's part) the ring not restarted since the window began.
// Any failure sets a flag below and *overran; adc_ring_freeze_flags() says which.
#define ADC_FREEZE_NOT_RUNNING  (1u << 0)   // START_MANY or EN was clear at the stop
#define ADC_FREEZE_NOT_READY    (1u << 1)   // READY did not come back after the stop
#define ADC_FREEZE_NOT_DRAINED  (1u << 2)   // the FIFO did not empty
#define ADC_FREEZE_STOP_FAILED  (1u << 3)   // the DMA stop did not complete
#define ADC_FREEZE_DMA_ERROR    (1u << 4)   // READ_ERROR or WRITE_ERROR on the ring
#define ADC_FREEZE_FIFO_OVER    (1u << 5)   // conversions were lost
#define ADC_FREEZE_FIFO_UNDER   (1u << 6)   // a read of an empty FIFO
#define ADC_FREEZE_CONV_ERROR   (1u << 7)   // a copied sample carries the conversion-error bit
uint32_t adc_ring_freeze_flags(void);

// The ring's generation: changes whenever the ring is restarted.
uint32_t adc_ring_generation(void);

// Ring transfers ISSUED since the current mode started, from the DMA's write
// index: exact until the ring's first wrap (32.8 ms), smaller after it. Issued,
// not completed (review round 4): the newest few writes may still be in flight,
// but each issue is one finished conversion. 0 if the ring is not running.
// Cheap; safe to poll.
size_t adc_ring_written(void);

// A BRACKETED snapshot of a ring transfer being ISSUED -- the DMA write index
// advancing, which happens when the transfer enters the address FIFOs, not when
// its SRAM write completes (review round 4; datasheet 12.6.4). Runs from SRAM (no
// flash fetch can stall it). Each pass reads SysTick then the write index; when
// the index changes, one more SysTick reading closes the bracket. The issue then
// lies between tick_lo and tick_lo - bracket (SysTick counts down) -- a MEASURED
// uncertainty, a few SRAM instructions wide, and anything that widens it
// (contention, a stall) shows in `bracket` and is rejected by
// strobe_live_coverage_ok(). Round 3: the first version ran from flash and
// assumed its latency.
//
// THE CALLER DISABLES INTERRUPTS, and keeps whatever must follow the snapshot
// -- the strobe's PIO enable -- in the same section. false if the index did not
// tick within a bounded spin (~tens of us): the ring is not acquiring.
typedef struct {
    size_t   w;        // write index just after the issue
    uint32_t tick_lo;  // SysTick (24-bit, counts DOWN at clk_sys) read before the issue
    uint32_t bracket;  // cycles from tick_lo to the reading after the issue
    uint64_t t_us;     // time_us_64(), read after the bracket closed
    uint32_t gen;      // ring generation: changes whenever the ring is restarted
} adc_ring_snap_t;

bool adc_ring_snapshot(adc_ring_snap_t *s);

// clk_sys cycles per ADC sample (300 at 150 MHz), or 0 if clk_sys is not an
// exact multiple of the ADC's 500 ksps -- then no coverage check can pass.
uint32_t adc_cycles_per_sample(void);

// ---------------------------------------------------------------------------
// `adc ringtest` -- the IDLE -> BURST switch a live firing makes, observed:
// poll the write index for ADC_PROBE_WINDOW_US, then back to IDLE. Fires
// nothing. false (and nothing done) unless the engine is in IDLE.
// ---------------------------------------------------------------------------
#define ADC_PROBE_POINTS     8
#define ADC_PROBE_HEAD       8
#define ADC_PROBE_WINDOW_US  1000u

typedef struct {
    bool     ok;          // both snapshots taken, same generation, index forward
    size_t   samples;     // transfers issued between the two snapshots
    uint32_t span_lo;     // cycles between the brackets' earlier readings
    uint32_t b1, b2;      // the two brackets
    uint32_t cps;         // adc_cycles_per_sample()
} adc_ring_pair_t;

typedef struct {
    uint32_t switch_us;                     // adc_engine_set_mode(BURST) took this long
    uint32_t first_us;                      // switch -> first slot written (UINT32_MAX: none)
    uint32_t ready_us;                      // switch -> STROBE_LIVE_RING_READY_SAMPLES written
    uint32_t at_us[ADC_PROBE_POINTS];       // checkpoints after the switch (as read)...
    size_t   written_at[ADC_PROBE_POINTS];  // ...and the slots written by then (0.5 per us)
    size_t   written_end;
    uint32_t window_us;
    bool     over;                          // FCS.OVER at the end of the window
    bool     conv_err;                      // a conversion-error flag in head[]
    uint16_t head[ADC_PROBE_HEAD];          // ring[0..]: the first BURST samples, all ADC0
    // Then pairs of adc_ring_snapshot()s ADC_PROBE_PAIR_US apart, as a live
    // firing takes them: the relation the coverage check relies on, without
    // firing. [0] warm; [1] with the XIP cache invalidated just before the first
    // snapshot (round 3's cold-fetch condition).
    adc_ring_pair_t pair[2];
} adc_ring_probe_t;

#define ADC_PROBE_PAIR_US    250u

bool adc_ring_probe(adc_ring_probe_t *p);

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
    size_t          w_at_snap; // raw write index when the view was taken
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
// FRESH OR NOTHING (independent review round 5, 2026-10-12). A reading is FRESH
// only if every sample in it was written by the ring DURING the call: the call
// waits (~1 ms, bounded at 3 ms) for V5IN_FRESH_SAMPLES new ch1 samples. That
// proves the ring is alive and excludes anything from before a mode switch. The
// earlier version averaged whatever was in the ring and stamped it "fresh" -- a
// stopped ring would have fed the supply monitor stale samples indefinitely.
//   FRESH        *volts is new; adc_5vin_age_ms() resets.
//   NOT_SAMPLED  the current mode does not convert ch1 (ARMED {5,7}, BURST {0}).
//                *volts is the last good value, NOT a reading; the age grows.
//   STALLED      the mode should convert ch1 but the ring is down or did not
//                advance: an unexpected failure. *volts as NOT_SAMPLED.
//   INVALID      samples arrived but cannot be trusted (review round 6): a FIFO
//                overflow or underflow since the ring started -- an overflow
//                LOSES conversions and so can rotate the round-robin, putting
//                another channel in "ch1's" slots -- a DMA bus error on the ring,
//                or a sample carrying the conversion-error bit (datasheet
//                12.4.3.4: discard them). After an overflow or DMA error the ring
//                is restarted to re-establish the channel order. *volts as above.
// Decisions (the power FSM) must act on the status, never on *volts alone; the
// FSM treats everything but FRESH and NOT_SAMPLED as SUPPLY_UNREADABLE.
typedef enum {
    ADC_5VIN_FRESH = 0,
    ADC_5VIN_NOT_SAMPLED,
    ADC_5VIN_STALLED,
    ADC_5VIN_INVALID,
} adc_5vin_status_t;

adc_5vin_status_t adc_5vin_read(float *volts);

// DISPLAY ONLY: adc_5vin_read()'s value, fresh when it can be, else the last
// good one. Pair it with adc_5vin_age_ms(); never base a decision on it.
float    adc_read_5vin_volts(void);

// Milliseconds since the last FRESH reading (UINT32_MAX: none yet). Grows while
// ch1 is not sampled or the ring is stalled; nothing else refreshes it.
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
// Block capture -- the bench instrument.
//
// Free-runs `chan_mask` into a RAM buffer via DMA, then hands it back for the
// CLI to dump as CSV. With tools/scope.py on the other end this turns the board
// into a logging DSO, which is what makes phases 3-5 tractable.
//
// If chan_mask has more than one bit set, samples are round-robin interleaved
// in ascending channel order starting at the lowest channel in the mask.
// ---------------------------------------------------------------------------

#define ADC_CAPTURE_MAX_SAMPLES 16384   // 32 KB of SRAM
// log2 of the buffer size IN BYTES -- what DMA ring mode wants. 16384 samples
// x 2 bytes = 32768 = 2^15. Keep these two in step.
#define ADC_CAPTURE_RING_BITS   15

// Returns the number of samples captured, or 0 on error (bad mask, bad rate).
// Blocking. Restores ADC_MODE_IDLE afterwards.
size_t adc_capture(unsigned chan_mask, size_t n_samples, uint32_t rate_hz);

// ---------------------------------------------------------------------------
// TRIGGERED CAPTURE -- a real oscilloscope trigger, with PRE-trigger history.
//
// WHY THE CIRCULAR DMA, rather than "start capturing when the signal is loud":
// by the time a threshold has been crossed the onset has ALREADY HAPPENED. The
// interesting part of an impact -- the baseline immediately before it, and the
// leading edge itself -- is in the past. A start-on-command capture cannot ever
// contain it. So the DMA runs continuously into a wrapping buffer, and the
// trigger decides where to STOP, not where to start.
//
// The buffer is therefore circular and the valid data does not begin at index
// 0. Use adc_capture_start() and walk it modulo ADC_CAPTURE_MAX_SAMPLES; the
// plain adc_capture() path leaves start = 0 so the same walk works for both.
//
// `poll` is called every couple of ms while waiting. Return true from it to
// abort. It exists so this file needs no dependency on the service layer --
// the CLI passes something that services the FSM and watches for a keypress.
typedef bool (*adc_wait_poll_fn)(void);

typedef enum {
    ADC_TRIG_OK = 0,
    ADC_TRIG_TIMEOUT,      // nothing crossed the threshold in time
    ADC_TRIG_ABORTED,      // poll() asked to stop
    ADC_TRIG_ERROR,        // bad arguments, or no DMA channel
} adc_trig_result_t;

// Single channel only -- interleaving would make the threshold ambiguous.
//   threshold  : |sample - baseline| in CODES that arms the stop
//   pre_frac   : fraction of n_samples kept BEFORE the trigger (0.0 .. 0.9)
//   baseline   : measured internally before arming, and returned via *out_base
adc_trig_result_t adc_capture_triggered(unsigned chan, size_t n_samples,
                                        uint32_t rate_hz, uint16_t threshold,
                                        float pre_frac, uint32_t timeout_ms,
                                        adc_wait_poll_fn poll,
                                        uint16_t *out_base, size_t *out_trig);

// Access the buffer filled by the last adc_capture().
const uint16_t *adc_capture_buffer(void);
size_t          adc_capture_count(void);
unsigned        adc_capture_mask(void);
uint32_t        adc_capture_rate(void);

// Index in adc_capture_buffer() where the valid data BEGINS. Always 0 after a
// plain adc_capture(); non-zero after a triggered one, because the DMA wrapped.
// Walk (start + i) % ADC_CAPTURE_MAX_SAMPLES.
size_t          adc_capture_start(void);

// Offset of the trigger point within the captured data, or SIZE_MAX if the
// last capture was not triggered.
size_t          adc_capture_trig_offset(void);

// True if the ADC FIFO overran during the last capture. An overrun rotates the
// round-robin channel phase, which silently mislabels every subsequent sample --
// so this is a hard error, not a warning.
bool adc_capture_overran(void);

// Convert a raw code to volts at the pin.
static inline float adc_code_to_volts(uint16_t code) {
    return (float)code * (3.3f / 4095.0f);
}

#endif // PITRAC_ADC_ENGINE_H
