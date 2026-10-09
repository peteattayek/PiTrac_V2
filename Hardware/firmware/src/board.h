// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// board.h -- PiTrac "Second Board To Rule Them All" Rev V1
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
// HARDWARE FACTS THAT CONSTRAIN THE FIRMWARE -- do not "simplify" these away
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
//    no disable path on the beam watchdog -- by design, no DC beam mode exists.
//
// 6. Both one-shots (U5 strobe, U9 beam) run from +3V3, not 5 V. This matters
//    for the clamp width -- see STROBE_HW_LIMIT_US.
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
#define PIN_PULSE_LIMIT_DIS 27   // out : DANGER -- defeats the strobe watchdog. TEST ONLY.
#define PIN_GATE_PWM        28   // out (PWM 6A) : strobe current setpoint DAC
                                 //   *** SHARES SLICE 6A WITH PIN_READY_LED (GPIO12).
                                 //   *** Resolved in firmware 2026-10-02: GPIO12 is SIO
                                 //   *** on/off and strobe.c owns slice 6. See the map.

// --- Optical chain ----------------------------------------------------------
#define PIN_MOD_PWM         31   // out (PWM 7B) : beam carrier. R69 1K pulldown.
                                 //   *** shares slice 7B with PIN_LATCH_CONTROL (GPIO15).
                                 //   *** GPIO15 MUST STAY SIO. See the PWM SLICE MAP below.
#define PIN_HPF_TOGGLE      33   // out : U14 TMUX1219 SEL. 0 = TRACK, 1 = HOLD -- see below.
#define PIN_DEMOD_PWM       39   // out (PWM 11B): demod clock, phase-locked to slice 7
#define PIN_THRESHOLD_PWM   44   // out (PWM 10A): comparator threshold DAC (also ADC4 -- never sample)
#define PIN_D_COMPARATOR    46   // in  : ball-detect comparator. EXTERNAL 10K pull-up (R103).

// --- U14 gated HPF: which SEL level selects which path -----------------------
//
// ✅ RESOLVED ON HARDWARE 2026-08-17, and confirmed against the datasheet.
//
// U14 is a TMUX1219 SPDT. Only ONE throw is connected:
//   S1 -> R96 2M -> GND    with C81 330 nF this is the 0.66 s HPF  ("TRACK")
//   S2 -> NOT CONNECTED    the node floats, C81 holds its charge   ("HOLD")
//
// So HOLD is a genuine open, not a second filter corner. In HOLD the U12B input
// DC level is undefined and drifts on op-amp bias current and switch leakage --
// that is a real, unbounded effect, and it is the state the detector runs in
// while armed.
//
// TMUX1219 truth table (TI datasheet): SEL = 0 -> S1 to D, SEL = 1 -> S2 to D.
// The unselected source goes high-impedance. Therefore on THIS board:
//
//        GPIO33 = 0  ->  S1  ->  R96/GND  ->  TRACK
//        GPIO33 = 1  ->  S2  ->  open     ->  HOLD
//
// **The netlist could not tell us this** -- it encodes only the pin name "SEL" --
// and BENCH_P3_DETECT.md's original assumption that `gpio 33 1` = TRACK was
// backwards. Two independent lines of evidence now agree: `hpf test` on hardware
// reported INVERTED relative to the old constant, and the datasheet truth table
// says the same thing.
//
// Nothing else in the firmware may compare against PIN_HPF_TOGGLE's raw level.
#define HPF_SEL_TRACK        0    // level that selects S1 (the 2M/GND leg)

// C81, the hold capacitor. Used to turn the measured HOLD drift rate into a
// leakage current, which is the number that actually bounds the armed window.
#define HPF_C81_F            330e-9f
#define HPF_SEL_HOLD         (!HPF_SEL_TRACK)

