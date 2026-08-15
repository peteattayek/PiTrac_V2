// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// detect.h -- Phase 3: the ball-detection end of the optical chain.
//
// This module owns three pins, and they are the last three in the signal path:
//
//   GPIO44  Threshold_PWM   PWM -> two RC poles -> Threshold_DC (TP8) -> U15 pin 2
//   GPIO33  HPF_Toggle      -> U14 TMUX1219 SEL: baseline TRACK or HOLD
//   GPIO46  D_Comparator    <- U15 pin 1, open collector, R103 10K pull-up to +3V3
//
// The chain ahead of them, for orientation (verified against the netlist, not the
// .md -- several numbers there are wrong, see BENCH_P3_DETECT.md):
//
//   D12 -> U11A TIA (Rf 470K) -> U13 chopper demod -> 4th-order LPF f0 15.39 kHz,
//   gain 2 -> TP9 -> C81 330nF -> [U14 gate] -> R99 -> U12B non-inverting x14.5
//   -> U15 (+) vs Threshold_DC (-), and -> R102 -> D14 clamp -> ADC5
//
// THREE THINGS THAT ARE NOT OBVIOUS FROM THE SCHEMATIC AND WILL COST YOU AN
// AFTERNOON IF YOU ASSUME OTHERWISE:
//
// 1. ADC5 IDLES NEAR 0 V, not at the 2.59 V virtual ground. Everything from TP6
//    to TP10 sits at +5VA/2, but U12B is referenced to GROUND (its + input gets
//    its DC level through R99 from a node held at 0 V by R96 2M), so the ADC5
//    node starts at zero and a ball is a positive bump from there.
//
// 2. ADC5 SATURATES AROUND 3.3 V. D14 (BAT54S) clamps the node to the +3V3 rail
//    through R102's 1K. U12B is rail-to-rail on +5VA and will happily drive 5 V
//    into it. A clipped peak makes the 50 %-of-peak refinement meaningless, which
//    is why detect_refine() has to report saturation rather than average through it.
//
// 3. U15 HAS NO HYSTERESIS. There is no resistor from D_Comparator back to pin 3
//    anywhere on the board -- I checked every net that touches U15. Chatter on a
//    slow-slewing edge is a property of this board, not a firmware bug. See the
//    glitch-reject note in detect.pio.
//
// Detection is ACTIVE HIGH on GPIO46: signal above threshold pulls the open
// collector off and R103 takes the pin to +3V3.

#ifndef PITRAC_DETECT_H
#define PITRAC_DETECT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

void detect_init(void);

// ---------------------------------------------------------------------------
// THRESHOLD DAC  (GPIO44 -> TP8 -> U15 pin 2)
//
// PWM at DAC_TOP+1 = 1024 counts -> 146.5 kHz, 3.2 mV steps, DC = duty x 3V3.
//
// Settling is DAC_SETTLE_MS (20 ms), and that number is not the one the bench
// doc quotes. R86/C74 and R89/C76 are 10K/0.1uF each, so "two 1 ms poles" is the
// obvious reading -- but the second section LOADS the first, so the real poles of
// the cascaded ladder are at RC/0.382 = 2.62 ms and RC/2.618 = 0.382 ms. Five
// time constants of the dominant pole is 13 ms; the doc's 10 ms is about four,
// leaving ~2 % of a step uncrossed. On a 3.3 V full scale that is 66 mV, or 20
// threshold steps -- enough to smear the flip point the cross-calibration is
// trying to find.
// ---------------------------------------------------------------------------

void  detect_threshold_set_duty(float duty);   // 0..1, blocks for DAC_SETTLE_MS
void  detect_threshold_set_volts(float v);
float detect_threshold_duty(void);
float detect_threshold_volts(void);            // duty x vref
uint16_t detect_threshold_level(void);         // raw PWM compare level, 0..DAC_TOP

// Nominal 3.3 V. The real +3V3 measured 3.246 V on this board (PROGRESS.md 6),
// so trim this if you want detect_threshold_volts() to agree with a DMM on TP8.
// TP8 is the truth; this is only how the firmware reports its intent.
void  detect_threshold_set_vref(float v);
float detect_threshold_vref(void);

