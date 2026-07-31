// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// board.h â€” PiTrac "Second Board To Rule Them All" Rev V1
//
// SINGLE SOURCE OF TRUTH for pins and hardware constants.
//
// Every GPIO below was verified against The_Second_Board_To_Rule_Them_All.net
// (KiCad netlist export), not just the documentation. If you change a number
// here, re-check the netlist.
//
// Read the HARDWARE FACTS block before touching anything. Several of these are
// load-bearing and are exactly the kind of thing that gets "cleaned up" by
// someone who doesn't know the board.

#ifndef PITRAC_BOARD_H
#define PITRAC_BOARD_H

#include <stdint.h>

// ===========================================================================
// HARDWARE FACTS THAT CONSTRAIN THE FIRMWARE â€” do not "simplify" these away
// ===========================================================================
//
// 1. PIN_PWR_TOGGLE (GPIO14) has NO EXTERNAL PULL-UP. The internal pull-up is
//    load-bearing. R42 1K + C40 100 nF give ~100 us of hardware filtering only.
//
// 2. PIN_PULSE_LIMIT_DIS (GPIO27) DEFEATS THE STROBE HARDWARE WATCHDOG.
//    It is written 0 in exactly one place (safe_state()) and nowhere else.
//    There must be no CLI path and no config flag that raises it until the
//    strobe phase is fully characterised. A stuck-high strobe with the
//    watchdog defeated destroys the LED bank and possibly Q9.
//
// 3. The +5V latch (GPIO15) IS THE PI 5's POWER SWITCH. Raising it with a Pi
//    seated and no orderly shutdown corrupts the SD card. Raising it on
//    USB-only power browns out everything. Guarded by V5_MIN_FOR_LATCH at latch
//    time and V5_MIN_SUSTAINED continuously thereafter.
//
// 4. D_COMPARATOR (GPIO46) has an EXTERNAL 10K pull-up (R103). Do not enable
//    an internal pull.
//
// 5. The beam LED is NOT driven directly by the MCU. PIN_MOD_PWM feeds a
//    74LVC1G123 one-shot (U9) that hardware-clamps every high phase. There is
//    no disable path on the beam watchdog â€” by design, no DC beam mode exists.
//
// 6. Both one-shots (U5 strobe, U9 beam) run from +3V3, not 5 V. This matters
//    for the clamp width â€” see STROBE_HW_LIMIT_US_ASSUMED.
//
// 7. Panel LEDs on J7 are fed from the SWITCHED +5V rail. They cannot indicate
//    standby. Use the on-board D5/D6 (always-on +3V3) for pre-latch feedback.
//
// 8. RP2350 erratum E9 (check your die revision): a GPIO input with the
//    INTERNAL PULL-DOWN enabled, driven from a high-impedance source, can latch
//    around 2.2 V instead of pulling to 0. PIN_CAM_STROBE_0/1 and PIN_RPI5_ON
//    are exactly that configuration when the connectors are unpopulated.
//    Never use them as a safety interlock without software confirmation.

// ===========================================================================
// GPIO MAP  (netlist-verified against U3 pin functions)
// ===========================================================================

// --- Raspberry Pi 5 interface (J8 40-pin header) ---------------------------
#define PIN_RPI5_ON          0   // in  : Pi userspace up      (J8.15 <- Pi GPIO22, R29 1K)
#define PIN_HANDSHAKE_0      1   // bidir spare                (J8.16 <-> Pi GPIO23, R36 1K)
#define PIN_SYSTEM_READY     2   // out : armed mirror         (J8.13 -> Pi GPIO27, R41 1K)
#define PIN_IRQ_OUT          3   // out : capture-complete     (J8.11 -> Pi GPIO17, R43 1K)
#define PIN_HANDSHAKE_1      7   // bidir spare                (J8.36 <-> Pi GPIO16, R37 1K)
#define PIN_PI_3V3_SENSE    24   // in  : Pi 3V3 present. R45 10K / R44 100K divider
                                 //       -> 3.3 V * 0.909 = 3.0 V when the Pi is powered.
