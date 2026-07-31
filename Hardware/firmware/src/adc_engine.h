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
// One-shot blocking reads. These stop any running free-run mode, take the
// sample(s), and restore the previous mode. Fine for the CLI and the power FSM;
// do not call from a hot path.
// ---------------------------------------------------------------------------

// Oversampled mean of one channel. n is clamped to [1, 4096].
uint16_t adc_read_avg(unsigned chan, unsigned n);

// Same, converted to volts at the pin (does NOT undo any divider).
float    adc_read_volts(unsigned chan, unsigned n);

// +5V_IN in volts, undoing the R46/R47 divider and applying cal.adc1_scale.
// This is the discriminator that decides whether we are allowed to close the
// +5V latch: ~2.30-2.35 V at the pin on USB-C power vs ~2.60 V on the Meanwell.
float    adc_read_5vin_volts(void);

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