// --- Misc -------------------------------------------------------------------
#define PIN_USB_ENABLE      32   // out : USB-A accessory VBUS switch (Q6 -> Q7)
#define PIN_UART_TX         36   // out : UART1 -> Pi RXD  (J8.10, R30 220R)
#define PIN_UART_RX         37   // in  : UART1 <- Pi TXD  (J8.8,  R31 220R)

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
//   GPIO12  READY_LED         slice  6A    SIO ONLY (on/off) since 2026-10-02
//   GPIO15  LATCH_CONTROL     slice  7B    SIO ONLY
//   GPIO27  PULSE_LIMIT_DIS   slice  5B    SIO ONLY
//   GPIO28  GATE_PWM          slice  6A    strobe DAC, PWM, owned by strobe.c
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
// 1. GPIO12 READY_LED  vs GPIO28 GATE_PWM   -- slice 6A. RESOLVED IN FIRMWARE
//    2026-10-02 (ARCHITECTURE.md A7). Whichever pin was configured second used to
//    take over both, so LED brightness could have become the strobe current
//    setpoint. The ready LED now gives up the PWM block: panel.c drives it as SIO
//    on/off, and strobe.c owns slice 6 and refuses to raise the gate if GPIO12 is
//    ever found on GPIO_FUNC_PWM again. Never put GPIO12 back on PWM.
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
// 1% resistors can contribute +/-50 mV of error at 5 V by themselves. Since
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
// THIS IS THE FINAL CARRIER. Decided 2026-08-28: one frequency for every board
// and every user, chosen for uniformity over per-board SNR. A per-board carrier
// would need a scan, a `cal model` re-fit and a recorded frequency per board,
// repeated on every replacement -- for a few percent of signal.
//
// DO NOT pick a carrier with `scan carrier`'s "BEST by SNR". That command is a
// VERIFICATION now, not an optimisation, and its SNR column cannot rank
// frequencies anyway: the scan chops the beam, halving its average duty, so the
// LED cools through the run and `signal` climbs with ELAPSED TIME. Board 3's
// nine rows came out pre-sorted -- 1 in 363,000 by chance. See cal.c.
//
// The folding concern that motivated the scan is MEASURED CLEAR (2026-08-31).
// A square-wave demodulator folds anything near an odd harmonic n*fc down to
// |fi - n*fc|, and the only switcher tone on +5 V is a dithered band at
// 762.9-833.1 kHz. `scan carrier 95000 115000 9` put 7*fc INSIDE that band at
// three separate points and sigma_noise stayed flat, so it does not couple.
// (The 1.055 MHz nominal this comment used to quote was never confirmed and is
// ~24 % off the measured tone. See BENCH_P3_DETECT.md 3.6.)
#define CARRIER_TOP_DEFAULT  1439u
#define CARRIER_LEVEL_30PCT   432u   // 432/1440 = exactly 30.000 %
#define DEMOD_LEVEL_50PCT     720u

// ---------------------------------------------------------------------------
// HIGH-BANK PWM COLLISIONS -- the "16 apart" rule above is WRONG above GPIO32.
//
// The slice map earlier in this file says "any two GPIOs 16 apart collide",
// which is true for GPIO < 32 where slice = (gpio>>1)&7. Above 32 the SDK uses
// slice = 8 + ((gpio>>1)&3), so the index has period 8, NOT 16 -- and the
// collision pairs in the high bank are EIGHT apart. Verified against SDK 2.3.0
// PWM_GPIO_SLICE_NUM().
//
//   GPIO36 UART_TX      vs GPIO44 THRESHOLD_PWM  -> slice 10A  *** SEE BELOW ***
//   GPIO37 UART_RX      vs GPIO45 ADC_CH_DETECT  -> slice 10B  safe (ADC in)
//   GPIO39 DEMOD_PWM    vs GPIO47 ADC_CH_MIC     -> slice 11B  safe (ADC in)
//   GPIO33 HPF_TOGGLE   vs GPIO41 ADC_CH_5VIN    -> slice  8B  safe (ADC in)
//   GPIO35 (unassigned) vs GPIO43 RPI5_SHUTDOWN  -> slice  9B  safe (both SIO)
//   GPIO38 (unassigned) vs GPIO46 D_COMPARATOR   -> slice 11A  safe (input)
//
// *** GPIO36 / GPIO44 IS A LIVE PAIR. *** Threshold_PWM is a real PWM output as
// of Phase 3. UART_TX coexists ONLY because it stays SIO / GPIO_FUNC_UART.
// If GPIO36 is ever put on GPIO_FUNC_PWM, the UART to the Pi and the comparator
// threshold become one compare register: the threshold would move with the
// serial data, and the Pi link would carry the threshold duty. NEVER PUT GPIO36
// ON PWM. This is the same failure as CR-01, found the same way, one bank up.
//
// Design rule for the next board spin, corrected: below GPIO32 keep PWM
// functions off pins 16 apart; at or above GPIO32, off pins 8 apart.
//
// TWO MORE PAIRS, found 2026-08-14 by GENERATING the table instead of writing
// it by hand (tools/netlist_report.py). Neither is in the list above, and both
// are the kind of thing an ordinary-sounding change walks straight into:
//
//   GPIO2  SYSTEM_READY  vs GPIO18 LED_RED     -- slice 1A
//   GPIO3  IRQ_OUT       vs GPIO19 LED_YELLOW  -- slice 1B
//
// Both status LEDs are plain SIO on/off today, so nothing is broken. But "dim
// the status LEDs with PWM" is a completely reasonable request, and doing it
// would silently start toggling SYSTEM_READY or IRQ_OUT -- both Pi-facing
// signals. If the status LEDs ever need brightness control, use software PWM off
// the 50 Hz timer the way ARCHITECTURE.md A4 describes for the panel ring.
//
// THE FULL, GENERATED COLLISION TABLE LIVES IN /HARDWARE_REFERENCE.md section 11.
// This comment is a summary of it and can go stale; that file cannot, because it
// is regenerated from the netlist and `--check` fails if it drifts.
// ---------------------------------------------------------------------------