#define PIN_RPI5_SHUTDOWN   43   // out : shutdown request     (J8.37 -> Pi GPIO26, R39 1K)

// --- External I2S microphone (J5) ------------------------------------------
#define PIN_I2S_SCK          4   // out (PIO1)  J5.5, R28 220R
#define PIN_I2S_WS           5   // out (PIO1)  J5.3, R26 220R
#define PIN_I2S_DATA         6   // in  (PIO1)  J5.1, R25 220R

// --- Cameras (J4) -----------------------------------------------------------
#define PIN_CAM_STROBE_0     8   // in  : cam0 exposure active (J4.7, R19 220R)
#define PIN_CAM_STROBE_1     9   // in  : cam1 exposure active (J4.8, R24 220R)
#define PIN_CAM_TRIGGER     10   // out : both cameras         (J4.3/4, R18 220R)

// --- Panel (J7) and on-board indicators ------------------------------------
#define PIN_PWR_BTN_LED     11   // out : panel button LED, Q4 sink via R48 47R.  SWITCHED +5V.
#define PIN_READY_LED       12   // out : panel ready LED,  Q5 sink via R49 220R. SWITCHED +5V.
#define PIN_LED_RED         18   // out : on-board D6, 120R, ACTIVE HIGH, always-on +3V3
#define PIN_LED_YELLOW      19   // out : on-board D5, 120R, ACTIVE HIGH, always-on +3V3

// --- Power control ----------------------------------------------------------
#define PIN_PWR_TOGGLE      14   // in  : panel button, ACTIVE LOW, INTERNAL PULL-UP REQUIRED
#define PIN_LATCH_CONTROL   15   // out : high = +5V rail on. Q2 gate; R12 10K pulldown.

// --- Strobe -----------------------------------------------------------------
#define PIN_STROBE_PULSE    25   // out (PIO0) : hardware-clamped pulse gate. R57 1K pulldown.
#define PIN_PULSE_LIMIT_DIS 27   // out : DANGER â€” defeats the strobe watchdog. TEST ONLY.
#define PIN_GATE_PWM        28   // out (PWM 6A) : strobe current setpoint DAC
                                 //   *** SHARES SLICE 6A WITH PIN_READY_LED (GPIO12).
                                 //   *** See the PWM SLICE MAP below. Phase 6 blocker.

// --- Optical chain ----------------------------------------------------------
#define PIN_MOD_PWM         31   // out (PWM 7B) : beam carrier. R69 1K pulldown.
                                 //   *** shares slice 7B with PIN_LATCH_CONTROL (GPIO15).
                                 //   *** GPIO15 MUST STAY SIO. See the PWM SLICE MAP below.
#define PIN_HPF_TOGGLE      33   // out : 1 = baseline tracking, 0 = hold (U14 TMUX1219 SEL)
#define PIN_DEMOD_PWM       39   // out (PWM 11B): demod clock, phase-locked to slice 7
#define PIN_THRESHOLD_PWM   44   // out (PWM 10A): comparator threshold DAC (also ADC4 â€” never sample)
#define PIN_D_COMPARATOR    46   // in  : ball-detect comparator. EXTERNAL 10K pull-up (R103).

// --- Misc -------------------------------------------------------------------
#define PIN_USB_ENABLE      32   // out : USB-A accessory VBUS switch (Q6 -> Q7)
#define PIN_UART_TX         36   // out : UART1 -> Pi RXD  (J8.10, R30 0R)
#define PIN_UART_RX         37   // in  : UART1 <- Pi TXD  (J8.8,  R31 1K)