// ---------------------------------------------------------------------------
// GATED HPF  (GPIO33 -> U14 TMUX1219 SEL)
//
// U14 is an SPDT with only ONE throw connected:
//   S1 -> R96 2M -> GND   with C81 330nF this is the 0.66 s HPF   -> TRACK
//   S2 -> not connected   the node floats and C81 holds its charge -> HOLD
//
// So HOLD is a genuine open circuit, not a second filter corner. The U12B input
// DC level is undefined there and walks on switch leakage and op-amp bias --
// roughly 1 nA into 330 nF is 3 mV/s at the node, x14.5 = ~44 mV/s at ADC5. That
// is the effect detect_hpf_test() measures, and it is also why ARMED (which means
// HOLD) cannot be held open indefinitely.
//
// WHICH GPIO33 LEVEL SELECTS WHICH PATH IS UNVERIFIED. The netlist encodes only
// the pin name "SEL". Everything here goes through HPF_SEL_TRACK in board.h; if
// detect_hpf_test() says the sense is inverted, flip that one line.
// ---------------------------------------------------------------------------

typedef enum { HPF_TRACK = 0, HPF_HOLD } hpf_mode_t;

// Refuses unless the +5V rail is up. U14 runs from +5VA, and driving SEL high
// into an unpowered mux back-feeds the analog rail through its protection
// structures -- the exact condition safe_state.c drives GPIO33 low to prevent.
bool        detect_hpf_set(hpf_mode_t m);
hpf_mode_t  detect_hpf_mode(void);
const char *detect_hpf_name(hpf_mode_t m);

// Drive GPIO33 low unconditionally. Must be called whenever the +5V rail is
// dropped, or a preceding `hpf track` leaves SEL high into a dark U14 --
// reachable through an ordinary `hpf track` then `off`. safe_state.c covers the
// boot case; this covers the runtime one.
void        detect_hpf_safe_off(void);

typedef struct {
    float track_drift_v;      // ADC5 excursion over the window, TRACK
    float hold_drift_v;       // same, HOLD
    float track_mean_v;
    float hold_mean_v;
    float ratio;              // hold_drift / track_drift
    bool  conclusive;         // ratio big enough to call it
    bool  polarity_ok;        // HPF_SEL_TRACK as defined matches what TRACK does
} hpf_test_t;

// Sit in each mode for `window_ms` and compare how far the ADC5 baseline walks.
// TRACK is pinned to 0 V through 2M and should barely move; HOLD floats and
// should visibly ramp. Blocking, ~2 x window_ms. Needs the ADC ring running and
// ch5 in the current round-robin set (IDLE or ARMED).
void detect_hpf_test(hpf_test_t *out, uint32_t window_ms);

// ---------------------------------------------------------------------------
// COMPARATOR  (GPIO46)
// ---------------------------------------------------------------------------

bool detect_comparator(void);      // true = above threshold (active high)

// ---------------------------------------------------------------------------
// PIO TRANSIT TIMER  (ARCHITECTURE.md A2)
//
// One state machine on the high-bank PIO block (pio_alloc.h) times each HIGH
// pulse on GPIO46 and pushes its width as one FIFO word. The count loop is
// exactly 2 cycles per tick, so the SM runs at 2 x DETECT_TICK_HZ.
//
// CHATTER AND COALESCING. U15 has no hysteresis, so one ball can produce
// several pulses as a slow edge crosses the threshold. The PIO pushes every
// one; detect_service() merges fragments separated by less than
// DETECT_COALESCE_US into a single pass, spanning from the first rise to the
// last fall, and records how many fragments it merged.
//
// That fragment count is not bookkeeping -- it is the chatter measurement the
// bench doc's "one clean pulse pair per pass" criterion actually needs, and it
// is why no filtering happens in the PIO program. Coalescing is a software
// policy that can be retuned from the CLI after seeing real data; a PIO filter
// would have to be guessed at before any data existed.
// ---------------------------------------------------------------------------

#define DETECT_TICK_HZ        1000000u   // 1 tick = 1 us
#define DETECT_COALESCE_US       2000u   // default; retune from bench data

// Why a pass may not mean what it looks like. Bit flags.
typedef enum {
    DQ_OK          = 0,
    DQ_SATURATED   = 1u << 0,  // ADC5 hit full scale: the peak is unknown
    DQ_NO_CROSSING = 1u << 1,  // the 50 % level was never crossed in the window
    DQ_WINDOW_CLIP = 1u << 2,  // the bump ran off the end of the ring history
    DQ_RING_LAPPED = 1u << 3,  // the DMA overwrote samples during the analysis
    DQ_CHATTER     = 1u << 4,  // fragments > 1: comparator transit is a bound
    DQ_NO_ADC      = 1u << 5,  // ch5 was not in the round-robin; no ADC result
} detect_quality_t;