// NOTE: there are deliberately no PWM_SLICE_* constants here.
//
// There used to be four, and THREE OF THEM WERE WRONG -- they carried the same
// 3B/7B/2A numbering that the PWM SLICE MAP above was corrected for on
// 2026-07-31, and were simply missed in that pass. Nothing referenced them, so
// the error was invisible. Every slice in this firmware is resolved at runtime
// with pwm_gpio_to_slice_num() / pwm_gpio_to_channel(), which cannot go stale
// when a pin moves. Keep it that way: if you need a slice number, ask the SDK.
//
// U9 BEAM ONE-SHOT CLAMP.
//
// Modulation_PWM feeds a 74LVC1G123 monostable wired A=GND, B=~CLR. A rising
// edge triggers it; the LED high phase is min(commanded, t_w) and NOTHING can
// defeat it. Measured on this board 2026-08-13: t_w = 122.68 us, spread 0.22 us
// over 1291 pulses.
//
// This is SEPARATE from STROBE_HW_LIMIT_US below even though both are the same
// part and RC. They have already diverged: U5 measured 135 us on board 1 against
// U9's 122.68 us. A shared constant would silently move the beam's safety check
// with the strobe's.
#define BEAM_ONESHOT_CLAMP_US    122u    // U9, MEASURED 2026-08-13 (122.68 us)

// Average-current ceiling for the beam LED, as an EFFECTIVE duty after the U9
// clamp -- see beam_effective_duty(). 35 % is the design ceiling; CR-12 lowered
// the intended sustained operating point to 25 % (junction ~87.5 C) because 30 %
// puts it at 123-133 C against a 145 C max.
#define BEAM_DUTY_CEILING      0.35f
#define BEAM_DUTY_OPERATING    0.25f   // Phase 3 onward; see NEXT_BOARD_REV CR-12

// DAC PWM: TOP+1 = 1024 -> 146.5 kHz, 3.2 mV steps.
//
// Shared by both PWM DACs, because their filters are identical 10K/0.1uF ladders
// (netlist, rechecked 2026-10-02): the comparator threshold DAC (GPIO44, R86/C74
// then R89/C76) and the strobe gate DAC (GPIO28, R55/C52 then R58/C53).
//
// Filtered by R86/C74 then R89/C76 (10K/0.1uF each). Those are NOT two
// independent 1 ms poles -- the second section loads the first, so the real
// poles of the cascaded ladder are at RC/0.382 = 2.62 ms and RC/2.618 = 0.382 ms.
// Settle to 5 tau of the DOMINANT pole, not of the isolated 1 ms product.
#define DAC_TOP              1023u
#define DAC_SETTLE_MS          20u    // 5 x 2.62 ms dominant pole, rounded up