// ===========================================================================
// ADC CHANNELS  (RP2350B: ADC channel n == GPIO 40+n)
// ===========================================================================
// PWM SLICE MAP -- READ THIS BEFORE PUTTING ANY PIN ON GPIO_FUNC_PWM
// ===========================================================================
//
// RP2350B has 12 slices and the GPIO->slice mapping is NOT the RP2040 formula.
// From SDK 2.3.0 hardware/pwm.h:
//
//     gpio < 32 :  slice = (gpio >> 1) & 7          channel = gpio & 1
//     gpio >= 32:  slice = 8 + ((gpio >> 1) & 3)    channel = gpio & 1
//
// Getting this wrong is easy and the earlier comments in this file did (GPIO28
// was documented as "2A", GPIO31 as "3B", GPIO39 as "7B" -- all wrong).
//
//   GPIO11  PWR_BTN_LED       slice  5B    panel, PWM, active
//   GPIO12  READY_LED         slice  6A    panel, PWM, active
//   GPIO15  LATCH_CONTROL     slice  7B    SIO ONLY
//   GPIO27  PULSE_LIMIT_DIS   slice  5B    SIO ONLY
//   GPIO28  GATE_PWM          slice  6A    strobe DAC, Phase 6
//   GPIO31  MOD_PWM           slice  7B    beam carrier, PWM, active
//   GPIO39  DEMOD_PWM         slice 11B    demod clock, PWM, active
//   GPIO44  THRESHOLD_PWM     slice 10A    comparator DAC, Phase 3
//
// THREE PAIRS COLLIDE, and in every case it is the SAME CHANNEL, not merely the
// same slice. That distinction is the whole problem:
//
//   same slice, DIFFERENT channel (6A vs 6B) -> shares TOP and DIV, so a common
//       frequency, but each channel has its OWN compare register. Independent
//       duty cycles. This is fine and is the normal way to get two PWMs from one
//       slice.
//   same slice, SAME channel (6A and 6A)     -> ONE compare register. The block
//       produces a single output and the GPIO mux routes it to both pins. They
//       emit the IDENTICAL waveform. There is no second register to write, so
//       "let the one that needs a specific frequency win" does not help --
//       the duty is shared too.
//
// Any two GPIOs **16 apart** collide this way: slice = (gpio>>1)&7 wraps and the
// channel bit (gpio&1) is unchanged. 12/28, 15/31, 11/27 are all exactly 16 apart.
// **Design rule for the next board spin: never put two PWM functions on GPIOs
// 16 apart.** Note slice 6 channel B (GPIO13/GPIO29) is unassigned -- had the
// ready LED been routed to GPIO13, it and GPIO28 would have coexisted perfectly.
//
// 1. GPIO12 READY_LED  vs GPIO28 GATE_PWM   -- slice 6A. **A REAL CONFLICT, NOT
//    YET HIT.** panel.c drives 6A today; Phase 6b needs it for the strobe current
//    setpoint. Whichever is configured second silently takes over both. The LED
//    brightness would become the 9 A current setpoint, or vice versa.
//    RESOLUTION: the ready LED must give up the PWM block -- plain on/off, or
//    software PWM off the 50 Hz timer (ARCHITECTURE.md A4). Both GPIO numbers are
//    fixed by the PCB, so the indicator is the one that yields. Do this in 6b.
//
// 2. GPIO15 LATCH_CONTROL vs GPIO31 MOD_PWM -- slice 7B. Safe ONLY because GPIO15
//    stays SIO. If GPIO15 were ever set to GPIO_FUNC_PWM it would switch the +5V
//    rail -- the Pi's power -- at the beam carrier frequency and duty. NEVER put
//    GPIO15 on PWM.
//
// 3. GPIO11 PWR_BTN_LED vs GPIO27 PULSE_LIMIT_DIS -- slice 5B. Safe ONLY because
//    GPIO27 stays SIO (see hard rule 2 above). If GPIO27 were ever set to
//    GPIO_FUNC_PWM it would toggle the strobe hardware watchdog defeat line at the
//    panel LED's ~1 kHz. NEVER put GPIO27 on PWM.
//
// beam.c and panel.c both resolve slices at runtime via pwm_gpio_to_slice_num(),
// so the CODE has always been correct. It was the documentation that was wrong.
//
// ===========================================================================
#define ADC_FIRST_GPIO      40