typedef struct {
    uint32_t seq;
    uint32_t t_ms;             // ms since boot, when the pass was closed
    uint32_t transit_us;       // SUM of the fragments' widths -- see below
    uint32_t raw_ticks;        // same, in raw PIO ticks, uncorrected
    uint32_t adc_transit_ns;   // 50 %-of-own-peak, interpolated. 0 = not computed
    uint16_t adc_peak;         // code, relative to baseline
    uint16_t adc_baseline;     // code
    uint16_t adc_sat_samples;
    int16_t  adc_asym_q8;      // (t_peak - midpoint)/transit, Q8. Shape check.
    uint16_t adc_rate_khz;     // per-channel ADC rate when captured
    uint16_t fragments;        // 1 = clean. >1 = comparator chatter.
    uint16_t threshold_level;  // DAC level in force at the time
    uint16_t quality;          // detect_quality_t bits
    uint8_t  condition;        // 0/1/2, tags a Phase 4 experimental condition
    uint8_t  _pad;
} detect_pass_t;

// WHAT transit_us MEANS, AND WHEN TO TRUST IT.
//
//   fragments == 1  -> transit_us is the PIO's own measurement of the pulse.
//                      Exact to one tick plus DETECT_PIO_OVERHEAD_TICKS.
//   fragments > 1   -> transit_us is the SUM of the fragment widths, which
//                      EXCLUDES the notches between them. It is a LOWER BOUND
//                      on the true transit, not an estimate of it.
//
// The notches cannot be recovered from the arrival times: the RX FIFO is 4 deep
// and the superloop drains it in one pass, so all fragments of one ball are
// typically read within microseconds of each other no matter when the edges
// happened. Reconstructing a span from those timestamps would be fake precision.
//
// So a chattered pass has no trustworthy comparator transit, and the honest
// response is to say so rather than to average through it. Use the ADC-derived
// transit for those passes -- it works from the bump's own shape and does not
// care how many times the comparator crossed. Quantifying that disagreement is
// exactly what the Phase 4 experiment is for.
//
// If chatter turns out to be common enough that comparator timing needs to
// survive it, the fix is in the PIO program, not here: push the LOW interval as
// a second word so the gaps are measured rather than inferred. That is a real
// option, deliberately not taken before any chatter has been observed.

// Enable/disable the SM. Refuses to enable unless the +5V rail is up.
bool     detect_arm(bool on);
bool     detect_armed(void);

// Drain the PIO FIFO and coalesce. Call from the superloop; cheap when idle.
void     detect_service(void);

uint32_t detect_events(void);        // completed passes since arm
uint32_t detect_fragments(void);     // raw FIFO words READ since arm

// FIFO words the PIO pushed and we never read, because `push noblock` discards
// when the RX FIFO is full. Non-zero means the fragment counts are understated
// and the superloop is not keeping up with the chatter. Polls and drains the
// hardware RXSTALL latch, so call it before trusting detect_fragments().
uint32_t detect_dropped(void);
bool     detect_last_pass(detect_pass_t *out);

void     detect_set_coalesce_us(uint32_t us);
uint32_t detect_coalesce_us(void);

// Fixed overhead of the PIO program in ticks: the prologue between the rising
// edge being sampled and the first count, plus the epilogue after the fall.
// Derived from the instruction listing; VERIFY ON THE BENCH against known
// bit-banged pulse widths before trusting absolute transit numbers. It is a
// constant offset, so it cancels out of any comparison between two methods.
#define DETECT_PIO_OVERHEAD_TICKS  2u

// ---------------------------------------------------------------------------
// ADC REFINEMENT  (BENCH_P3_DETECT.md Phase 4, step 4)
//
// A fixed comparator threshold crosses the signal's rising slope at a point
// that depends on the signal's AMPLITUDE, so a dimmer ball is detected later
// going up and earlier coming down -> transit short -> speed over-estimated,
// with the error scaling with reflectance. That is a systematic bias, not
// noise, and averaging will not remove it.
//
// Working from 50 % of the bump's OWN peak removes the amplitude dependence
// entirely. It costs ~10 us of post-processing, which is free because it
// happens during the camera handshake we are already waiting on.
//
// SATURATION. ADC5 reaches full scale (code 4095, 3.3 V) BEFORE D14 conducts at
// ~3.6 V, so clipping shows up as ADC full scale rather than a diode knee.
// A clipped peak makes the 50 % level too LOW, so the rise crossing lands early
// and the fall crossing late -> transit LONG -> speed UNDER-estimated. That is
// the OPPOSITE sign to the comparator's bias, so a saturated pass brackets the
// truth rather than being useless -- but mixing saturated passes into the
// statistics silently would flatten the very slope Phase 4 is measuring. Hence
// DQ_SATURATED, and hence detect_stats() counts them separately.
// ---------------------------------------------------------------------------