// ---------------------------------------------------------------------------
// U5 STROBE ONE-SHOT CLAMP -- MEASURED.
//
// The number moved three times, so the history matters. The .md quoted 113 us
// for both 74LVC1G123 one-shots. The datasheet's K ~= 0.7 at 3.3 V predicted
// ~86 us from R = 56K, C = 2.2 nF. U9 -- the identical circuit, BOM-confirmed --
// measured 122.68 us on 2026-08-13 (spread 0.22 us over 1291 pulses), so K ~ 1.0.
// U5 itself measured 135 us on board 1 in 6a.1 (2026-10-06), the same at 200 and
// 1000 us commanded. The RC therefore varies ~10 % between parts.
//
// STROBE_HW_LIMIT_US is a WORST CASE, not one board's number: board 1's 135 us
// plus a little for parts not yet measured. It is the longest pulse the
// hardware can deliver, which is what the charge interlock must assume when a
// request is longer than the clamp. A board that measures above it in 6a.1
// raises it. (It was STROBE_HW_LIMIT_US_ASSUMED = 122, U9's value, until
// 2026-10-07.)
//
// STROBE_SW_MAX_US must stay BELOW every board's clamp, or slow-ball pulses get
// truncated by hardware instead of controlled by firmware. 100 us sits 35 us
// under board 1's U5. It also meets the .md S15 slow-ball row (100 us at
// 10 m/s); the 73 us limit before 2026-08-13 did not. A board whose U5 measures
// under ~110 us needs a look at this margin.
#define STROBE_HW_LIMIT_US         137    // U5 worst case; board 1 measured 135 us (6a.1)
#define STROBE_SW_MAX_US           100    // meets .md S15 at 10 m/s; 35 us under board 1's U5
#define STROBE_MIN_GAP_US          150    // let the VIR bulk caps breathe between pulses

// How long past the nominal enable->IRQ0 time a firing may run before the engine
// is stopped and the run reported as TIMEOUT.
#define STROBE_TIMEOUT_MARGIN_US 20000u

// Per-burst charge ceiling, in millicoulombs. BENCH_P6_STROBE 6a.3 has referred
// to this constant since it was written; it did not exist until 2026-08-28.
//
// Charge per burst is (pulses x width x current). At the design 10 pulses and
// 9 A, the .md S15 schedule gives:
//
//   ball    width    charge      vs 6.0 mC
//   90 m/s   11 us   0.99 mC     fine
//   50 m/s   20 us   1.80 mC     fine
//   20 m/s   50 us   4.50 mC     fine, 25 % margin
//   10 m/s  100 us   9.00 mC     *** OVER -- the interlock must SHED pulses ***
//
// So the slow-ball row is the only one that trips it, and shedding there is the
// designed behaviour rather than an error. The design (.md 13.4) sheds pulses
// and KEEPS THE SPACING, down to BURST_PULSES_MIN: at 10 m/s the burst becomes
// 6 pulses spanning 21.4 ms instead of 10 spanning 38.5 ms -- fewer freeze
// positions, not sparser ones. (This comment used to say shedding "costs sample
// density, not coverage". That contradicted the design and is withdrawn.)
//
// What the number is: a VIR sag budget, Q = C x dV = ~670 uF x ~9 V (.md 13.1,
// section 15), keeping headroom over the LED string Vf. It is not a thermal
// limit, and the 670 uF effective bulk has never been measured.
//
// 🔴 UNVALIDATED on the real bank. 6a.3 confirms the interlock sheds at 10 m/s;
// 6c is where it meets real current. Do not raise it to make a test pass.
#define BURST_CHARGE_MAX_MC        6.0f
#define BURST_PULSES_NOMINAL       10u    // S15 assumes 10 per burst, 9 gaps
#define BURST_PULSES_MIN            3u    // .md 13.4: shed no further than this

// Schedule inputs from the design (.md 13.1 config, 13.4 compute_schedule()).
// Integer micrometres, so the section-15 table rounds exactly: as a float,
// 42.67 is 42.66999817..., which would round the 20 m/s period (exactly
// 2133.5 us) down to 2133 instead of 2134.
#define STROBE_BLUR_BUDGET_UM      1000u   // max ball travel during one pulse
#define STROBE_FREEZE_SPACING_UM  42670u   // one ball diameter between freezes
#define STROBE_V_MIN_MPS            2.0f   // .md 13.1 plausibility window
#define STROBE_V_MAX_MPS          100.0f