#define ADC_CH_CURRENT       0   // GPIO40 : strobe current sense, 135 mV/A (R65||R66 = 0.135R)
#define ADC_CH_5VIN          1   // GPIO41 : +5V_IN / 2 (R46 100K / R47 100K)
#define ADC_CH_TIA           2   // GPIO42 : raw TIA carrier (R82 1K + D13 BAT54S clamp)
// channel 3 = GPIO43 = RPI5_SHUTDOWN  -> digital output, NEVER sample
// channel 4 = GPIO44 = Threshold_PWM  -> digital output, NEVER sample
#define ADC_CH_DETECT        5   // GPIO45 : post-filter detect signal (comparator + input)
// channel 6 = GPIO46 = D_Comparator   -> digital input,  NEVER sample
#define ADC_CH_MIC           7   // GPIO47 : mic amp, 1.65 V bias

// Mask of channels that are safe to sample. Anything outside this is a pin
// configured as a digital output; sampling it is a bug, not a measurement.
#define ADC_VALID_MASK  ((1u << ADC_CH_CURRENT) | (1u << ADC_CH_5VIN) | \
                         (1u << ADC_CH_TIA)     | (1u << ADC_CH_DETECT) | \
                         (1u << ADC_CH_MIC))

// ===========================================================================
// ELECTRICAL / SCALING CONSTANTS
// ===========================================================================

#define ADC_VREF_V           3.3f
#define ADC_FULL_SCALE       4095.0f

// +5V_IN monitor: R46/R47 = 100K/100K -> exactly /2 nominal.
// cal.adc1_scale trims the real divider + ADC INL (both ~1%), which matters
// because we are discriminating 2.30 V from 2.60 V.
#define V5IN_DIVIDER         2.0f

// --- The USB-vs-external-supply discriminator -------------------------------
//
// MEASURED ON THIS BOARD (2026-07-29):
//   USB-C only     4.85 V   (host VBUS 5.2 V less ~0.35 V across D8, an SS14)
//   Bench PSU / Meanwell   5.20 V
//
// Two DIFFERENT thresholds, because the two checks answer different questions.
//
// V5_MIN_FOR_LATCH -- asked in STANDBY, BEFORE closing the latch, with no load
// on the +5V rail. "Is there a real supply, or only USB?" Latching on USB would
// brown out the Pi and sit below the boost's 4.74 V UVLO.
//
// Sits at the midpoint of the two measured values, ~170 mV from each. The
// original single 4.90 V threshold left only 50 mV above the USB reading -- at
// the ADC pin that is ~25 mV, or about 31 codes, and the R46/R47 divider's two
// 1% resistors can contribute Â±50 mV of error at 5 V by themselves. Since
// `adc5vcal` is RAM-only and lost on every reset, this guard has to be right
// UNCALIBRATED on a cold boot, so it needs real margin.
//
// TRADE-OFF, made knowingly: a supply below ~5.05 V will now be refused. The
// design specifies a Meanwell LRS-75-5 set to 5.2 V, so set bench supplies to
// 5.2 V. If you deliberately want to run lower, lower this -- but re-measure
// your USB-only voltage first and keep the gap.
#define V5_MIN_FOR_LATCH     5.05f

// V5_MIN_UNDER_LOAD -- asked in POWERING_ON, AFTER the latch closed, with the
// Pi and the boost now drawing. A real supply legitimately sags here, so reusing
// the discriminator threshold would false-fault. This is a genuine-collapse
// floor only: set below the boost UVLO (4.74 V on / 4.52 V off) and the Pi 5's
// 4.64 V brownout warning, above its 4.2 V fatal threshold.
#define V5_MIN_UNDER_LOAD    4.60f

// V5_MIN_SUSTAINED -- polled continuously WHILE LATCHED. Catches the supply being
// removed after the latch already closed, which the one-shot check at latch time
// cannot see.
//
// The failure it prevents (found on the bench 2026-07-30): latch on with both USB
// and the bench supply connected, then unplug the bench supply. Q3 stays closed, so
// the +5V rail -- Pi, boost, beam LED, analog -- is now fed from USB VBUS through
// D8, an SS14 rated 1 A. With no Pi that looks stable and harmless. With a Pi 5 on
// the header it is a multi-amp load through a 1 A diode: brownout, SD corruption,
// and a cooked D8.
//
// Set above the USB-only level (~4.6-4.85 V measured, port dependent) and above the
// Pi 5's 4.64 V brownout warning, so we act before the Pi notices.
#define V5_MIN_SUSTAINED     4.90f