#define DETECT_SAT_CODE   4080u    // a few LSB below full scale

// Log capacity. 60 passes is the Phase 4 experiment; 256 leaves room for
// retries without wrapping.
#define DETECT_LOG_N      256u

// Waveform ring. A full ARMED window is 8192 samples x 2 B = 16 KB, and 60 of
// those would be 960 KB against 520 KB of SRAM -- so full waveforms for every
// pass are simply not possible. The bump is band-limited to 15.39 kHz by the
// LPF but its WIDTH is the transit, i.e. milliseconds, so at 250 ksps it is
// oversampled by orders of magnitude. Decimating by 8 with a box average is
// both the anti-alias filter and a small noise reduction.
#define DETECT_WAVE_DECIM   8u
#define DETECT_WAVE_LEN  1024u
#define DETECT_WAVE_N       4u     // 4 x 2 KB

typedef struct {
    uint32_t pass_seq;
    uint32_t rate_hz;              // post-decimation
    uint16_t len, peak_idx;
    uint16_t baseline;
    uint16_t _pad;
    uint16_t s[DETECT_WAVE_LEN];
} detect_wave_t;

// Beam path width across the optical axis, in mm. Velocity is NOT reported
// until this is set -- a speed derived from a guessed geometry is worse than no
// speed, because it looks authoritative. Cross-check against the cardboard-ramp
// prediction v = sqrt(2*g*h*5/7).
void  detect_set_path_mm(float mm);
float detect_path_mm(void);

// Tags subsequent passes with a Phase 4 condition index (0 nominal, 1 far,
// 2 low-reflectance).
void    detect_set_condition(uint8_t c);
uint8_t detect_condition(void);

size_t               detect_log_count(void);
const detect_pass_t *detect_log_at(size_t i);       // 0 = oldest retained
void                 detect_log_clear(void);
const detect_wave_t *detect_wave_for(uint32_t pass_seq);
const detect_wave_t *detect_wave_recent(size_t i);  // 0 = most recent

// Mean/sigma per method for one condition. Excludes passes whose ADC result is
// unusable; `n_excluded` says how many.
typedef struct {
    uint32_t n, n_excluded, n_saturated, n_chatter;
    float    cmp_mean_us, cmp_sd_us;
    float    adc_mean_us, adc_sd_us;
    float    bias;          // cmp_mean/adc_mean - 1
    float    peak_mean;     // mean peak code, for the bias-vs-amplitude fit
} detect_stats_t;

bool detect_stats(uint8_t condition, detect_stats_t *out);

// ---------------------------------------------------------------------------
// THRESHOLD / COMPARATOR CROSS-CALIBRATION  (BENCH_P3_DETECT.md 3.5)
//
// Sweep the threshold and find the duty at which D_Comparator flips. That single
// measurement ties the threshold DAC to the ADC5 scale and exercises both output
// paths of U12B at once, with no external gear.
//
// It doubles as the GPIO44 crosstalk check: the 146.5 kHz DAC carrier is only
// 42 kHz from the optical carrier, and while the two RC poles should bury it,
// PCB coupling from the GPIO44 trace would not care. If ADC5 moves as the
// threshold steps, that is crosstalk -- change DAC_TOP (2047 -> 73 kHz, or
// 511 -> 293 kHz) and re-run.
// ---------------------------------------------------------------------------

typedef struct {
    bool     found;
    float    flip_duty;        // first duty where the comparator went high
    float    flip_volts;       // threshold there, nominal
    uint16_t adc5_at_flip;     // raw ADC5 code at the flip
    float    adc5_at_flip_v;
    float    adc5_span_v;      // total ADC5 movement across the sweep = crosstalk
    uint16_t steps;
} threshold_sweep_t;

// Sweeps duty from `lo` to `hi` in `steps` increments. Blocking:
// steps x DAC_SETTLE_MS, so 64 steps is ~1.3 s. Restores the previous threshold.
void detect_threshold_sweep(threshold_sweep_t *out,
                            float lo, float hi, uint16_t steps);

#endif // PITRAC_DETECT_H