// The DESIGN target (2 strings x 4.5 A). It is what `strobe cal` solves for by
// default (6c) and the current at which burst charge is evaluated -- always the
// design value, never a measured one, so the charge limits stay pessimistic.
#define STROBE_TARGET_CURRENT_A     9.0f

// The DAC filter output feeds U6A, a non-inverting stage referenced to ground:
// gain = 1 + R60 20K / R59 10K = 3 (netlist 2026-10-02). U7, a complementary
// emitter follower, drives TP3 and closes the loop, so TP3 = 3 x the filtered
// DAC. U6 runs from +12 V, so TP3 cannot reach 3 x 3.3 V = 9.9 V exactly: expect
// it to flatten somewhere near the top. R59 + R60 also load TP3 with 30K to GND.
#define STROBE_GATE_AMP_GAIN        3.0f

// Bench-command bounds. Written without a `u` suffix so cli.c can stringify them
// into `help`, which keeps the help text from drifting away from the limits.
#define STROBE_MIN_WIDTH_US          5     // .md 13.4 clamp floor; PIO encodes >= 3
#define STROBE_CLAMPTEST_MAX_US   2000     // 6a.1 only: deliberately past the U5 clamp
#define STROBE_MAX_GAP_US        50000     // sanity bound on a typed manual gap
#define STROBE_BURST_MAX_PULSES     16

// ---------------------------------------------------------------------------
// 6c/6d LIVE MODE -- the first firmware that can command LED-bank current.
//
// Bench guards, not production limits: Phase 7's firing path will need its own.
// The policy is in strobe_plan.c and the measurement in strobe_live.c, both
// host-tested.
//
// THE ESTIMATE THE GUARDS ARE SIZED FROM -- NOT MEASURED; 6c measures it. There
// is no analog servo: current = (TP3 - Vgs(Q9)) / (R65||R66 0.135 R + Q10's
// Rds(on) ~0.02 R), so at most ~6 A per volt at TP3, less once Q9's own
// transconductance is counted. One DAC level is 3.29 V x 3 / 1024 = 9.6 mV at
// TP3, so at most ~0.06 A. The design expects TP3 ~4.5-6 V at 9 A.
// ---------------------------------------------------------------------------

// Gate ceiling while live: 717 of 1024 = 70.0 % (what `strobe gate 70` asks for)
// -> TP3 ~6.9 V, above the 4.5-6 V the design expects at 9 A. `strobe cal`
// fails at it rather than pass it.
#define STROBE_LIVE_GATE_CEILING      717u

// THE STAIRCASE RULE. While live, the gate may be raised to at most this many
// levels above the highest level already FIRED AND MEASURED within limits since
// live mode was armed. 31 levels = 3.0 % = 0.30 V at TP3 = at most ~1.8 A. So no
// typed command can jump from a measured current to an unmeasured one more than
// ~1.8 A higher. Arming starts the staircase again from 0. Typed steps of 3 %
// (`strobe gate 3`, 6, 9 ... 69, 70) always fit: 3 % rounds to 30 or 31 levels.
#define STROBE_LIVE_GATE_STEP_MAX      31u

// Stop current: 1.2 x the 9 A design target (BENCH_P6 6c). A plateau above
// I_STOP, or any single sample above I_PEAK_STOP, latches
// FAULT_STROBE_OVERCURRENT and leaves live mode with the gate at 0.
#define STROBE_LIVE_I_STOP_A         10.8f
#define STROBE_LIVE_I_PEAK_STOP_A    13.0f