// Debounce, and it is load-bearing. From Phase 6 the rail is EXPECTED to sag during
// strobe bursts -- the boost UVLO is deliberately bracketed at 4.74/4.52 V for that
// reason. A bare threshold would trip on every shot. A removed supply is a sustained
// low; burst sag is milliseconds. Requiring half a second of continuous low cleanly
// separates the two.
#define V5_LOW_DEBOUNCE_MS      500
#define V5_MONITOR_INTERVAL_MS  100   // ~0.5 ms of ADC work per 100 ms

// Strobe current sense: 0.135 R -> 135 mV/A, read through R32 4K7.
#define STROBE_SENSE_V_PER_A 0.135f

// ===========================================================================
// TIMING / CARRIER CONSTANTS
// ===========================================================================

#define SYSCLK_HZ            150000000u

// Beam carrier. TOP+1 = 1440 divides 150 MHz exactly, and 30 % lands on an
// integer level (432), so both duty and demod phase are exact.
//   f = 150e6 / 1440 = 104166.67 Hz
//   phase resolution = 1 tick = 6.67 ns = 0.25 deg
// This is a STARTING POINT, not a final answer â€” the boost runs at a nominal
// 1.055 MHz with real tolerance, and a square-wave demodulator folds anything
// near an ODD harmonic n*fc down to |fi - n*fc|. Pick the final carrier with
// the `scan carrier` CLI command (max measured SNR), not with arithmetic.
#define CARRIER_TOP_DEFAULT  1439u
#define CARRIER_LEVEL_30PCT   432u   // 432/1440 = exactly 30.000 %
#define DEMOD_LEVEL_50PCT     720u

#define PWM_SLICE_CARRIER      3     // GPIO31 = 3B
#define PWM_SLICE_DEMOD        7     // GPIO39 = 7B
#define PWM_SLICE_GATE         2     // GPIO28 = 2A
#define PWM_SLICE_THRESHOLD   10     // GPIO44 = 10A

// DAC PWM: TOP+1 = 1024 -> 146.5 kHz, 3.2 mV steps, filtered by two 1 ms RC
// poles (10K/0.1uF each) so essentially nothing escapes the node.
#define DAC_TOP              1023u

// ---------------------------------------------------------------------------
// !! UNVERIFIED â€” MEASURE BEFORE RELYING ON THESE !!
//
// The .md quotes 113 us for both 74LVC1G123 one-shots. The datasheet says
// t_w ~= K * Rext * Cext with K ~= 0.7 at Vcc = 3.3 V (and both one-shots ARE
// on +3V3). With R = 56K and C = 2.2 nF that gives ~86 us, not 113 us.
//
// If the real clamp is 86 us then a 100 us software limit is ABOVE the hardware
// limit, and every slow-ball pulse gets silently truncated by hardware instead
// of controlled by firmware. Measure both clamps (Phase 2 step 4 for U9,
// Phase 6a for U5), then set these from measurement and delete this warning.
// ---------------------------------------------------------------------------
#define STROBE_HW_LIMIT_US_ASSUMED  86    // <-- MEASURE. .md says 113; physics says ~86.
#define STROBE_SW_MAX_US            73    // 0.85 * assumed HW limit. Re-derive after measuring.
#define STROBE_MIN_GAP_US          150    // let the VIR bulk caps breathe between pulses