// Pacing. Pulses come from the VIR bulk caps and the PSU only sees the average:
//   - at least 100 ms between live firings (a burst is one firing);
//   - at most 30 mC in any 10 s, booked BEFORE firing at the 9 A design current
//     and the clamp-limited width -- pessimistic by construction. 30 mC per 10 s
//     is 3 mA average from VIR. A 20 us pulse books 0.18 mC; a burst at the
//     6 mC BURST_CHARGE_MAX_MC limit books 6 mC, so five of those per 10 s.
#define STROBE_LIVE_MIN_INTERVAL_MS     100u
#define STROBE_LIVE_BUDGET_MC          30.0f
#define STROBE_LIVE_BUDGET_WINDOW_MS  10000u
#define STROBE_LIVE_BUDGET_SLOTS        128u   // > window / interval: cannot fill

// Live mode drops itself after this long with no strobe command: gate to 0,
// and the watchdog it armed back off.
#define STROBE_LIVE_IDLE_TIMEOUT_MS  300000u

// ADC0 readback. BURST mode is ch0 alone at 500 ksps = 2 us per sample, into the
// 16384-sample ring (32.768 ms). A live firing's enable->IRQ0 time must fit in
// SPAN_MAX, leaving room for the PRE (baseline) and POST (turn-off) windows.
// The S15 schedules at 10 m/s and faster span <= 21.4 ms, so they fit.
#define STROBE_LIVE_SAMPLE_US             2u
#define STROBE_LIVE_PRE_US              200u
#define STROBE_LIVE_POST_US             200u
#define STROBE_LIVE_SPAN_MAX_US       30000u
#define STROBE_LIVE_WAVE_MAX_SAMPLES  15360u   // 30.72 ms: SPAN_MAX + PRE + POST

// Pulse detection, in ADC codes (0.806 mV = 6.0 mA at 135 mV/A):
//   - a pulse is a run of samples more than max(DETECT_MIN, half the peak)
//     above the baseline;
//   - a baseline (the mean of the window before the first edge) above
//     BASELINE_MAX means current was flowing BEFORE the pulse.
#define STROBE_LIVE_DETECT_MIN_CODES     12u   // ~72 mA
#define STROBE_LIVE_BASELINE_MAX_CODES   40u   // ~0.24 A

// Applied to EVERY live firing: current on for longer than this, still on at
// the end of the window, or flowing before the pulse means the U5 clamp or Q10
// did not end it -> FAULT_STROBE_CLAMP. (U5 measured 135 us.)
#define STROBE_LIVE_MAX_ON_US           200u

// 6d: `strobe clamptest` while live is admitted only at the gate level of the
// most recent live firing, and only if that firing measured 0.5-2.5 A -- "low
// only, ~2 A" (BENCH_P6 6d), and enough current that TP4 shows the pulse.
#define STROBE_6D_MIN_A                 0.5f
#define STROBE_6D_MAX_A                 2.5f

// `strobe cal [A]`: one 20 us pulse per step. Coarse steps while no current is
// detected (Q9 below threshold), fine steps once it is. Abort above 1.2 x the
// target; fail at the ceiling. Both step sizes are under the staircase limit.
#define STROBE_CAL_WIDTH_US              20u
#define STROBE_CAL_COARSE_STEP           20u   // ~2.0 %, <= ~1.2 A
#define STROBE_CAL_FINE_STEP              5u   // ~0.5 %, <= ~0.3 A
#define STROBE_CAL_ABORT_RATIO          1.2f
// 2 A, not lower: below it the first coarse step that shows current could land
// over 1.2 x the target (tests: cal_steps_cannot_overshoot). 6d's ~2 A is in range.
#define STROBE_CAL_MIN_A                2.0f
#define STROBE_CAL_CONFIRM_PULSES         3u
#define STROBE_CAL_CONFIRM_TOL          0.05f  // +-5 % of the target
#define STROBE_CAL_MAX_POINTS           160u

// ===========================================================================
// Pi 5 SOFT-SHUTDOWN -- polarity decision
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
// MEASURED 2026-07-31 (PROGRESS.md Q10): with a Pi seated, that pull-down (~34K,
// stronger than assumed) divides against gpio-shutdown's ~50K pull-up through
// R39's 1K, so a real Pi would see ~1.34 V at J8.37 -- below RP1's VIH. The
// level fails, but it is defence-in-depth, not a gate: every reset also opens
// the +5V latch, so the Pi is losing power in the same instant. Optional: a 10K
// pull-up from J8.37 to the always-on +3V3 (NEXT_BOARD_REV.md CR-04).
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