// ===========================================================================
// Pi 5 SOFT-SHUTDOWN â€” polarity decision
// ===========================================================================
//
// The .md pseudocode says pulse(PIN_RPI5_SHUTDOWN, 200ms), which reads as
// driving the line HIGH. The Linux `gpio-shutdown` overlay defaults to
// ACTIVE-LOW with an internal pull-up (active_low=1, gpio_pull=up), so the
// pseudocode is backwards for the default overlay.
//
// We commit to ACTIVE-LOW here because it matches the overlay default.
//
// This block used to claim active-low was "inherently safe through an RP2354
// reset: the pad reverts to input (high-Z) and the Pi's own pull-up holds the
// line deasserted." THAT IS WRONG and the error cut the safe direction.
// PADS_BANK0_GPIO43_RESET = 0x116 -> PDE=1, PUE=0. The output driver is high-Z,
// but a ~50-80K internal pull-down is ACTIVE from the reset edge until
// safe_state_init() runs. For active-low, that is the ASSERTED level. So the
// reset behaviour is a hazard to be measured, not a safety property to lean on.
//
// Pi side:  dtoverlay=gpio-shutdown,gpio_pin=26
//
// INVARIANT, non-negotiable: an RP2354 reset must never look like a shutdown
// request. Verify by scoping GPIO43 through an SW2 press -- but judge it by
// PULSE WIDTH, not by the presence of an edge. A few ms of low is the pad
// default; 200 ms (PI_SHUTDOWN_PULSE_MS) is a real assertion and a failure.
//
// Still open (PROGRESS.md Q10): with a Pi seated, that pull-down divides against
// gpio-shutdown's ~50K pull-up through R39's 1K, putting J8.37 near 1.7-2.0 V --
// at or below RP1's VIH. Measured in Phase 1b with an emulated pull-up. If it
// reads low, fit a 10K pull-up from J8.37 to the always-on +3V3.
#define PI_SHUTDOWN_ACTIVE_LOW  1
#define PI_SHUTDOWN_PULSE_MS   200

// Guard times for the shutdown sequence.
//   MIN_HOLDOFF: never drop the latch sooner than this after the request, even
//     if the down-indicator says the Pi is already gone. A glitch on a 1K sense
//     line must not be able to yank power mid-filesystem-sync. gpio-shutdown ->
//     systemd -> halt is typically 5-15 s. Retune to ~2x the measured duration.
//   MAX_WAIT: if the Pi never indicates down, cut power anyway and log a fault.
#define PI_SHUTDOWN_MIN_HOLDOFF_MS  15000
#define PI_SHUTDOWN_MAX_WAIT_MS     60000
#define PI_BOOT_TIMEOUT_MS          90000
#define RAIL_SETTLE_MS                250
#define BUTTON_DEBOUNCE_MS             25
#define BUTTON_LONG_PRESS_MS         5000

// How long POWERING_ON keeps LOOKING for a Pi before concluding there is not
// one and dropping into BENCH_RUNNING.
//
// This used to be the same instant as RAIL_SETTLE_MS, which conflated two
// unrelated timings: RAIL_SETTLE_MS sizes the BOOST soft start (~86 ms), and has
// nothing to say about how long a Pi 5 takes to raise its header 3V3 rail after
// +5V is applied. A single sample at 250 ms means a Pi that is merely slow gets
// classified as absent -- and in BENCH_RUNNING a button press is a hard
// FORCE_OFF, so the next press yanks the rail out from under a booting Pi.
// That is the SD-card corruption this whole subsystem exists to prevent.
//
// Note the Phase 1b test matrix cannot catch this: every simulated-Pi test
// asserts the jumpers BEFORE the button press, an ordering a real Pi can never
// produce (it cannot raise 3V3 until after the rail it is powered by comes up).
//
// 3 s is a deliberately generous placeholder. The real number comes from Phase
// 8.3 -- scope +5V at J8.2 against Pi 3V3 at J8.1 and measure the latency. The
// only cost of an over-long window is that a no-Pi bench session waits this long
// before the ring goes solid, which is also what makes POWERING_ON visible.
#define PI_DETECT_WINDOW_MS          3000

// Debounce for LATE Pi detection (BENCH_RUNNING -> PI_BOOTING). The promotion is
// a backstop for a Pi slower than PI_DETECT_WINDOW_MS, which is what keeps that
// window's exact value from being critical.
//
// Load-bearing: without it, one glitch on the sense line promotes us to
// PI_BOOTING, and if it then de-asserts we raise FAULT_NO_PI_DETECTED and
// FORCE_OFF -- dropping the rail in the middle of, say, a beam ramp.
#define PI_PRESENT_DEBOUNCE_MS        100

#endif // PITRAC_BOARD_H
