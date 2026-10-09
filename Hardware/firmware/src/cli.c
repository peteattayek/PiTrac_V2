// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "cli.h"
#include "board.h"
#include "safe_state.h"
#include "adc_engine.h"
#include "power_fsm.h"
#include "panel.h"
#include "beam.h"
#include "detect.h"
#include "pio_alloc.h"
#include "cal.h"
#include "config_store.h"
#include "service.h"
#include "strobe.h"
#include "strobe_plan.h"

#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"   // clock_get_hz / clk_sys, for the `id` command
#include "hardware/pwm.h"                 // beam hardware readback
#include "hardware/structs/padsbank0.h"   // pad ISO bit (RP2350-specific)
#include "pico/bootrom.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#define CLI_MAX_LINE 96
#define CLI_MAX_ARGS 8

// Fitted by `cal model`; consumed by `scan carrier`. Persisted by `cfg save`
// and restored by cli_init().
static cal_phase_model_t s_phase_model;

static char   s_line[CLI_MAX_LINE];
static size_t s_len;
// A character did not fit in s_line. The line is refused at Enter rather than
// dispatched with its tail missing -- a truncated argument is still a valid one.
static bool   s_overflow;

// ---------------------------------------------------------------------------

// How long `level` will run unattended before giving up. It holds the beam on
// and chopping, which is a LIGHTER thermal load than steady operation (50 % on),
// so this is about not leaving the bench in an unknown state rather than heat.
#define LEVEL_MAX_MS  300000u

// How long `level` reads LOW before the gated HPF has settled, and its output
// means anything. Starting the chop is a step into a 0.66 s high-pass (R96 2M
// with C81 330 nF), so the first readings climb toward the true value rather
// than sitting at it.
//
// Measured 2026-08-28 from level's own output: the deficit from the final value
// fell by a factor 0.736 per 200 ms reading, i.e. tau = 0.653 s -- the HPF's own
// time constant, recovered by the command from its own numbers. 5 tau is 3.3 s,
// so mark everything below 3.5 s as settling rather than letting an operator
// read "9 %" and reach for the target.
#define LEVEL_SETTLE_MS  3500u

// Lets `help` quote the strobe limits from board.h instead of restating them.
#define CLI_STR(x)  #x
#define CLI_XSTR(x) CLI_STR(x)

// ---------------------------------------------------------------------------
// Strict argument parsing. EVERY numeric argument in this file goes through
// these. The whole argument must be a number: strtoul("2O") returns 2 and
// strtof("x") returns 0, so `gpio xyz` used to read GPIO0 and `cal gain 2O0`
// used to solve for a peak of 2 codes. Until 2026-10-05 only `strobe` used them.
// ---------------------------------------------------------------------------

// Decimal, or hex with a 0x prefix (capture masks are written that way). No
// octal: "010" is ten. errno catches overflow -- on this target unsigned long is
// 32 bits, so strtoul saturates to 0xFFFFFFFF and a range test alone cannot.
static bool parse_u32(const char *s, uint32_t *out) {
    if (!s || *s < '0' || *s > '9') return false;
    int base = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10;
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, base);
    if (*end != '\0' || errno == ERANGE || v > 0xFFFFFFFFul) return false;
    *out = (uint32_t)v;
    return true;
}

static bool parse_u32_in(const char *s, uint32_t lo, uint32_t hi, uint32_t *out) {
    uint32_t v;
    if (!parse_u32(s, &v) || v < lo || v > hi) return false;
    *out = v;
    return true;
}

static bool parse_i32(const char *s, int32_t *out) {
    if (!s) return false;
    const char *d = (*s == '-' || *s == '+') ? s + 1 : s;
    if (*d < '0' || *d > '9') return false;
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || errno == ERANGE || v < INT32_MIN || v > INT32_MAX) return false;
    *out = (int32_t)v;
    return true;
}

static bool parse_float(const char *s, float *out) {
    if (!s || !*s) return false;
    char *end = NULL;
    float v = strtof(s, &end);
    if (end == s || *end != '\0' || !isfinite(v)) return false;
    *out = v;
    return true;
}

static bool parse_float_in(const char *s, float lo, float hi, float *out) {
    float v;
    if (!parse_float(s, &v) || v < lo || v > hi) return false;
    *out = v;
    return true;
}

static void bad_arg(const char *what, const char *got, const char *want) {
    printf("ERR: %s '%s' -- want %s. Nothing was changed.\n", what, got, want);
}

static const char *k_help =
    "\n"
    "PiTrac RP2354 bench CLI\n"
    "  help                       this text\n"
    "  id                         firmware + chip identity\n"
    "  stat                       power state, rails, faults\n"
    "  pins                       read every signal pin\n"
    "\n"
    "  adc <ch> [n]               oversampled read (ch: 0,1,2,5,7)\n"
    "  adc5v                      +5V_IN in volts (divider + cal applied)\n"
    "  adc5vcal <measured_volts>  trim the +5V_IN scale against a DMM\n"
    "  capture <mask> <n> <rate>  block capture -> CSV. mask is a channel bitmask\n"
    "  capture trig <ch> [thr] [rate] [pre%] [tmo_s]\n"
    "                             TRIGGERED single shot. Runs the DMA in a ring\n"
    "                             and stops AFTER the trigger, so the buffer holds\n"
    "                             the baseline and the leading edge. 80 codes,\n"
    "                             500 ksps, 25% pre-trigger, 30 s by default.\n"
    "                             e.g. 'capture 0x20 2000 250000' = ADC5 @ 250 ksps\n"
    "  adcmode off|idle|armed|burst\n"
    "\n"
    "  on                         request power on from STANDBY (USB guard applies)\n"
    "                             refused elsewhere; never queues a later restart.\n"
    "  off                        request orderly shutdown\n"
    "  forceoff                   drop the latch immediately\n"
    "  pisim                      show the simulated-Pi input levels\n"
    "\n"
    "  gpio <n>                   read a pin\n"
    "  gpio <n> <0|1>             drive a pin (outputs only; guarded)\n"
    "  led r|y <0|1>              on-board status LEDs (+3V3, work in standby)\n"
    "  panel pwr <0-100|auto>     power LED on J7 (needs the +5V rail latched)\n"
    "  panel rdy <0-100|auto>     ready LED on J7: on/off only, >= 50 lights it\n"
    "                             (GPIO12 gave PWM slice 6 to the strobe gate, A7)\n"
    "  panel test                 ramp both LEDs 0->100->0 for a current check\n"
    "  panel pattern <p|auto>     force a ring pattern; p = off powering booting\n"
    "                             running shutdown fault\n"
    "  panel demo                 play every pattern in turn, ~4 s each\n"
    "\n"
    "\n"
    "  -- Phase 2: beam (needs the +5V rail up) --\n"
    "  beam                       show carrier/demod state\n"
    "  beam on | off\n"
    "  beam freq <hz>             use 104166 (actual 104166.67 Hz, TOP=1439)\n"
    "                             fixed carrier for every board; 25% at level 360.\n"
    "  beam duty <pct>            IMMEDIATE. 25% operating maximum (CR-12).\n"
    "                             use 'beam ramp' above ~5%.\n"
    "  beam ramp <pct> [step_ms]  gradual, 1% steps -- watch temps as it climbs\n"
    "  beam phase <ticks>         demod offset, 0..TOP (1 tick = 6.67 ns = 0.25 deg)\n"
    "  beam clamp                 1 kHz / 50% -- scope TP5 to measure the U9 clamp (Q1)\n"
    "  beam sweep <f0> <f1> <n> <dwell_ms>   duty-fidelity sweep (Q2)\n"
    "\n"
    "\n"
    "  -- Phase 3: detection (needs the +5V rail up) --\n"
    "  threshold                  show threshold DAC + comparator state\n"
    "  threshold duty <pct>       set Threshold_DC via GPIO44 (TP8 = 3.3V x duty)\n"
    "  threshold volts <v>\n"
    "  threshold sweep [lo] [hi] [steps]   find the comparator flip point (3.5)\n"
    "  hpf                        show baseline mode (TRACK / HOLD)\n"
    "  hpf track | hpf hold\n"
    "  hpf test [ms]              per-board polarity + switch-leakage measurement.\n"
    "                             'hpf' shows this board's saved polarity status.\n"
    "  detect                     comparator + PIO transit timer status\n"
    "  detect arm | disarm        start/stop the PIO edge timer; arm resets counters.\n"
    "                             HPF unchanged; use HOLD for ball transits.\n"
    "  detect coalesce <us>       chatter-merge window (U15 has no hysteresis)\n"
    "  detect path <mm>           beam width; no velocity is reported until set\n"
    "  detect cond <0|1|2>        tag passes: 0 nominal 1 far 2 low-reflectance\n"
    "  detect log | stats | clear per-pass CSV / the Phase 4 table / reset\n"
    "  detect wave [seq]          dump a retained waveform as CSV\n"
    "\n"
    "  cfg                        show the saved calibration (carrier, phase, model)\n"
    "  cfg save                   persist it. Refuses unless the machine is quiet\n"
    "  cfg default                reset the in-RAM copy (does not erase flash)\n"
    "  level                      LIVE % of full scale -- aim the target with this\n"
    "                             BEFORE running cal demod. target 50-70 %.\n"
    "  cal demod                  sweep the demod phase and commit it (3.4, ~26 s)\n"
    "  cal model [f0] [f1] [n]    fit phase vs frequency across a band (~128 s).\n"
    "                             NOTHING may move for the whole run.\n"
    "  cal gain <peak> [frac]     solve R98 from a measured transit peak (3.7)\n"
    "  scan carrier [f0] [f1] [n] verify carrier robustness (3.6, minutes).\n"
    "                             NOT frequency selection: carrier stays 104166.67 Hz.\n"
    "                             needs a fitted phase model; run in FINAL geometry.\n"
    "\n"
    "  -- Phase 6: strobe. DRY by default (6a/6b): no LED-bank current possible --\n"
    "  -- LIVE (6c/6d) only after 'strobe live on confirm': REAL LED-BANK CURRENT --\n"
    "  strobe                     gate DAC, pulse engine, interlocks, live state, limits\n"
    "  strobe gate <pct>          current-setpoint DAC 0-100 %; TP3 ~ 3 x DAC volts.\n"
    "                             dry: raised only while no pulse can start.\n"
    "                             live: at most ~3 % above the highest level already\n"
    "                             fired and measured, and under the ~70 % ceiling\n"
    "  strobe pulse <us>          ONE pulse, " CLI_XSTR(STROBE_MIN_WIDTH_US) "-"
                                  CLI_XSTR(STROBE_SW_MAX_US) " us. dry: needs the gate at 0.\n"
    "  strobe burst <w> <gap> <n> uniform PIO/DMA burst: gap "
                                  CLI_XSTR(STROBE_MIN_GAP_US) "-" CLI_XSTR(STROBE_MAX_GAP_US)
                                  " us, n <= " CLI_XSTR(STROBE_BURST_MAX_PULSES) ",\n"
    "                             burst charge limit at the design current\n"
    "  strobe clamptest <us>      ONE pulse over " CLI_XSTR(STROBE_SW_MAX_US) ", up to "
                                  CLI_XSTR(STROBE_CLAMPTEST_MAX_US) " us: U5 clamp (6a.1).\n"
    "                             live (6d): only at a level that measured 0.5-2.5 A\n"
    "  strobe sched <m/s> [fire]  section-15 schedule; 'fire' runs it (dry or live)\n"
    "  strobe off                 gate to 0, engine stopped, GPIO25 low, live mode off\n"
    "  strobe live                live state: staircase, budget, why it last ended\n"
    "  strobe live on [confirm]   'on' prints the checklist; 'on confirm' ARMS live\n"
    "                             mode (from gate 0; arms the watchdog; limits the CLI)\n"
    "  strobe live off            leave live mode: gate 0, engine stopped\n"
    "                             live: every firing is read back on ADC0. Overcurrent\n"
    "                             or current outside the pulse latches a fault\n"
    "  strobe cal [A]             live: solve the gate level for A amps (default 9.0)\n"
    "  strobe wave                last live ADC0 record as CSV, 2 us per sample\n"
    "\n"
    "  fault                      show / 'fault clear' (also acks FAULT -> STANDBY)\n"
    "  reset [force]              soft reset. REFUSED while a Pi is powered:\n"
    "                             a reset drops the latch = hard power cut\n"
    "  bootsel [force]            reboot to USB mass storage (same guard).\n"
    "                             both disarm the watchdog first.\n"
    "  wdog [on|off]              hardware watchdog. OFF by default -- a reset\n"
    "                             DROPS THE LATCH. Live strobe mode arms it itself.\n";

// ---------------------------------------------------------------------------

static void cmd_id(void) {
    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);
    // Phases the image actually implements. Was hardcoded "phase0/1/1b" and
    // stayed that way through phases 2, 3 and 4 -- a version string nobody
    // updates is worse than none, because it reads as authoritative.
    printf("fw       : pitrac phases 0-4 + mic capture (block/triggered)\n"
           "           + strobe 6a/6b dry tests + 6c/6d live current (guarded, ADC0 per firing)\n");
    printf("pending  : mic onset/veto, camera handshake, Pi integration\n");
    printf("built    : " __DATE__ " " __TIME__ "\n");
    printf("board    : pitrac_ltb_v1 (RP2354B, 48 GPIO, 2MB internal flash)\n");
    printf("uid      : ");
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) printf("%02x", uid.id[i]);
    printf("\n");
    printf("sysclk   : %u Hz\n", (unsigned)clock_get_hz(clk_sys));
    printf("NOTE: run 'picotool info -a' on the host for the die revision;\n");
    printf("      erratum E9 (input+pulldown latching ~2.2V) affects GPIO0/8/9 here.\n");
}

// ---------------------------------------------------------------------------
// Reset guard.
//
// `reset` and `bootsel` do not merely restart the MCU. Both reset the pads, so
// GPIO15 goes high-Z, R12 pulls Q2's gate low, and the +5V latch OPENS. With a
// Pi on the header that is a hard power cut mid-write -- no shutdown request, no
// filesystem sync, no warning. Netlist net 62 "LATCH_CONTROL" = Q2 gate + R12 +
// GPIO15, so this is structural, not a firmware choice.
//
// Guarded rather than forbidden: there are legitimate reasons to reflash with a
// Pi attached. It just must not happen by reflex on a console.
// ---------------------------------------------------------------------------
static bool reset_guard_ok(int argc, char **argv, const char *cmd) {
    // A Pi we know about, and a latch that is currently feeding it.
    if (!(power_pi_present() && gpio_get_out_level(PIN_LATCH_CONTROL))) return true;
    if (argc > 1 && !strcmp(argv[1], "force")) return true;

    printf("REFUSED: a Pi is present and powered (state %s).\n"
           "  '%s' resets the pads -> GPIO15 high-Z -> R12 pulls the latch open ->\n"
           "  the Pi loses +5V instantly. No shutdown, no sync, no warning.\n"
           "  Shut the Pi down first ('off', or the panel button), then retry.\n"
           "  If you really mean it: '%s force'\n",
           power_state_name(power_fsm_state()), cmd, cmd);
    return false;
}

static void cmd_stat(void) {
    printf("state    : %s (%lu ms)\n", power_state_name(power_fsm_state()),
           (unsigned long)power_state_elapsed_ms());
    printf("fault    : %s\n", fault_name(fault_current()));
    printf("5V_IN    : %.3f V   (latch guard %.2f V, load floor %.2f V, scale %.4f)\n",
           (double)adc_read_5vin_volts(), (double)V5_MIN_FOR_LATCH,
           (double)V5_MIN_UNDER_LOAD, (double)adc_get_5vin_scale());
    printf("latch    : %d   railsready %d\n",
           gpio_get_out_level(PIN_LATCH_CONTROL), power_rails_ready());
    printf("pi       : present %d  3v3 %d  userspace %d  isdown %d\n",
           power_pi_present(), gpio_get(PIN_PI_3V3_SENSE),
           gpio_get(PIN_RPI5_ON), power_pi_is_down());
    printf("button   : %s\n", gpio_get(PIN_PWR_TOGGLE) ? "released" : "PRESSED");
    {
        uint32_t age = adc_5vin_age_ms();
        printf("adc      : mode %d   ring %s   5V_IN %s\n",
               (int)adc_engine_mode(),
               adc_ring_running() ? "RUNNING" : "*** STOPPED ***",
               age == UINT32_MAX ? "never read" : (age == 0 ? "fresh" : "HELD"));
        // ch1 is not in the ARMED {5,7} or BURST {0} round-robin, so the reading
        // is held rather than the ADC being stopped to go and fetch one (A1).
        if (age && age != UINT32_MAX)
            printf("           +5V_IN last sampled %lu ms ago\n", (unsigned long)age);
    }
    // A watchdog that can reset the board -- and so drop the latch -- must be
    // visible. It defaults OFF and is armed only deliberately; see service.h.
    printf("watchdog : %s%s\n",
           pitrac_watchdog_enabled() ? "ARMED (1 s)" : "off",
           pitrac_watchdog_enabled() ? "   a reset here DROPS THE LATCH" : "");
}

static void cmd_pins(void) {
    struct { const char *n; uint p; } in[] = {
        {"PWR_TOGGLE(14,act-low)", PIN_PWR_TOGGLE},
        {"D_COMPARATOR(46)",       PIN_D_COMPARATOR},
        {"PI_3V3_SENSE(24)",       PIN_PI_3V3_SENSE},
        {"RPI5_ON(0)",             PIN_RPI5_ON},
        {"CAM_STROBE_0(8)",        PIN_CAM_STROBE_0},
        {"CAM_STROBE_1(9)",        PIN_CAM_STROBE_1},
    };
    printf("-- inputs --\n");
    for (size_t i = 0; i < count_of(in); i++)
        printf("  %-24s %d\n", in[i].n, gpio_get(in[i].p));

    struct { const char *n; uint p; } out[] = {
        {"LATCH_CONTROL(15)",   PIN_LATCH_CONTROL},
        {"RPI5_SHUTDOWN(43)",   PIN_RPI5_SHUTDOWN},
        {"SYSTEM_READY(2)",     PIN_SYSTEM_READY},
        {"STROBE_PULSE(25)",    PIN_STROBE_PULSE},
        {"PULSE_LIMIT_DIS(27)", PIN_PULSE_LIMIT_DIS},
        {"MOD_PWM(31)",         PIN_MOD_PWM},
        {"HPF_TOGGLE(33)",      PIN_HPF_TOGGLE},
        {"CAM_TRIGGER(10)",     PIN_CAM_TRIGGER},
        {"USB_ENABLE(32)",      PIN_USB_ENABLE},
        {"LED_RED(18)",         PIN_LED_RED},
        {"LED_YELLOW(19)",      PIN_LED_YELLOW},
    };
    printf("-- outputs --\n");
    for (size_t i = 0; i < count_of(out); i++)
        printf("  %-24s %d\n", out[i].n, gpio_get_out_level(out[i].p));
#if PI_SHUTDOWN_ACTIVE_LOW
    printf("  (RPI5_SHUTDOWN is ACTIVE LOW: 1 = deasserted)\n");
#else
    printf("  (RPI5_SHUTDOWN is ACTIVE HIGH: 0 = deasserted)\n");
#endif
}

// Passed to adc_capture_triggered() so the wait services the FSM and can be
// broken by a keypress. Keeps adc_engine.c free of any service-layer include.
static bool capture_wait_poll(void) {
    pitrac_service();
    return pitrac_abort_pending();
}

static void cmd_capture_trig(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: capture trig <ch> [threshold] [rate_hz] [pre%%] [timeout_s]\n"
               "  defaults: threshold 80, rate 500000, pre 25%%, timeout 30 s\n"
               "  Runs the DMA continuously and stops it AFTER the trigger, so the\n"
               "  buffer contains the baseline and the leading edge -- which a\n"
               "  start-on-command capture can never hold.\n");
        return;
    }
    uint32_t ch_u, thr_u = 80u, rate = 500000u, pre = 25u, tmo_s = 30u;
    if (!parse_u32(argv[2], &ch_u)) { bad_arg("channel", argv[2], "0, 1, 2, 5 or 7"); return; }
    if (argc > 3 && !parse_u32_in(argv[3], 0u, 4095u, &thr_u)) {
        bad_arg("threshold", argv[3], "0-4095 codes"); return;
    }
    if (argc > 4 && !parse_u32_in(argv[4], 1u, 0xFFFFFFFFu, &rate)) {
        bad_arg("rate", argv[4], "a sample rate in Hz"); return;
    }
    // adc_capture_triggered() clamps pre-trigger to 90 %; refuse more here so
    // the "armed:" line never reports a split the capture is not using.
    if (argc > 5 && !parse_u32_in(argv[5], 0u, 90u, &pre)) {
        bad_arg("pre-trigger", argv[5], "0-90 %"); return;
    }
    // Bounded so the x1000 to milliseconds cannot wrap into a short timeout.
    if (argc > 6 && !parse_u32_in(argv[6], 1u, 3600u, &tmo_s)) {
        bad_arg("timeout", argv[6], "1-3600 s"); return;
    }
    uint     ch   = (uint)ch_u;
    uint16_t thr  = (uint16_t)thr_u;
    float    pref = (float)pre / 100.0f;
    uint32_t tmo  = tmo_s * 1000u;

    if (ch > 7 || !((ADC_VALID_MASK >> ch) & 1u)) {
        printf("ERR: ch%u is not an analog input (valid: 0,1,2,5,7)\n", ch);
        return;
    }

    uint16_t base = 0;
    size_t   trig = 0;
    printf("armed: ch%u, threshold %u codes, %lu Hz, %.0f%% pre-trigger.\n",
           ch, thr, (unsigned long)rate, (double)(pref * 100.0f));
    // "  window" must stay the line prefix: tools/scope.py keys on it.
    printf("  window %.1f ms. Trigger the event now (%s). Any key aborts.\n",
           (double)ADC_CAPTURE_MAX_SAMPLES * 1000.0 / (double)rate,
           ch == (uint)ADC_CH_DETECT ? "roll the ball"
               : (ch == (uint)ADC_CH_MIC ? "make the sound" : "the stimulus"));

    adc_trig_result_t r = adc_capture_triggered(ch, ADC_CAPTURE_MAX_SAMPLES, rate,
                                                thr, pref, tmo,
                                                capture_wait_poll, &base, &trig);
    switch (r) {
        case ADC_TRIG_TIMEOUT:
            printf("NO TRIGGER: nothing reached %u codes from a baseline of %u\n"
                   "  in %lu s. Lower the threshold, or get closer.\n",
                   thr, base, (unsigned long)(tmo / 1000u));
            return;
        case ADC_TRIG_ABORTED: printf("aborted.\n"); return;
        case ADC_TRIG_ERROR:   printf("ERR: bad arguments\n"); return;
        case ADC_TRIG_OK:      break;
    }

    size_t got = adc_capture_count();
    printf("# capture mask=0x%02x n=%u rate=%lu overran=%d trig=%u base=%u\n",
           adc_capture_mask(), (unsigned)got, (unsigned long)adc_capture_rate(),
           adc_capture_overran(), (unsigned)trig, base);
    printf("# columns: ch%u\n", ch);
    const uint16_t *b = adc_capture_buffer();
    const size_t start = adc_capture_start();
    for (size_t i = 0; i < got; i++) {
        printf("%u\n", b[(start + i) % ADC_CAPTURE_MAX_SAMPLES]);
        // A full dump is 16384 lines over CDC. Keep the FSM, the supply monitor
        // and an armed watchdog fed while it drains.
        if ((i % 256) == 0) pitrac_service();
    }
    printf("# end\n");
    if (adc_capture_overran())
        printf("WARN: FIFO overran -- samples are not evenly spaced.\n");
}

static void cmd_capture(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "trig")) { cmd_capture_trig(argc, argv); return; }
    if (argc < 4) {
        printf("usage: capture <mask> <n> <rate_hz>\n");
        printf("       capture trig <ch> [threshold] [rate_hz] [pre%%] [timeout_s]\n");
        return;
    }
    uint32_t mask_u, n_u, rate;
    if (!parse_u32_in(argv[1], 1u, 0xFFu, &mask_u)) {
        bad_arg("mask", argv[1], "a channel bitmask, e.g. 0x20 for ch5"); return;
    }
    if (!parse_u32(argv[2], &n_u)) { bad_arg("sample count", argv[2], "a whole number"); return; }
    if (!parse_u32_in(argv[3], 1u, 0xFFFFFFFFu, &rate)) {
        bad_arg("rate", argv[3], "a sample rate in Hz"); return;
    }
    uint   mask = (uint)mask_u;
    size_t n    = (size_t)n_u;

    if ((mask & ~ADC_VALID_MASK) != 0) {
        printf("ERR: mask 0x%02x includes a non-analog channel.\n", mask);
        printf("     valid mask is 0x%02x (ch 0,1,2,5,7).\n", ADC_VALID_MASK);
        printf("     ch3=GPIO43 RPI5_SHUTDOWN and ch4=GPIO44 Threshold_PWM are\n");
        printf("     digital outputs on this board -- sampling them is a bug.\n");
        return;
    }

    size_t got = adc_capture(mask, n, rate);
    if (!got) { printf("ERR: capture failed\n"); return; }

    // CSV, machine-readable for tools/scope.py. Header first so the host can
    // de-interleave without being told the mask separately.
    printf("# capture mask=0x%02x n=%u rate=%lu overran=%d\n",
           adc_capture_mask(), (unsigned)got, (unsigned long)adc_capture_rate(),
           adc_capture_overran());
    printf("# columns:");
    for (uint c = 0; c < 8; c++) if (adc_capture_mask() & (1u << c)) printf(" ch%u", c);
    printf("\n");

    const uint16_t *b = adc_capture_buffer();
    uint nch = 0;
    for (uint c = 0; c < 8; c++) if (adc_capture_mask() & (1u << c)) nch++;

    // A triggered capture wrapped, so the data does not start at index 0. The
    // plain path leaves start = 0, which makes this the same walk for both.
    const size_t start = adc_capture_start();
    for (size_t i = 0; i < got; i += nch) {
        for (uint k = 0; k < nch; k++)
            printf(k ? ",%u" : "%u", b[(start + i + k) % ADC_CAPTURE_MAX_SAMPLES]);
        printf("\n");
        // The CDC pipe is slower than we can print. Service the FSM, the supply
        // monitor and an armed watchdog while it drains (2026-10-05 audit: this
        // used to be tight_loop_contents(), which services nothing).
        if ((i % 256) == 0) pitrac_service();
    }
    printf("# end\n");

    if (adc_capture_overran())
        printf("WARN: FIFO overran -- round-robin phase lost, samples are mislabeled.\n");
}

// Pins that the CLI is allowed to drive. PIN_PULSE_LIMIT_DIS is deliberately
// absent: it defeats the strobe hardware watchdog and there must be no path to
// it until Phase 6d. PIN_LATCH_CONTROL is absent too -- go through the FSM so
// the USB-power guard cannot be bypassed.
static bool gpio_writable(uint p) {
    switch (p) {
        case PIN_LED_RED: case PIN_LED_YELLOW:
        case PIN_PWR_BTN_LED: case PIN_READY_LED:
        case PIN_CAM_TRIGGER: case PIN_USB_ENABLE:
        case PIN_HPF_TOGGLE: case PIN_SYSTEM_READY:
        case PIN_IRQ_OUT: case PIN_RPI5_SHUTDOWN:
            return true;
        default:
            return false;
    }
}

static void cmd_gpio(int argc, char **argv) {
    if (argc < 2) { printf("usage: gpio <n> [0|1]\n"); return; }
    uint32_t p_u;
    if (!parse_u32_in(argv[1], 0u, 47u, &p_u)) { bad_arg("pin", argv[1], "0-47"); return; }
    uint p = (uint)p_u;

    if (argc == 2) { printf("gpio%u = %d\n", p, gpio_get(p)); return; }

    if (!gpio_writable(p)) {
        printf("ERR: gpio%u is not CLI-writable.\n", p);
        if (p == PIN_PULSE_LIMIT_DIS)
            printf("     GPIO27 defeats the strobe hardware watchdog. Test only,\n"
                   "     and not before Phase 6d. There is no CLI path by design.\n");
        if (p == PIN_LATCH_CONTROL)
            printf("     GPIO15 is the +5V latch. Use 'on'/'off' so the USB-power\n"
                   "     guard is applied.\n");
        return;
    }
    // Exactly 0 or 1. Anything else used to count as 1 -- `gpio 2 o` drove it high.
    if (strcmp(argv[2], "0") && strcmp(argv[2], "1")) { bad_arg("level", argv[2], "0 or 1"); return; }
    gpio_put(p, argv[2][0] == '1');
    printf("gpio%u <- %d\n", p, gpio_get_out_level(p));
}

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Command handlers.
//
// These were one 1064-line if/else chain inside dispatch() until 2026-08-28.
// Splitting them is not cosmetic: phases 5, 6 and 7 add roughly ten more
// commands, and the chain was already the largest function in the firmware by
// a factor of four. A table also makes `help` checkable against the real
// command set, which is what let `cal`, `scan` and `level` go undocumented.
// ---------------------------------------------------------------------------

// Defined after k_cmds, which it walks.
static void help_check_undocumented(void);

// The curated text is worth keeping by hand -- its value is the grouping, the
// warnings and the units, none of which a generated list gives you. What a
// generated list DOES give you is completeness, so do both: print the curated
// text, then name anything in the command table it forgot to mention.
//
// This exists because `cal`, `cal model`, `scan carrier` and `level` -- four of
// the most important commands in Phase 3 -- were absent from `help` for months.
// Nothing failed; they were simply undiscoverable except by reading the bench
// docs. A hand-maintained list of a growing command set will always drift.
static void cmd_help(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("%s", k_help);
    help_check_undocumented();
}

static void cmd_id_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;
 cmd_id(); 
}

static void cmd_stat_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;
 cmd_stat(); 
}

static void cmd_pins_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;
 cmd_pins(); 
}

static void cmd_capture_cmd(int argc, char **argv) {
 cmd_capture(argc, argv); 
}

static void cmd_gpio_cmd(int argc, char **argv) {
 cmd_gpio(argc, argv); 
}

static void cmd_adc(int argc, char **argv) {

        if (argc < 2) { printf("usage: adc <ch> [n]\n"); return; }
        uint32_t ch_u, n_u = 64u;
        if (!parse_u32(argv[1], &ch_u)) { bad_arg("channel", argv[1], "0, 1, 2, 5 or 7"); return; }
        if (argc > 2 && !parse_u32_in(argv[2], 1u, 4096u, &n_u)) {
            bad_arg("sample count", argv[2], "1-4096"); return;
        }
        uint ch = (uint)ch_u;
        uint n  = (uint)n_u;
        // The ch > 7 test is not redundant. Shifting a uint32_t by >= 32 is
        // undefined behaviour, and on ARM the shift count is taken mod 32 -- so
        // `adc 32` would evaluate (ADC_VALID_MASK >> 0) & 1, see bit 0 set, and
        // sail through to adc_read_avg() with a nonexistent channel.
        if (ch > 7 || !((ADC_VALID_MASK >> ch) & 1u)) {
            printf("ERR: ch%u is not an analog input on this board (valid: 0,1,2,5,7)\n", ch);
            return;
        }
        uint16_t code = adc_read_avg(ch, n);
        printf("ch%u = %u  (%.4f V at pin, n=%u)\n", ch, code,
               (double)adc_code_to_volts(code), n);
    
}

static void cmd_adc5v(int argc, char **argv) {
    (void)argc;
    (void)argv;

        float v = adc_read_5vin_volts();
        printf("+5V_IN = %.3f V  (latch guard %.2f V -> %s)\n",
               (double)v, (double)V5_MIN_FOR_LATCH,
               v < V5_MIN_FOR_LATCH ? "USB-only, latch INHIBITED"
                                    : "external supply, latch permitted");
        printf("         measured refs: USB-only 4.85 V, bench/Meanwell 5.20 V\n");
    
}

static void cmd_adc5vcal(int argc, char **argv) {

        if (argc < 2) { printf("usage: adc5vcal <volts measured with a DMM at +5V_IN>\n"); return; }
        float meas;
        if (!parse_float(argv[1], &meas)) { bad_arg("voltage", argv[1], "volts, e.g. 5.203"); return; }
        float raw  = adc_read_volts(ADC_CH_5VIN, 256) * V5IN_DIVIDER;
        if (raw > 0.1f && meas > 0.1f) {
            adc_set_5vin_scale(meas / raw);
            printf("scale <- %.4f  (raw %.3f V, measured %.3f V)\n",
                   (double)adc_get_5vin_scale(), (double)raw, (double)meas);
            printf("NOTE: RAM only until 'cfg save', which persists it with the rest of\n"
                   "      the calibration (restored at every boot).\n");
        } else printf("ERR: implausible values\n");
    
}

static void cmd_adcmode(int argc, char **argv) {

        if (argc < 2) { printf("usage: adcmode off|idle|armed|burst\n"); return; }
        if      (!strcmp(argv[1], "off"))   adc_engine_set_mode(ADC_MODE_OFF);
        else if (!strcmp(argv[1], "idle"))  adc_engine_set_mode(ADC_MODE_IDLE);
        else if (!strcmp(argv[1], "armed")) adc_engine_set_mode(ADC_MODE_ARMED);
        else if (!strcmp(argv[1], "burst")) adc_engine_set_mode(ADC_MODE_BURST);
        else { printf("ERR: unknown mode\n"); return; }
        printf("adcmode = %d\n", (int)adc_engine_mode());
    
}

static void cmd_on(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (!power_request_on()) {
        printf("REFUSED: 'on' needs STANDBY with no pending stop (state %s).\n"
               "  No power-on request was queued.\n",
               power_state_name(power_fsm_state()));
        return;
    }
    printf("requested on\n");
}

static void cmd_off(int argc, char **argv) {
    (void)argc;
    (void)argv;
 power_request_shutdown();  printf("requested shutdown\n"); 
}

static void cmd_forceoff(int argc, char **argv) {
    (void)argc;
    (void)argv;
 power_request_force_off(); printf("FORCE OFF\n"); 
}

static void cmd_pisim(int argc, char **argv) {
    (void)argc;
    (void)argv;

        printf("PI_3V3_SENSE(24) = %d   <- feed 3.3V into the top of R45 to assert\n",
               gpio_get(PIN_PI_3V3_SENSE));
        printf("RPI5_ON(0)       = %d   <- jumper J8.15 to J8.1 (+3V3) to assert\n",
               gpio_get(PIN_RPI5_ON));
        printf("RPI5_SHUTDOWN(43)= %d   -> scope J8.37 (%s)\n",
               gpio_get_out_level(PIN_RPI5_SHUTDOWN),
               PI_SHUTDOWN_ACTIVE_LOW ? "ACTIVE LOW" : "ACTIVE HIGH");
        printf("pi_is_down       = %d\n", power_pi_is_down());
    
}

static void cmd_panel(int argc, char **argv) {

        if (argc < 2) {
            printf("usage: panel pwr <0-100|auto> | panel rdy <0-100|auto>  (rdy: on/off)\n"
                   "       panel pattern <off|powering|booting|running|shutdown|fault|auto>\n"
                   "       panel test | panel demo\n");
            return;
        }

        if (!power_rails_ready())
            printf("NOTE: +5V rail is open (state %s) -- the panel LEDs are fed from the\n"
                   "      SWITCHED rail and will stay dark. Use 'on' first.\n",
                   power_state_name(power_fsm_state()));

        // --- ramp, for a current / thermal check ---------------------------
        if (!strcmp(argv[1], "test")) {
            printf("ramping both panel LEDs 0->100->0 ... (RDY is on/off: it lights\n"
                   "at >= 50 %%, since it gave up PWM for the strobe gate DAC -- A7)\n");
            for (int p = 0; p <= 100; p += 5) { panel_override(p, p); pitrac_yield_ms(80); }
            for (int p = 100; p >= 0; p -= 5) { panel_override(p, p); pitrac_yield_ms(80); }
            panel_override(-1, -1);
            printf("done -- both returned to automatic\n");
            return;
        }

        // --- play every pattern in turn -------------------------------------
        // Most of these states are unreachable on the bench without a simulated
        // Pi, so this is the only way to eyeball them before Phase 1b.
        if (!strcmp(argv[1], "demo")) {
            printf("playing all ring patterns, ~4 s each. Ctrl-C does nothing here;\n"
                   "it runs to completion (~24 s) then returns to automatic.\n");
            for (int p = 0; p < PANEL_PAT__COUNT; p++) {
                printf("  %-9s %s\n", panel_pattern_name(p),
                       p == PANEL_PAT_OFF      ? "(dark -- what STANDBY looks like)"      :
                       p == PANEL_PAT_POWERING ? "(fast breath -- rails coming up)"       :
                       p == PANEL_PAT_BOOTING  ? "(slow breath -- waiting on the Pi)"     :
                       p == PANEL_PAT_RUNNING  ? "(solid -- ready)"                       :
                       p == PANEL_PAT_SHUTDOWN ? "(fast blink -- teardown)"               :
                                                 "(double-blink -- fault)");
                panel_force_pattern(p);
                for (int i = 0; i < 400; i++) { pitrac_yield_ms(10); }
            }
            panel_force_pattern(-1);
            printf("done -- ring returned to automatic (now: %s)\n",
                   panel_pattern_name(panel_pattern_for_state()));
            return;
        }

        // --- force one pattern ----------------------------------------------
        if (!strcmp(argv[1], "pattern")) {
            if (argc < 3) {
                printf("usage: panel pattern <off|powering|booting|running|shutdown|fault|auto>\n");
                printf("current: forced=%s  fsm-would-be=%s\n",
                       panel_forced_pattern() < 0 ? "auto"
                           : panel_pattern_name(panel_forced_pattern()),
                       panel_pattern_name(panel_pattern_for_state()));
                return;
            }
            if (!strcmp(argv[2], "auto")) {
                panel_force_pattern(-1);
                printf("ring <- auto (now: %s)\n",
                       panel_pattern_name(panel_pattern_for_state()));
                return;
            }
            int p = panel_pattern_from_name(argv[2]);
            if (p < 0) { printf("ERR: unknown pattern '%s'\n", argv[2]); return; }
            panel_force_pattern(p);
            printf("ring <- %s (forced; 'panel pattern auto' to release)\n",
                   panel_pattern_name(p));
            return;
        }

        // --- fixed brightness on one LED -------------------------------------
        if (strcmp(argv[1], "pwr") && strcmp(argv[1], "rdy")) {
            printf("ERR: '%s' is not a panel subcommand -- nothing was changed.\n"
                   "usage: panel pwr <0-100|auto> | panel rdy <0-100|auto>\n"
                   "       panel pattern <p|auto> | panel test | panel demo\n", argv[1]);
            return;
        }
        if (argc < 3) { printf("usage: panel %s <0-100|auto>\n", argv[1]); return; }
        bool is_pwr = !strcmp(argv[1], "pwr");
        int  pct    = -1;
        if (strcmp(argv[2], "auto")) {
            uint32_t v;
            if (!parse_u32_in(argv[2], 0u, 100u, &v)) { bad_arg("brightness", argv[2], "0-100 or auto"); return; }
            pct = (int)v;
        }
        panel_override(is_pwr ? pct : -2, is_pwr ? -2 : pct);
        printf("%s <- %s\n", is_pwr ? "PWR_LED(J7.2)" : "RDY_LED(J7.6)",
               pct < 0 ? "auto" : argv[2]);
    
}

static void cmd_threshold(int argc, char **argv) {

        // Reject anything that is not a known subcommand INSTEAD of falling
        // through to the status print. `threshold 5` used to print the current
        // level and return, which reads exactly like it worked -- on 2026-08-25
        // an operator set the threshold twice that way, got the same status line
        // back both times, and only found out three commands later that the
        // level had never moved. A command that silently ignores its argument is
        // worse than one that errors.
        if (argc >= 2 && strcmp(argv[1], "duty") && strcmp(argv[1], "volts")
                      && strcmp(argv[1], "sweep") && strcmp(argv[1], "vref")) {
            printf("ERR: '%s' is not a threshold subcommand -- NOTHING WAS SET.\n"
                   "usage: threshold                    (status only)\n"
                   "       threshold duty <percent>\n"
                   "       threshold volts <v>\n"
                   "       threshold sweep [lo%%] [hi%%] [steps]\n"
                   "       threshold vref <volts>       (RAM only)\n",
                   argv[1]);
            return;
        }
        if (argc >= 3 && !strcmp(argv[1], "duty")) {
            float pct;
            if (!parse_float_in(argv[2], 0.0f, 100.0f, &pct)) { bad_arg("duty", argv[2], "0-100 %"); return; }
            detect_threshold_set_duty(pct / 100.0f);
        } else if (argc >= 3 && !strcmp(argv[1], "volts")) {
            float v;
            if (!parse_float_in(argv[2], 0.0f, detect_threshold_vref(), &v)) {
                bad_arg("threshold", argv[2], "0 V to the DAC vref"); return;
            }
            detect_threshold_set_volts(v);
        } else if (argc >= 3 && !strcmp(argv[1], "vref")) {
            // The DAC's effective reference, from the two-point sweep in
            // BENCH_P3_DETECT 3.5: solve ADC5 = vref*duty + Vos across a low and
            // a high flip. Board 2 measured 3.268 V against the compiled 3.300,
            // i.e. every printed threshold voltage reads ~1 % high.
            //
            // RAM ONLY, deliberately. It is worth ~1 % on a threshold that gets
            // tuned empirically in 3.7 anyway, which does not justify spending
            // another field of a fixed-size config record -- see cal_duty in
            // config_store.h for why that record cannot simply grow. If absolute
            // threshold accuracy ever matters, this is the number to persist,
            // alongside adc5v_scale.
            float vref;
            // detect_threshold_set_vref() silently ignores anything outside
            // (0.5, 6.0) -- endpoints excluded -- so refuse exactly that set here.
            if (!parse_float(argv[2], &vref) || !(vref > 0.5f && vref < 6.0f)) {
                bad_arg("vref", argv[2], "volts strictly between 0.5 and 6.0"); return;
            }
            detect_threshold_set_vref(vref);
            printf("threshold vref <- %.4f V (RAM only, lost on reset)\n",
                   (double)detect_threshold_vref());
        } else if (argc >= 2 && !strcmp(argv[1], "sweep")) {
            float lo_pct = 0.0f, hi_pct = 100.0f;
            uint32_t n_u = 64u;
            if (argc > 2 && !parse_float_in(argv[2], 0.0f, 100.0f, &lo_pct)) { bad_arg("lo", argv[2], "0-100 %"); return; }
            if (argc > 3 && !parse_float_in(argv[3], 0.0f, 100.0f, &hi_pct)) { bad_arg("hi", argv[3], "0-100 %"); return; }
            // The sweep counts with a uint16_t `i <= steps`, so 65535 never ends,
            // and 0 returns before filling the result. 1000 steps is ~20 s.
            if (argc > 4 && !parse_u32_in(argv[4], 1u, 1000u, &n_u)) { bad_arg("steps", argv[4], "1-1000"); return; }
            float lo   = lo_pct / 100.0f;
            float hi   = hi_pct / 100.0f;
            uint16_t n = (uint16_t)n_u;
            printf("sweeping threshold %.1f%% -> %.1f%% in %u steps "
                   "(%u ms settle each, ~%lu ms total) ...\n",
                   (double)(lo * 100.0f), (double)(hi * 100.0f), n,
                   (unsigned)DAC_SETTLE_MS, (unsigned long)((n + 1) * DAC_SETTLE_MS));
            threshold_sweep_t r;
            detect_threshold_sweep(&r, lo, hi, n);
            if (r.found)
                printf("FLIP at duty %.2f%%  = %.4f V nominal at TP8\n"
                       "  ADC5 there: code %u = %.4f V\n",
                       (double)(r.flip_duty * 100.0f), (double)r.flip_volts,
                       r.adc5_at_flip, (double)r.adc5_at_flip_v);
            else
                printf("NO FLIP across the sweep. Either the signal never crosses this\n"
                       "  range, or D_Comparator is stuck. Check 'threshold' and GPIO46.\n");
            printf("ADC5 movement across the sweep: %.4f V\n", (double)r.adc5_span_v);
            printf("  That is GPIO44 crosstalk into the detect node (146.5 kHz DAC vs\n"
                   "  104.17 kHz optical carrier, only 42 kHz apart). Expect ~0 -- two RC\n"
                   "  poles give ~120 dB. If it moves, change DAC_TOP: 2047 -> 73 kHz,\n"
                   "  or 511 -> 293 kHz.\n");
            return;
        }
        printf("threshold: level %u of %lu  duty %.2f %%  -> TP8 %.4f V nominal\n",
               detect_threshold_level(), (unsigned long)(DAC_TOP + 1u),
               (double)(detect_threshold_duty() * 100.0f),
               (double)detect_threshold_volts());
        printf("           vref %.3f V (RAM only; TP8 is the measured threshold)\n",
               (double)detect_threshold_vref());
        printf("D_Comparator(46) = %d   (%s -- active HIGH, R103 10K pull-up)\n",
               gpio_get(PIN_D_COMPARATOR),
               detect_comparator() ? "ABOVE threshold" : "below threshold");
        printf("settle %u ms per change (dominant pole 2.62 ms)\n",
               (unsigned)DAC_SETTLE_MS);
    
}

static void cmd_cfg(int argc, char **argv) {

        if (argc >= 2 && !strcmp(argv[1], "save")) {
            // Snapshot the live values that other modules own.
            pitrac_cfg_t *m = cfg_mut();
            m->demod_phase_ticks  = beam_phase_ticks();
            m->carrier_hz         = beam_actual_freq_hz();
            m->threshold_level    = detect_threshold_level();
            m->detect_coalesce_us = (uint16_t)detect_coalesce_us();
            m->adc5v_scale        = adc_get_5vin_scale();
            m->path_mm            = detect_path_mm();
            if (s_phase_model.valid) {
                m->phase_a0 = s_phase_model.a0;
                m->phase_a1 = s_phase_model.a1;
                m->phase_a2 = s_phase_model.a2;
                m->phase_f_lo = s_phase_model.f_lo;
                m->phase_f_hi = s_phase_model.f_hi;
                m->phase_pure_delay = s_phase_model.pure_delay ? 1 : 0;
            }
            const char *why = cfg_save_blocked_reason();
            if (why) {
                printf("REFUSED: %s.\n", why);
                printf("  A 4 KB erase holds interrupts off for tens of ms, and during that\n"
                       "  window the power FSM does not run -- so the V5_MIN_SUSTAINED monitor\n"
                       "  is blind. That is the monitor that stops a Pi being fed through a\n"
                       "  1 A diode. Quiesce first.\n");
                return;
            }
            printf("%s\n", cfg_save() ? "saved" : "SAVE FAILED (readback did not verify)");
            return;
        }
        if (argc >= 2 && !strcmp(argv[1], "default")) {
            cfg_defaults(cfg_mut()); printf("in-RAM config reset to defaults ('cfg save' to commit)\n"); return;
        }
        {
            const pitrac_cfg_t *k = cfg();
            printf("source   : %s   seq %lu   v%u\n", cfg_source(),
                   (unsigned long)k->seq, k->version);
            printf("carrier  : %lu Hz   phase %ld ticks\n",
                   (unsigned long)k->carrier_hz, (long)k->demod_phase_ticks);
            printf("threshold: level %u    coalesce %u us\n",
                   k->threshold_level, k->detect_coalesce_us);
            printf("adc5v    : scale %.4f\n", (double)k->adc5v_scale);
            printf("u12b gain: %.1f    path %.2f mm%s\n", (double)k->u12b_gain,
                   (double)k->path_mm, k->path_mm > 0.0f ? "" : "  (unset: no velocity)");
            printf("hpf sel  : %s\n", k->hpf_sel_track == 0xff
                   ? "NEVER MEASURED -- run 'hpf test'"
                   : (k->hpf_sel_track ? "TRACK = 1" : "TRACK = 0"));
            printf("phase mdl: %s", k->phase_f_hi
                   ? (k->phase_pure_delay ? "fitted, pure delay" : "fitted, dispersive")
                   : "not fitted");
            if (k->phase_f_hi)
                printf("  (%lu..%lu Hz)", (unsigned long)k->phase_f_lo,
                       (unsigned long)k->phase_f_hi);
            printf("%s\n", s_phase_model.valid ? "" : "   [NOT IN RAM]");

            // The phase is only meaningful at the duty it was measured at. Say so
            // here rather than letting someone read a restored 1348 and run at the
            // 2 % power-on default, 166 ticks away from the peak.
            if (k->cal_duty > 0.0f) {
                printf("cal duty : %.2f %%", (double)(k->cal_duty * 100.0f));
                float dd = beam_duty() - k->cal_duty;
                if (dd < 0.0f) dd = -dd;
                if (dd > 0.005f) {
                    int32_t err = (int32_t)((float)(beam_top() + 1u) * dd * 0.5f + 0.5f);
                    printf("   *** BEAM IS AT %.2f %% -- the stored phase is %ld ticks\n"
                           "           off at this duty (geometric term, not drift). Set the\n"
                           "           duty back or re-run cal demod. ***",
                           (double)(beam_duty() * 100.0f), (long)err);
                }
                printf("\n");
            } else if (k->phase_f_hi || k->demod_phase_ticks) {
                printf("cal duty : NOT RECORDED -- saved before 2026-08-25. The stored\n"
                       "           phase cannot be checked against the current duty;\n"
                       "           re-run cal demod to fix.\n");
            }
            const char *why = cfg_save_blocked_reason();
            printf("save     : %s\n", why ? why : "permitted now");
        }
    
}

static void cmd_level(int argc, char **argv) {
    (void)argc;
    (void)argv;

        if (!beam_enabled())      { printf("REFUSED: beam is off.\n"); return; }
        if (!power_rails_ready()) { printf("REFUSED: +5V rail is open.\n"); return; }
        if (detect_hpf_mode() != HPF_TRACK)
            printf("WARNING: HPF is in HOLD. A chopped reading needs TRACK -- 'hpf track'.\n");

        printf("live level: chopping at %u Hz, reading the peak at the CURRENT phase\n"
               "  (%ld ticks, duty %.2f %%). Only the TRUE peak if the phase is already\n"
               "  near it -- run 'cal demod' once first.\n",
               (unsigned)CAL_CHOP_HZ_DEFAULT, (long)beam_phase_ticks(),
               (double)(beam_duty() * 100.0f));
        printf("TARGET 50-70 %% of full scale. Position the target, then CLAMP IT --\n"
               "  a hand cannot hold still for the 128 s 'cal model' takes.\n"
               "Any key stops; gives up after %u s.\n\n", (unsigned)(LEVEL_MAX_MS / 1000u));

        adc_engine_set_mode(ADC_MODE_ARMED);
        absolute_time_t t0 = get_absolute_time();
        while (absolute_time_diff_us(t0, get_absolute_time()) < (int64_t)LEVEL_MAX_MS * 1000) {
            float pk  = cal_level_peak(CAL_LEVEL_CYCLES, CAL_CHOP_HZ_DEFAULT);
            float pct = pk / ADC_FULL_SCALE * 100.0f;

            char bar[21];
            int fill = (int)(pct / 5.0f + 0.5f);
            if (fill < 0)  fill = 0;
            if (fill > 20) fill = 20;
            for (int i = 0; i < 20; i++) bar[i] = (i < fill) ? '#' : ' ';
            bar[20] = 0;

            // Anything inside the first 5 tau of the HPF is the filter settling,
            // not the target. Say so instead of reporting it as a level.
            bool settling = absolute_time_diff_us(t0, get_absolute_time())
                            < (int64_t)LEVEL_SETTLE_MS * 1000;
            const char *verdict = settling      ? "settling -- HPF tau 0.66 s, ignore"
                                : (pct > 90.0f) ? "*** TOO HIGH, will saturate ***"
                                : (pct > 70.0f) ? "high -- less light"
                                : (pct < 20.0f) ? "*** TOO LOW to fit ***"
                                : (pct < 50.0f) ? "low  -- more light"
                                                : "GOOD";
            printf("  peak %5.0f = %3.0f %%  |%s|  %s\n",
                   (double)pk, (double)pct, bar, verdict);

            // pitrac_service() consumes the keystroke inside the yield, so a
            // local getchar() here would almost never see one. Ask the latch.
            if (pitrac_abort_pending()) break;
        }
        adc_engine_set_mode(ADC_MODE_IDLE);
        printf("\nstopped. Beam left ON at %.2f %% duty.\n",
               (double)(beam_duty() * 100.0f));
        return;
    
}

static void cmd_cal(int argc, char **argv) {

        if (argc >= 2 && !strcmp(argv[1], "demod")) {
            if (!beam_enabled()) { printf("REFUSED: beam is off.\n"); return; }
            if (detect_hpf_mode() != HPF_TRACK)
                printf("WARNING: HPF is in HOLD. Method A needs TRACK -- 'hpf track'.\n");
            if (beam_duty_stable_ms() < BEAM_WARMUP_MS)
                printf("WARNING: beam warm for only %lu s of %lu. Optical output is still\n"
                       "  falling and nothing electrical shows it. Results will be cold.\n",
                       (unsigned long)(beam_duty_stable_ms() / 1000u),
                       (unsigned long)(BEAM_WARMUP_MS / 1000u));
            adc_engine_set_mode(ADC_MODE_ARMED);
            cal_demod_t r;
            // Print the frequency. `cal model` used to leave the beam at
            // 200 kHz, and without this line nothing on screen said which
            // carrier was actually being calibrated.
            printf("sweeping demod phase at %lu Hz (TOP+1 = %lu), ~26 s ...\n",
                   (unsigned long)beam_actual_freq_hz(),
                   (unsigned long)(beam_top() + 1u));
            bool ok = cal_demod_phase(beam_actual_freq_hz(), CAL_PHASE_POINTS,
                                      CAL_CHOP_CYCLES, CAL_CHOP_HZ_DEFAULT, &r);
            adc_engine_set_mode(ADC_MODE_IDLE);
            printf("\nfit      : %ld ticks   (grid argmax %ld)\n",
                   (long)r.best_ticks, (long)r.argmax_ticks);
            // The tick count is an encoding; the delay is the measurement. It is
            // what compares across frequencies, against the netlist, and between
            // boards -- best_ticks does none of those things.
            printf("delay    : %.0f ns from the PWM edge to the demod input\n"
                   "           (best_ticks + duty/2 x (TOP+1), unwrapped -- the\n"
                   "            duty term is geometry, not chain)\n",
                   (double)r.chain_delay_ns);
            printf("amplitude: %.0f codes fitted fundamental\n", (double)r.amplitude);
            printf("peak     : %.0f codes = %.0f %% of full scale%s\n"
                   "           (fundamental / %.3f, the 4*sinc(D)/pi trapezoid form\n"
                   "            factor at %.2f %% duty -- the RAIL is hit by the peak)\n",
                   (double)r.peak, (double)(r.peak / ADC_FULL_SCALE * 100.0f),
                   (r.peak > ADC_FULL_SCALE)
                       ? "  *** ABOVE FULL SCALE -- impossible for a real signal ***" : "",
                   (double)r.form_factor, (double)(r.duty * 100.0f));
            printf("saturated: %.0f %% of sweep points against the rail  (limit %.0f %%)%s\n",
                   (double)(r.sat_frac * 100.0f), (double)(CAL_SAT_MAX * 100.0f),
                   (r.sat_frac >= CAL_SAT_MAX) ? "  *** FAIL ***" : "");
            printf("quad null: %.1f codes  (want |null| < %.0f %% of amplitude = %.1f)%s\n",
                   (double)r.quad_null, (double)(CAL_QUAD_NULL_MAX * 100.0f),
                   (double)(CAL_QUAD_NULL_MAX * r.amplitude),
                   (fabsf(r.quad_null) >= CAL_QUAD_NULL_MAX * r.amplitude) ? "  *** FAIL ***" : "");
            printf("h2/h1    : %.3f  (DIAGNOSTIC ONLY, does not gate -- a 50 %% square\n"
                   "           demod emits only ODD harmonics, so this cannot see the\n"
                   "           optical path. High means the sweep outran the HPF.)\n",
                   (double)r.h2_ratio);
            printf("h3/h1    : %.3f  (SYMMETRIC CLIPPING. A CLEAN response at %.2f %%\n"
                   "           duty gives %.3f intrinsically, so the bar is %.3f;\n"
                   "           square wave = 0.333)%s\n",
                   (double)r.h3_ratio, (double)(r.duty * 100.0f),
                   (double)r.h3_expected, (double)(r.h3_expected + CAL_H3_TOL),
                   (r.h3_ratio >= r.h3_expected + CAL_H3_TOL) ? "  *** FAIL ***" : "");
            if (r.duty < 0.15f)
                printf("           !! below ~15 %% duty h3 cannot separate a clean\n"
                       "              response from a clipped one -- both approach\n"
                       "              0.333. Trust sat_frac here, not h3.\n");
            printf("warm     : %s\n", r.warm ? "yes" : "NO -- see above");
            cfg_mut()->cal_warm = r.warm ? 1u : 0u;
            // The duty is part of the answer, not context: phase_ticks carries a
            // -(duty/2)*(TOP+1) geometric term, so a phase without the duty it was
            // measured at cannot be used at any other duty.
            cfg_mut()->cal_duty = r.duty;
            if (ok) {
                beam_set_phase(r.best_ticks);
                printf("phase <- %ld ticks. Record it in PROGRESS.md section 6.\n",
                       (long)r.best_ticks);
            } else {
                printf("NOT COMMITTED -- see the *** FAIL *** lines above.\n");
                if (r.sat_frac >= CAL_SAT_MAX ||
                    r.h3_ratio >= r.h3_expected + CAL_H3_TOL ||
                    r.peak > ADC_FULL_SCALE) {
                    printf("  THE SIGNAL IS TOO BIG. The response is clipping into a square\n"
                           "  wave, so the peak is flat and its location is not measurable.\n"
                           "  Reduce the LIGHT, not the gain -- U12B is already at its minimum\n"
                           "  14.5 (R98 is DNP, and fitting it only raises gain). In order of\n"
                           "  preference: move the reflector further away, use a darker one\n"
                           "  (grey card, not white), or add an ND filter over D12.\n");
                } else {
                    printf("  Check the reflector, the HPF mode and that the beam is on.\n");
                }
            }
            return;
        }
        if (argc >= 2 && !strcmp(argv[1], "model")) {
            uint32_t f0 = 80000u, f1 = 200000u, n = 5u;
            if ((argc > 2 && !parse_u32_in(argv[2], 1u, SYSCLK_HZ / 2u, &f0)) ||
                (argc > 3 && !parse_u32_in(argv[3], 1u, SYSCLK_HZ / 2u, &f1))) {
                printf("ERR: frequencies must be whole Hz, 1-%lu. Nothing was run.\n",
                       (unsigned long)(SYSCLK_HZ / 2u));
                return;
            }
            if (argc > 4 && !parse_u32(argv[4], &n)) { bad_arg("points", argv[4], "a whole number"); return; }
            printf("fitting the phase model over %lu..%lu Hz at %lu points (~%lu s)\n",
                   (unsigned long)f0, (unsigned long)f1, (unsigned long)n,
                   (unsigned long)(n * 20u));
            adc_engine_set_mode(ADC_MODE_ARMED);
            bool ok = cal_demod_model(f0, f1, n, &s_phase_model);
            adc_engine_set_mode(ADC_MODE_IDLE);
            if (!ok) { printf("model fit FAILED\n"); return; }
            printf("\ntheta(f) = %.4f + %.4g*f + %.4g*f^2   rad, f in Hz\n",
                   (double)s_phase_model.a0, (double)s_phase_model.a1,
                   (double)s_phase_model.a2);
            printf("range %lu..%lu Hz, %u points, residual %.4f rad (%.2f deg)\n",
                   (unsigned long)s_phase_model.f_lo, (unsigned long)s_phase_model.f_hi,
                   s_phase_model.n_points, (double)s_phase_model.resid_rms_rad,
                   (double)(s_phase_model.resid_rms_rad * 57.29578f));
            // Optical stability. Frequency changes the phase, not how much light
            // comes back, so a large spread means the SCENE moved during the run
            // and the points describe different systems.
            printf("amp spread: %.0f %% across the accepted points  (limit %.0f %%)%s\n",
                   (double)(s_phase_model.amp_spread * 100.0f),
                   (double)(CAL_MODEL_AMP_SPREAD_MAX * 100.0f),
                   (s_phase_model.amp_spread >= CAL_MODEL_AMP_SPREAD_MAX)
                       ? "   *** THE SCENE MOVED ***" : "");
            if (s_phase_model.amp_spread >= CAL_MODEL_AMP_SPREAD_MAX)
                printf("  Every point sees the same optics, so the amplitude should barely\n"
                       "  move between them. This much spread means the target or the\n"
                       "  operator moved during the run. USE A STATIC TARGET -- a card on a\n"
                       "  stand, not a hand. The per-point checks cannot catch this: each\n"
                       "  point is individually valid, they just describe different scenes.\n"
                       "  Treat the fit as unusable and re-run.\n");
            printf("  !! the coefficients are a LOCAL INTERPOLANT. a0 absorbs the 2*pi\n"
                   "     wrap and is meaningless alone -- read the delay below instead.\n");

            // The physical result. Stripping the geometric (duty/2)*(TOP+1) term
            // back off leaves the chain delay, which is what the fit is really
            // measuring and the only part that transfers to another board.
            printf("\nchain delay: %.0f ns at %lu Hz -> %.0f ns at %lu Hz  (%+.1f %%)\n",
                   (double)s_phase_model.delay_lo_ns, (unsigned long)s_phase_model.f_lo,
                   (double)s_phase_model.delay_hi_ns, (unsigned long)s_phase_model.f_hi,
                   (double)((s_phase_model.delay_hi_ns / s_phase_model.delay_lo_ns - 1.0f)
                            * 100.0f));
            if (s_phase_model.pure_delay)
                printf("PURE DELAY to within %.2f deg at %lu Hz. The chain is a fixed\n"
                       "  time delay; one delay number covers the whole band.\n",
                       (double)s_phase_model.span_err_deg,
                       (unsigned long)s_phase_model.f_hi);
            else
                printf("DISPERSIVE: the delay drifts enough to cost %.2f deg at %lu Hz,\n"
                       "  past the %.1f deg bar. The poles are contributing.\n",
                       (double)s_phase_model.span_err_deg,
                       (unsigned long)s_phase_model.f_hi,
                       (double)CAL_PURE_DELAY_MAX_DEG);
            // Either way the MODEL is still required, and for a reason that has
            // nothing to do with dispersion: phase_ticks carries a geometric term
            // that scales with the period. Holding one tick count across the band
            // passes quadrature near mid-scan and INVERTS the correlation.
            printf("  In both cases: never hold one TICK COUNT across a sweep. The\n"
                   "  duty/2 x (TOP+1) term alone swings ~140 ticks over 80-200 kHz\n"
                   "  and the error passes 90 deg mid-band, inverting the response.\n");
            printf("*** DO NOT EXTRAPOLATE outside %lu..%lu Hz. ***\n",
                   (unsigned long)s_phase_model.f_lo, (unsigned long)s_phase_model.f_hi);
            return;
        }
        if (argc >= 3 && !strcmp(argv[1], "gain")) {
            float peak, frac = 0.67f;
            if (!parse_float_in(argv[2], 1.0f, 4095.0f, &peak)) {
                bad_arg("peak", argv[2], "1-4095 ADC codes"); return;
            }
            // A fraction, not a percent: `cal gain 3100 67` would solve for 67x
            // full scale and write that gain into the config.
            if (argc > 3 && !parse_float_in(argv[3], 0.05f, 1.0f, &frac)) {
                bad_arg("fraction", argv[3], "0.05-1.0 of full scale (not %)"); return;
            }
            cal_gain_t g;
            cal_gain_recommend(peak, frac, &g);
            // Record the gain the board will actually be running at once the
            // recommendation is fitted, so ADC5 codes can be converted back to
            // TP9 millivolts later without re-deriving it.
            cfg_mut()->u12b_gain = g.gain_target;
            printf("measured peak %.0f codes at the as-built gain %.1f\n",
                   (double)peak, (double)g.gain_now);
            printf("target %.0f%% of ADC full scale\n", (double)(frac * 100.0f));
            if (g.r98_e24 <= 0.0f) {
                printf("\nRECOMMENDATION: leave R98 UNPOPULATED. The signal already fills\n"
                       "  enough of the range; more gain would only cost headroom.\n");
            } else {
                printf("\nRECOMMENDATION: fit R98 = %.0f ohm (E24 nearest to %.0f)\n",
                       (double)g.r98_e24, (double)g.r98_ideal);
                printf("  gain %.1f -> %.1f\n", (double)g.gain_now, (double)g.gain_target);
                printf("  ADC5 saturates at dTP9 %.0f mV -> %.0f mV\n",
                       (double)g.clip_mv_now, (double)g.clip_mv_new);
            }
            printf("\nSet the gain from the WEAKEST target that must still trigger, and the\n"
                   "ceiling from the strongest. Usable full scale is min(ADC 3.3 V, LM393\n"
                   "common mode ~3.5 V) -- U12B is rail-to-rail on +5VA and can drive 5.2 V\n"
                   "into a comparator that stops being valid at 3.5.\n");
            return;
        }
        printf("usage: cal demod | cal model [f0] [f1] [n] | cal gain <peak> [frac]\n");
        printf("phase model: %s", s_phase_model.valid ? "" : "NOT FITTED\n");
        if (s_phase_model.valid)
            printf("%lu..%lu Hz, residual %.4f rad, %s, chain delay %.0f..%.0f ns\n",
                   (unsigned long)s_phase_model.f_lo, (unsigned long)s_phase_model.f_hi,
                   (double)s_phase_model.resid_rms_rad,
                   s_phase_model.pure_delay ? "pure delay" : "dispersive",
                   (double)s_phase_model.delay_lo_ns, (double)s_phase_model.delay_hi_ns);
    
}

static void cmd_scan(int argc, char **argv) {

        if (argc < 2 || strcmp(argv[1], "carrier")) {
            printf("usage: scan carrier [f0] [f1] [n] [force]\n"); return;
        }
        uint32_t f0 = 80000u, f1 = 200000u, n = 16u;
        if ((argc > 2 && !parse_u32_in(argv[2], 1u, SYSCLK_HZ / 2u, &f0)) ||
            (argc > 3 && !parse_u32_in(argv[3], 1u, SYSCLK_HZ / 2u, &f1))) {
            printf("ERR: frequencies must be whole Hz, 1-%lu. Nothing was run.\n",
                   (unsigned long)(SYSCLK_HZ / 2u));
            return;
        }
        if (argc > 4 && !parse_u32(argv[4], &n)) { bad_arg("points", argv[4], "a whole number"); return; }
        if (argc > 5 && strcmp(argv[5], "force")) { bad_arg("option", argv[5], "'force' or nothing"); return; }
        bool force  = (argc > 5);
        if (!beam_enabled()) { printf("REFUSED: beam is off.\n"); return; }
        if (!s_phase_model.valid)
            printf("WARNING: no phase model. Every candidate will be measured at phase 0,\n"
                   "  i.e. at a DIFFERENT phase error each, and the SNR ranking will be\n"
                   "  meaningless. Run 'cal model' first.\n");
        adc_engine_set_mode(ADC_MODE_ARMED);
        cal_scan_point_t best;
        bool ok = cal_scan_carrier(f0, f1, n, &s_phase_model, force, &best);
        adc_engine_set_mode(ADC_MODE_IDLE);
        // The carrier is fixed for every board (see the FLATNESS verdict cal.c
        // prints): this scan verifies robustness, it does not pick a frequency.
        if (ok) printf("\nRecord the FLATNESS verdict in PROGRESS.md section 6. Do NOT change\n"
                       "the carrier in board.h -- it stays 104166.67 Hz on every board.\n");
    
}

static void cmd_detect(int argc, char **argv) {

        if (argc >= 2 && (!strcmp(argv[1], "arm") || !strcmp(argv[1], "disarm"))) {
            bool on = (argv[1][0] == 'a');
            if (!detect_arm(on)) {
                printf("REFUSED: +5V rail is open (state %s). Use 'on' first.\n",
                       power_state_name(power_fsm_state()));
                return;
            }
            printf("detect %s\n", on ? "ARMED (counters reset)" : "disarmed");
            return;
        }
        if (argc >= 3 && !strcmp(argv[1], "coalesce")) {
            uint32_t us;
            if (!parse_u32(argv[2], &us)) { bad_arg("coalesce", argv[2], "microseconds"); return; }
            detect_set_coalesce_us(us);
        }
        if (argc >= 3 && !strcmp(argv[1], "path")) {
            float mm;
            if (!parse_float_in(argv[2], 0.0f, 1000.0f, &mm)) { bad_arg("path", argv[2], "0-1000 mm"); return; }
            detect_set_path_mm(mm);
            printf("beam path <- %.2f mm\n", (double)detect_path_mm());
            return;
        }
        if (argc >= 3 && !strcmp(argv[1], "cond")) {
            uint32_t c;
            if (!parse_u32_in(argv[2], 0u, 2u, &c)) { bad_arg("condition", argv[2], "0, 1 or 2"); return; }
            detect_set_condition((uint8_t)c);
            printf("condition <- %u  (0 nominal, 1 far, 2 low-reflectance)\n",
                   detect_condition());
            return;
        }
        if (argc >= 2 && !strcmp(argv[1], "clear")) {
            detect_log_clear(); printf("log cleared\n"); return;
        }
        if (argc >= 2 && !strcmp(argv[1], "log")) {
            printf("seq,t_ms,cond,cmp_us,adc_us,peak,baseline,asym_q8,frag,thr,rate_khz,qual\n");
            for (size_t i = 0; i < detect_log_count(); i++) {
                const detect_pass_t *p = detect_log_at(i);
                printf("%lu,%lu,%u,%lu,%lu,%u,%u,%d,%u,%u,%u,0x%02x\n",
                       (unsigned long)p->seq, (unsigned long)p->t_ms, p->condition,
                       (unsigned long)p->transit_us,
                       (unsigned long)(p->adc_transit_ns / 1000u),
                       p->adc_peak, p->adc_baseline, p->adc_asym_q8,
                       p->fragments, p->threshold_level, p->adc_rate_khz, p->quality);
            }
            printf("# qual bits: 01 SAT 02 NOCROSS 04 WINCLIP 08 LAPPED 10 CHATTER 20 NOADC\n");
            return;
        }
        if (argc >= 2 && !strcmp(argv[1], "wave")) {
            uint32_t seq = 0;
            if (argc > 2 && !parse_u32(argv[2], &seq)) { bad_arg("pass", argv[2], "a pass number"); return; }
            const detect_wave_t *w = (argc > 2)
                ? detect_wave_for(seq)
                : detect_wave_recent(0);
            if (!w) { printf("no waveform retained (only the last %u passes)\n",
                             (unsigned)DETECT_WAVE_N); return; }
            printf("# pass %lu  rate %lu Hz  len %u  baseline %u  peak_idx %u\n",
                   (unsigned long)w->pass_seq, (unsigned long)w->rate_hz,
                   w->len, w->baseline, w->peak_idx);
            printf("i,code\n");
            for (uint16_t i = 0; i < w->len; i++) printf("%u,%u\n", i, w->s[i]);
            return;
        }
        if (argc >= 2 && !strcmp(argv[1], "stats")) {
            static const char *names[3] = {"nominal", "far 1.5x", "low-reflect"};
            printf("cond           n   cmp mean   cmp sd   adc mean   adc sd     bias    peak\n");
            printf("---------------------------------------------------------------------------\n");
            float bx[3], by[3]; int nb = 0;
            for (uint8_t ci = 0; ci < 3; ci++) {
                detect_stats_t s;
                if (!detect_stats(ci, &s)) continue;
                printf("%u %-11s %3lu  %8.1f  %7.1f  %9.1f  %7.1f  %6.2f%%  %6.0f\n",
                       ci, names[ci], (unsigned long)s.n,
                       (double)s.cmp_mean_us, (double)s.cmp_sd_us,
                       (double)s.adc_mean_us, (double)s.adc_sd_us,
                       (double)(s.bias * 100.0f), (double)s.peak_mean);
                if (s.n_excluded || s.n_saturated || s.n_chatter)
                    printf("   excluded %lu   saturated %lu   chattered %lu\n",
                           (unsigned long)s.n_excluded, (unsigned long)s.n_saturated,
                           (unsigned long)s.n_chatter);
                if (s.peak_mean > 1.0f) { bx[nb] = 1.0f / s.peak_mean; by[nb] = s.bias; nb++; }
            }
            // The deliverable: bias against 1/amplitude. A fixed threshold
            // crosses a smaller bump later going up and earlier coming down, so
            // if the comparator's error is the amplitude effect and not
            // something else, bias is linear in 1/peak and this slope measures
            // it. A slope indistinguishable from zero at this geometry means
            // the ADC refinement can be dropped and the design simplifies.
            if (nb >= 2) {
                float sx=0, sy=0, sxx=0, sxy=0;
                for (int i=0;i<nb;i++){ sx+=bx[i]; sy+=by[i]; sxx+=bx[i]*bx[i]; sxy+=bx[i]*by[i]; }
                float den = nb*sxx - sx*sx;
                if (den != 0.0f) {
                    float m = (nb*sxy - sx*sy)/den, b = (sy - m*sx)/nb;
                    printf("\nbias vs 1/peak : slope %.4g  intercept %.4g  (n=%d conditions)\n",
                           (double)m, (double)b, nb);
                    printf("  ^ THIS is the amplitude-dependent comparator bias. A slope near\n"
                           "    zero at your geometry means the ADC refinement can be dropped.\n");
                }
            } else {
                printf("\n(need >=2 conditions with data for the bias fit -- use 'detect cond')\n");
            }
            return;
        }
        printf("detect   : %s   PIO block 2 (GPIOBASE %u) SM %u\n",
               detect_armed() ? "ARMED" : "disarmed",
               pio_get_gpio_base(PIO_BLK_HIGH), (unsigned)PIO_SM_DETECT);
        printf("D_Comparator(46) = %d  (%s)\n", gpio_get(PIN_D_COMPARATOR),
               detect_comparator() ? "ABOVE threshold" : "below threshold");
        printf("passes   : %lu     raw FIFO words: %lu\n",
               (unsigned long)detect_events(), (unsigned long)detect_fragments());
        {
            // Printed only when it happened, so the normal status is unchanged.
            uint32_t ov = detect_dropped();
            if (ov)
                printf("           *** RX FIFO overflowed on %lu poll(s): >= %lu edge word(s)\n"
                       "           DISCARDED. Fragment counts since arm are a LOWER BOUND. ***\n",
                       (unsigned long)ov, (unsigned long)ov);
        }
        printf("coalesce : %lu us  (fragments closer than this are one ball)\n",
               (unsigned long)detect_coalesce_us());
        {
            detect_pass_t p;
            if (detect_last_pass(&p)) {
                printf("last     : #%lu at %lu ms  transit %lu us  fragments %u\n",
                       (unsigned long)p.seq, (unsigned long)p.t_ms,
                       (unsigned long)p.transit_us, p.fragments);
                if (p.fragments > 1)
                    printf("           *** %u fragments -- U15 has no hysteresis, so this\n"
                           "           transit is a LOWER BOUND (notches excluded), not a\n"
                           "           measurement. Use the ADC-derived value for this pass.\n",
                           p.fragments);
            } else {
                printf("last     : (none yet)\n");
            }
        }
    
}

static void cmd_hpf(int argc, char **argv) {

        if (argc >= 2 && !strcmp(argv[1], "test")) {
            uint32_t w = 3000u;
            // Each window runs four times plus settling, so cap it: 60 s is ~4 min.
            if (argc > 2 && !parse_u32_in(argv[2], 1u, 60000u, &w)) { bad_arg("window", argv[2], "1-60000 ms"); return; }
            if (!power_rails_ready()) {
                printf("REFUSED: +5V rail is open (state %s). U14 runs from +5VA.\n"
                       "         Use 'on' first.\n", power_state_name(power_fsm_state()));
                return;
            }
            printf("Establishing the GPIO33 polarity. Each level twice, alternating,\n"
                   "%lu ms settle + %lu ms sample each -- about %lu s total.\n",
                   (unsigned long)HPF_TEST_SETTLE_MS, (unsigned long)w,
                   (unsigned long)(4u * (HPF_TEST_SETTLE_MS + w) / 1000u));
            printf("  TRACK is DC-coupled to 0 V through R96 2M, so its SETTLED baseline\n"
                   "  must sit at ~0. HOLD is a genuine open, so C81 integrates the switch\n"
                   "  leakage and the baseline walks off. That rate is a PROPERTY OF THIS\n"
                   "  BOARD -- 2.3 to 11 mV/s at ADC5 across the boards measured so far --\n"
                   "  so it is reported below rather than assumed.\n");
            printf("  Each level is measured TWICE so an ordering artifact shows up as a\n"
                   "  disagreement between repeats instead of as a confident wrong answer.\n\n");
            hpf_test_t r;
            detect_hpf_test(&r, w);
            printf("  level     rep1 mean   rep2 mean    rep1 p-p   rep2 p-p\n");
            for (int lvl = 0; lvl < 2; lvl++)
                printf("  GPIO33=%d  %+9.4f V %+9.4f V   %8.4f   %8.4f\n", lvl,
                       (double)r.mean_v[lvl][0], (double)r.mean_v[lvl][1],
                       (double)r.drift_v[lvl][0], (double)r.drift_v[lvl][1]);
            printf("\n  separation %.1fx\n", (double)r.separation);

            // Turn the measurement into the number F6 actually needs: how long
            // the frozen baseline stays put. Two independent statistics are
            // available and they check each other --
            //
            //   (a) HOLD's mean displacement from TRACK's settled baseline,
            //       divided by the time from entering HOLD to the CENTRE of the
            //       sample window (settle + w/2);
            //   (b) HOLD's peak-to-peak within the window, less TRACK's p-p,
            //       which is the noise floor of that statistic since a pinned
            //       node cannot drift at all.
            //
            // If (a) and (b) disagree badly, something other than leakage is
            // moving the node and neither number means what it says.
            if (r.track_level >= 0) {
                int th = r.track_level ? 0 : 1;
                float mt = (r.mean_v[r.track_level][0] + r.mean_v[r.track_level][1]) * 0.5f;
                float mh = (r.mean_v[th][0] + r.mean_v[th][1]) * 0.5f;
                float pt = (r.drift_v[r.track_level][0] + r.drift_v[r.track_level][1]) * 0.5f;
                float ph = (r.drift_v[th][0] + r.drift_v[th][1]) * 0.5f;

                float t_mid = (float)HPF_TEST_SETTLE_MS * 1e-3f + (float)w * 0.5e-3f;
                float rate_a = fabsf(mh - mt) / t_mid;
                float rate_b = fmaxf(ph - pt, 0.0f) / ((float)w * 1e-3f);

                float gain = cfg()->u12b_gain > 0.1f ? cfg()->u12b_gain : 14.5f;
                float i_a  = HPF_C81_F * rate_a / gain;
                float i_b  = HPF_C81_F * rate_b / gain;

                printf("  HOLD drift  %.2f mV/s (mean displacement)   %.2f mV/s (p-p)\n",
                       (double)(rate_a * 1e3f), (double)(rate_b * 1e3f));
                printf("  leakage     %.0f pA                        %.0f pA\n",
                       (double)(i_a * 1e12f), (double)(i_b * 1e12f));
                printf("              board 1 ~250 pA, board 2 ~52 pA. Leakage roughly\n"
                       "              doubles per 10 C, so warm boards read higher.\n");
                if (rate_a > 0.1e-3f)
                    printf("  armed window ~%.0f s to walk 100 mV at ADC5. A 1-10 ms transit\n"
                           "              walks ~%.0f uV, so detection is unaffected.\n",
                           (double)(0.100f / rate_a), (double)(rate_a * 10e-3f * 1e6f));
            }

            if (r.order_effect) {
                printf("\n  *** ORDER-DEPENDENT -- NO CONCLUSION DRAWN ***\n"
                       "  The two repeats of one level disagree by more than the levels\n"
                       "  differ from each other, so what is being measured is not a\n"
                       "  property of the level. Settling or something external dominates.\n"
                       "  Try a longer window. Do NOT change HPF_SEL_TRACK on this result --\n"
                       "  an earlier version of this test had exactly this bug and reported\n"
                       "  INVERTED twice, once for each value of the constant.\n");
            } else if (!r.conclusive) {
                printf("\n  INCONCLUSIVE -- the levels did not separate (%.1fx, need 3x).\n"
                       "  That is a HARDWARE finding, not a firmware result. Scope TP9 and\n"
                       "  the U14 pins directly rather than guessing a polarity.\n",
                       (double)r.separation);
            } else if (r.polarity_ok) {
                cfg_mut()->hpf_sel_track = (uint8_t)HPF_SEL_TRACK;
                printf("\n  CONFIRMED: GPIO33=%d is TRACK, matching the compiled\n"
                       "  HPF_SEL_TRACK. No change needed.\n", r.track_level);
                printf("  The TMUX1219 datasheet agrees independently: SEL=0 selects S1,\n"
                       "  and S1 is the R96/GND leg.\n");
                printf("  Recorded in the config -- 'cfg save' to persist.\n");
            } else {
                printf("\n  *** DISAGREES WITH THE DATASHEET -- INVESTIGATE, DO NOT FLIP ***\n"
                       "  This measurement says GPIO33=%d is TRACK; HPF_SEL_TRACK is %d.\n"
                       "  But the TMUX1219 truth table says SEL=0 selects S1, and S1 is the\n"
                       "  R96/GND leg -- so 0 should be TRACK. Two independent sources\n"
                       "  disagreeing means one is being misread. Scope TP9 and the U14\n"
                       "  pins before changing anything.\n", r.track_level, HPF_SEL_TRACK);
            }
            return;
        }
        if (argc >= 2 && (!strcmp(argv[1], "track") || !strcmp(argv[1], "hold"))) {
            hpf_mode_t m = (argv[1][0] == 't') ? HPF_TRACK : HPF_HOLD;
            if (!detect_hpf_set(m)) {
                printf("REFUSED: +5V rail is open (state %s). U14 runs from +5VA, and\n"
                       "         SEL high into an unpowered mux back-feeds the analog rail.\n",
                       power_state_name(power_fsm_state()));
                return;
            }
        }
        printf("HPF baseline: %s   (GPIO33 = %d)\n",
               detect_hpf_name(detect_hpf_mode()), gpio_get_out_level(PIN_HPF_TOGGLE));
        printf("  TRACK = S1 -> R96 2M -> GND, tau 0.66 s with C81 330 nF\n"
               "  HOLD  = S2, NOT CONNECTED -- the node floats and C81 holds charge.\n"
               "          Armed means HOLD, so the baseline is frozen AND drifting.\n");
        // This used to print the HYPOTHESIS warning unconditionally, so it kept
        // warning long after `hpf test` had confirmed the polarity and written it
        // to the config -- the same failure as the hardcoded `id` banner and the
        // stored pure_delay flag: a message that does not consult the state it is
        // describing. Read the config instead.
        {
            uint8_t rec = cfg()->hpf_sel_track;
            if (rec == 0xffu)
                printf("  *** polarity is a compiled HYPOTHESIS -- 'hpf test' has not run\n"
                       "      on this board. Everything downstream (3.4, 3.6, and 3.6b\n"
                       "      TRACK-vs-HOLD isolation) is wrong if it is inverted. ***\n");
            else if (rec == (uint8_t)HPF_SEL_TRACK)
                printf("  polarity CONFIRMED on this board by 'hpf test' (TRACK = %d), and\n"
                       "  it matches the compiled HPF_SEL_TRACK and the TMUX1219\n"
                       "  truth table. Nothing to re-run.\n", rec);
            else
                printf("  *** CONFIG DISAGREES WITH THE BUILD: 'hpf test' recorded TRACK = %u,\n"
                       "      this image compiles HPF_SEL_TRACK = %d. One of them is\n"
                       "      wrong and the detect chain is unsafe until you know which.\n"
                       "      Re-run 'hpf test'. ***\n", rec, HPF_SEL_TRACK);
        }
    
}

// ---------------------------------------------------------------------------
// Phase 6: strobe -- dry tests (6a/6b) and live current (6c/6d). See strobe.h
// and BENCH_P6_STROBE.md.
// ---------------------------------------------------------------------------

static float gate_dac_volts(uint16_t level) {
    return ADC_VREF_V * (float)level / (float)(DAC_TOP + 1u);
}

static double gate_pct(uint16_t level) {
    return 100.0 * (double)level / (double)(DAC_TOP + 1u);
}

static double gate_tp3_volts(uint16_t level) {
    return (double)(gate_dac_volts(level) * STROBE_GATE_AMP_GAIN);
}

static void print_strobe_result(const strobe_result_t *r) {
    if (!r->valid) { printf("last     : none since boot\n"); return; }
    const strobe_burst_t *b = &r->burst;
    printf("last     : %s%lu x %lu us", r->live ? "LIVE " : "", (unsigned long)b->count,
           (unsigned long)b->width_us);
    if (b->count > 1u)
        printf(", gap %lu us (period %lu us)", (unsigned long)b->gap_us,
               (unsigned long)(b->width_us + b->gap_us));
    printf(" -- %s\n", strobe_run_text(r->outcome));
    if (r->live && r->outcome != STROBE_RUN_INVALID)
        printf("           at gate level %u (%.2f %%, TP3 nominal %.2f V); %.2f mC booked\n",
               r->gate_level, gate_pct(r->gate_level), gate_tp3_volts(r->gate_level),
               (double)r->booked_mc);

    if (r->outcome == STROBE_RUN_INVALID) {
        printf("           %s: width %d-%lu us, gap %d-%d us, 1-%d pulses,\n"
               "           <= %.1f mC per burst at %.1f A (clamp-limited width)\n",
               strobe_burst_err_text(r->burst_err), STROBE_MIN_WIDTH_US,
               (unsigned long)r->max_width_us, STROBE_MIN_GAP_US, STROBE_MAX_GAP_US,
               STROBE_BURST_MAX_PULSES, (double)BURST_CHARGE_MAX_MC,
               (double)STROBE_TARGET_CURRENT_A);
        if (r->burst_err == STROBE_BURST_SPAN)
            printf("           live: enable to IRQ0 must be <= %lu us; this is %lu us\n",
                   (unsigned long)STROBE_LIVE_SPAN_MAX_US, (unsigned long)r->expected_us);
        return;
    }
    if (r->outcome == STROBE_RUN_REFUSED) {
        printf("           %s\n", strobe_refusal_text(r->refusal));
        return;
    }
    printf("           %lu PIO words; IRQ0 %s; enable->IRQ0 polled at %lu us,\n"
           "           nominal %lu us (coarse -- the logic analyser is the timing truth)\n",
           (unsigned long)r->words, r->irq_seen ? "seen" : "NOT seen",
           (unsigned long)r->observed_us, (unsigned long)r->expected_us);
    printf("           GPIO25 after: %s\n", r->pin_low_after ? "low, SIO" : "*** HIGH ***");
}

// The measurement of one live firing, as the bench reads it against TP4.
static void print_live_meas(const strobe_live_shot_t *s) {
    const strobe_live_meas_t *m = &s->meas;
    printf("ADC0     : %u samples at 500 ksps, %u before the edge; baseline %.1f codes\n"
           "           (%.3f A); threshold %u codes. Verdict: %s%s\n",
           (unsigned)m->n_samples, (unsigned)m->pre_samples, (double)m->baseline_code,
           (double)strobe_code_to_amps(m->baseline_code), m->threshold_code,
           strobe_verdict_text(m->verdict), s->readback_err ? " (ADC FIFO ERROR FLAG SET)" : "");
    if (!m->detected) {
        printf("           no current above the ~%.0f mA detection floor\n",
               1000.0 * (double)strobe_code_to_amps((float)STROBE_LIVE_DETECT_MIN_CODES));
        return;
    }
    uint32_t shown = m->n_pulses < STROBE_BURST_MAX_PULSES ? m->n_pulses
                                                          : STROBE_BURST_MAX_PULSES;
    for (uint32_t k = 0; k < shown; k++) {
        const strobe_pulse_meas_t *p = &m->pulse[k];
        printf("  pulse %2lu: %7.3f A plateau   peak %7.3f A   on %4lu us (%lu samples)%s"
               "   +%lu us\n",
               (unsigned long)(k + 1u), (double)p->amps, (double)p->peak_a,
               (unsigned long)p->width_us, (unsigned long)p->samples,
               p->short_run ? ", short" : "", (unsigned long)p->start_us);
    }
    if (m->n_pulses > shown)
        printf("  ... %lu runs in all, %lu kept\n", (unsigned long)m->n_pulses,
               (unsigned long)shown);
    if (shown > 1u) {
        float first = m->pulse[0].amps, last = m->pulse[shown - 1u].amps;
        printf("  burst   : min %.3f A, max %.3f A, first -> last %+.1f %%\n",
               (double)m->min_amps, (double)m->max_amps,
               first > 0.0f ? 100.0 * (double)((last - first) / first) : 0.0);
    }
    printf("  measured charge %.3f mC. TP4 should read %.0f mV for %.3f A (135 mV/A):\n"
           "  compare it on the scope -- the two must agree.\n",
           (double)m->charge_mc, 1000.0 * (double)(m->max_amps * STROBE_SENSE_V_PER_A),
           (double)m->max_amps);
}

static void live_status(const strobe_info_t *i) {
    const strobe_live_t *l = &i->live_in;
    if (!i->live) {
        printf("live     : OFF -- last ended: %s", strobe_live_exit_text(i->live_exit));
        if (i->live_exit == STROBE_LIVE_EXIT_CONDITION)
            printf(" (%s)", strobe_refusal_text(i->live_exit_refusal));
        printf("\n");
        strobe_refusal_t a = strobe_live_may_arm(&i->snap, l);
        printf("may arm  : %s\n", a == STROBE_OK ? "yes -- 'strobe live on' prints the checklist"
                                                 : strobe_refusal_text(a));
    } else {
        printf("live     : *** ARMED *** for %lu s; idle exit in %lu s\n",
               (unsigned long)(i->live_for_ms / 1000u), (unsigned long)(i->idle_left_ms / 1000u));
        printf("watchdog : %s\n", !l->watchdog_armed ? "*** OFF ***"
               : i->live_owns_wdog ? "ARMED by live mode; disarmed when live mode ends"
                                   : "ARMED by 'wdog on'; stays armed when live mode ends");
        uint16_t lim = strobe_live_gate_limit(l->gate_proven);
        printf("staircase: proven to level %u (%.2f %%); may raise to %u (%.2f %%, TP3 %.2f V)\n"
               "           ceiling %u (%.2f %%, TP3 %.2f V)\n",
               l->gate_proven, gate_pct(l->gate_proven), lim, gate_pct(lim),
               gate_tp3_volts(lim), (unsigned)STROBE_LIVE_GATE_CEILING,
               gate_pct(STROBE_LIVE_GATE_CEILING), gate_tp3_volts(STROBE_LIVE_GATE_CEILING));
        strobe_refusal_t f = strobe_live_may_fire(&i->snap, l);
        printf("may fire : %s\n", f == STROBE_OK ? "yes" : strobe_refusal_text(f));
    }
    printf("budget   : %.2f of %.1f mC used in the last %lu s; >= %lu ms between firings\n",
           (double)i->budget_used_mc, (double)STROBE_LIVE_BUDGET_MC,
           (unsigned long)(STROBE_LIVE_BUDGET_WINDOW_MS / 1000u),
           (unsigned long)STROBE_LIVE_MIN_INTERVAL_MS);

    const strobe_cal_t *c = strobe_cal_last();
    if (c->valid) {
        if (c->res == STROBE_CAL_RES_SOLVED)
            printf("cal      : %.2f A -> level %u (%.2f %%), confirmation mean %.3f A, %s.\n"
                   "           RAM only; a reset or a new cal replaces it.\n",
                   (double)c->target_a, c->solved_level, gate_pct(c->solved_level),
                   (double)c->confirm_mean_a, c->confirm_ok ? "within 5 %" : "*** NOT within 5 % ***");
        else
            printf("cal      : last run for %.2f A: %s\n", (double)c->target_a,
                   strobe_cal_res_text(c->res));
    }

    const strobe_live_shot_t *s = strobe_live_last();
    if (s->valid) {
        printf("shot     : last live firing, %lu x %lu us at level %u (%.2f %%):\n",
               (unsigned long)s->burst.count, (unsigned long)s->burst.width_us,
               s->gate_level, gate_pct(s->gate_level));
        print_live_meas(s);
    }
}

// Live mode can end inside a command -- a verdict, a fault, the rail. Say so
// where the operator is looking, not only in `strobe`.
static void print_live_ended(void) {
    strobe_info_t i;
    strobe_info(&i);
    printf("*** LIVE MODE ENDED: %s", strobe_live_exit_text(i.live_exit));
    if (i.live_exit == STROBE_LIVE_EXIT_CONDITION)
        printf(" (%s)", strobe_refusal_text(i.live_exit_refusal));
    printf(". Gate 0, engine stopped. ***\n");
    if (fault_current() != FAULT_NONE)
        printf("    fault %s is latched: keep the record above, then 'fault clear'.\n",
               fault_name(fault_current()));
}

static void strobe_status(void) {
    strobe_info_t i;
    strobe_info(&i);

    if (i.live)
        printf("strobe   : *** LIVE *** -- pulses fire WITH the gate setpoint: real LED-bank\n"
               "           current, read back on ADC0. 'strobe live off' to leave.\n");
    else
        printf("strobe   : DRY. Pulses need the gate at 0 and the gate rises only while no\n"
               "           pulse can start, so no LED-bank current can be commanded.\n"
               "           Live current (6c/6d) is 'strobe live on confirm'.\n");
    printf("rail     : %s (state %s)   fault %s   pi %s\n",
           i.snap.rails_ready ? "up" : "DOWN", power_state_name(power_fsm_state()),
           fault_name(fault_current()), i.snap.pi_present ? "PRESENT" : "absent");

    bool limit_ok = i.limit_out_sio && !i.limit_out_level && !i.limit_pad;
    printf("GPIO27   : %s, driven %d, pad %d  -> %s\n",
           i.limit_out_sio ? "SIO output" : "*** NOT an SIO output ***",
           i.limit_out_level, i.limit_pad,
           limit_ok ? "U5 pulse limiter armed" : "*** LIMITER MAY BE DEFEATED -- STOP ***");
    printf("A7       : GPIO12 ready LED %s\n",
           i.ready_led_sio ? "on SIO (on/off); slice 6 belongs to the gate DAC"
                           : "*** NOT SIO -- the gate DAC is refused ***");

    float v = gate_dac_volts(i.gate_level);
    printf("gate DAC : level %u of %lu (%.2f %%)   hw compare %u   GPIO28 %s\n",
           i.gate_level, (unsigned long)(DAC_TOP + 1u), gate_pct(i.gate_level),
           i.gate_hw_level, i.gate_pin_pwm ? "PWM" : "*** NOT PWM ***");
    printf("           nominal %.3f V filtered -> TP3 %.3f V (x%.0f). TP3 is the truth.\n",
           (double)v, (double)(v * STROBE_GATE_AMP_GAIN), (double)STROBE_GATE_AMP_GAIN);
    if (i.gate_settling)
        printf("           zeroed < %u ms ago: Q9's gate is still decaying, dry pulses refused\n",
               (unsigned)DAC_SETTLE_MS);
    printf("engine   : %s   PIO0 SM%u @ 1 MHz, offset %u, DMA ch %d   GPIO25 %s, pad %d\n",
           i.busy ? "BUSY" : "idle", (unsigned)PIO_SM_STROBE, i.pio_offset, i.dma_channel,
           i.pulse_pin_pio ? "PIO" : "SIO", i.pulse_pin_pad);
    printf("limits   : width %d-%d us; U5 clamp <= %d us (board 1 measured 135); gap %d-%d us;\n"
           "           <= %d pulses; <= %.1f mC per burst at the %.1f A design current;\n"
           "           PIO overhead %u/%u us (confirmed on the LA, 6a.2)\n",
           STROBE_MIN_WIDTH_US, STROBE_SW_MAX_US, STROBE_HW_LIMIT_US,
           STROBE_MIN_GAP_US, STROBE_MAX_GAP_US, STROBE_BURST_MAX_PULSES,
           (double)BURST_CHARGE_MAX_MC, (double)STROBE_TARGET_CURRENT_A,
           STROBE_PIO_WIDTH_OVERHEAD_US, STROBE_PIO_GAP_OVERHEAD_US);
    print_strobe_result(&i.last);

    if (!i.live) {
        strobe_refusal_t f = strobe_may_fire(&i.snap);
        strobe_refusal_t g = strobe_may_set_gate(&i.snap, 1u);
        printf("may fire : %s\n", f == STROBE_OK ? "yes" : strobe_refusal_text(f));
        printf("may gate : %s (raising it; lowering to 0 is always allowed)\n",
               g == STROBE_OK ? "yes" : strobe_refusal_text(g));
    }
    live_status(&i);
}

static void strobe_fire(const strobe_burst_t *b, uint32_t max_width_us) {
    bool was_live = strobe_live_armed();
    strobe_result_t r = strobe_run(b, max_width_us);
    print_strobe_result(&r);
    if (r.live && r.outcome != STROBE_RUN_INVALID && r.outcome != STROBE_RUN_REFUSED)
        print_live_meas(strobe_live_last());
    if (was_live && !strobe_live_armed()) print_live_ended();
}

static void strobe_print_schedule(const strobe_schedule_t *s) {
    printf("schedule at %.2f m/s: %u um blur, %u um spacing, %u pulses, %.1f A target\n",
           (double)s->speed_mps, (unsigned)STROBE_BLUR_BUDGET_UM,
           (unsigned)STROBE_FREEZE_SPACING_UM, (unsigned)BURST_PULSES_NOMINAL,
           (double)STROBE_TARGET_CURRENT_A);
    printf("  width %lu us   period %lu us   gap %lu us   pulses %lu of %lu\n",
           (unsigned long)s->width_us, (unsigned long)s->period_us,
           (unsigned long)s->gap_us, (unsigned long)s->count,
           (unsigned long)s->count_requested);
    printf("  span %lu us (first rising edge to last falling edge)\n",
           (unsigned long)s->span_us);
    printf("  charge %.2f mC at %.1f A (limit %.1f mC)\n", (double)s->charge_mc,
           (double)STROBE_TARGET_CURRENT_A, (double)BURST_CHARGE_MAX_MC);
    if (s->width_clamped)
        printf("  width CLAMPED to %d us: the blur budget wanted more\n", STROBE_SW_MAX_US);
    if (s->gap_raised)
        printf("  gap RAISED to the %d us minimum\n", STROBE_MIN_GAP_US);
    if (s->shed)
        printf("  SHED %lu -> %lu pulses to stay under the charge limit; spacing kept,\n"
               "  so the burst covers fewer freeze positions\n",
               (unsigned long)s->count_requested, (unsigned long)s->count);
}

static void live_checklist(void) {
    printf("LIVE MODE IS NOT ARMED. It lets a pulse fire WITH the gate setpoint: real\n"
           "LED-bank current, up to ~9 A from the 36 V VIR rail through Q9 in linear mode.\n"
           "Before 'strobe live on confirm' (BENCH_P6_STROBE.md 6c board state):\n"
           "  - J3 LED bank connected on purpose; PSU 5.20 V, current limit ~3 A\n"
           "  - scope on TP4 (135 mV/A) and TP3, ground on TP1; FLIR on Q9 and HS1\n"
           "  - nothing conductive near D12 (36 V); no probe tips in fine-pitch areas\n"
           "  - no Pi on J8; beam off; detector disarmed; ADC idle; gate at 0\n"
           "Live mode then:\n"
           "  - arms the 1 s watchdog (and disarms it again on exit if it armed it)\n"
           "  - limits the CLI to: help id stat pins adc5v fault off forceoff wdog strobe\n"
           "  - gate <= level %u (%.1f %%, TP3 ~%.2f V); each raise at most %u levels\n"
           "    (%.1f %%) above the highest level already fired and measured\n"
           "  - reads every firing back on ADC0: a plateau over %.1f A (or a sample over\n"
           "    %.1f A) latches STROBE_OVERCURRENT; current before the pulse, after it,\n"
           "    or on longer than %u us latches STROBE_CLAMP. Either ends live mode\n"
           "    with the gate at 0.\n"
           "  - >= %lu ms between firings; <= %.1f mC per %lu s, booked at %.1f A\n"
           "  - ends itself after %lu s with no strobe command\n",
           (unsigned)STROBE_LIVE_GATE_CEILING, gate_pct(STROBE_LIVE_GATE_CEILING),
           gate_tp3_volts(STROBE_LIVE_GATE_CEILING), (unsigned)STROBE_LIVE_GATE_STEP_MAX,
           gate_pct(STROBE_LIVE_GATE_STEP_MAX), (double)STROBE_LIVE_I_STOP_A,
           (double)STROBE_LIVE_I_PEAK_STOP_A, (unsigned)STROBE_LIVE_MAX_ON_US,
           (unsigned long)STROBE_LIVE_MIN_INTERVAL_MS, (double)STROBE_LIVE_BUDGET_MC,
           (unsigned long)(STROBE_LIVE_BUDGET_WINDOW_MS / 1000u),
           (double)STROBE_TARGET_CURRENT_A,
           (unsigned long)(STROBE_LIVE_IDLE_TIMEOUT_MS / 1000u));
}

static void cmd_strobe_live(int argc, char **argv) {
    if (argc == 2) {
        strobe_info_t i;
        strobe_info(&i);
        live_status(&i);
        return;
    }
    if (argc == 3 && !strcmp(argv[2], "off")) {
        bool was = strobe_live_armed();
        strobe_live_disarm();
        printf(was ? "live OFF: gate 0, engine stopped, GPIO25 low. Watchdog now %s.\n"
                   : "live mode was not armed. Gate 0, engine stopped anyway. Watchdog %s.\n",
               pitrac_watchdog_enabled() ? "ARMED" : "off");
        return;
    }
    if (!strcmp(argv[2], "on") && argc == 3) {
        if (strobe_live_armed()) { printf("live mode is already armed -- 'strobe live' for its state.\n"); return; }
        live_checklist();
        return;
    }
    if (!strcmp(argv[2], "on") && argc == 4 && !strcmp(argv[3], "confirm")) {
        if (strobe_live_armed()) { printf("live mode is already armed.\n"); return; }
        strobe_refusal_t r = strobe_live_arm();
        if (r != STROBE_OK) {
            printf("REFUSED: %s. Live mode NOT armed; nothing changed.\n",
                   strobe_refusal_text(r));
            return;
        }
        printf("LIVE: ARMED. Gate 0; the staircase starts from level 0. Watchdog ARMED (1 s).\n"
               "  First step (6c): 'strobe pulse 20' at gate 0 -- expect no current --\n"
               "  then 'strobe gate 3', 'strobe pulse 20', and compare every result\n"
               "  with TP4. Raise in 3 %% steps; each firing judged OK moves the limit up.\n");
        return;
    }
    printf("usage: strobe live | strobe live on [confirm] | strobe live off\n");
}

static void cal_report(const strobe_cal_point_t *p, bool confirm) {
    printf("  %s  %4u  %6.2f %%  %5.2f V  ", confirm ? "confirm" : "step   ", p->level,
           gate_pct(p->level), gate_tp3_volts(p->level));
    if (p->detected) printf("%7.3f A\n", (double)p->amps);
    else             printf("      -- (nothing above ~72 mA)\n");
}

static void cmd_strobe_cal(int argc, char **argv) {
    float a = STROBE_TARGET_CURRENT_A;
    if (argc > 3 || (argc == 3 && !parse_float_in(argv[2], STROBE_CAL_MIN_A,
                                                   STROBE_TARGET_CURRENT_A, &a))) {
        printf("usage: strobe cal [amps]   %.1f-%.1f A, default %.1f. NOTHING FIRED.\n",
               (double)STROBE_CAL_MIN_A, (double)STROBE_TARGET_CURRENT_A,
               (double)STROBE_TARGET_CURRENT_A);
        return;
    }
    if (!strobe_live_armed()) {
        printf("REFUSED: %s\n", strobe_refusal_text(STROBE_REFUSE_NOT_LIVE));
        return;
    }
    printf("CAL: solving the gate level for %.2f A -- one %u us pulse per step from gate 0,\n"
           "  coarse %u levels until current shows, then %u. Stops above %.2f A.\n"
           "  Watch TP4. Any key aborts (gate -> 0).\n"
           "           level   gate     TP3   current\n",
           (double)a, (unsigned)STROBE_CAL_WIDTH_US, (unsigned)STROBE_CAL_COARSE_STEP,
           (unsigned)STROBE_CAL_FINE_STEP, (double)(a * STROBE_CAL_ABORT_RATIO));
    bool was_live = strobe_live_armed();
    strobe_cal_t c = strobe_cal(a, cal_report);
    if (c.res == STROBE_CAL_RES_SOLVED) {
        float err = 100.0f * (c.confirm_mean_a - a) / a;
        printf("SOLVED: level %u (%.2f %%, TP3 nominal %.2f V) for %.2f A.\n"
               "  confirmation mean %.3f A (%+.1f %%) over %lu pulses: %s\n"
               "  Gate left at level %u. RAM only -- nothing is saved.\n",
               c.solved_level, gate_pct(c.solved_level), gate_tp3_volts(c.solved_level),
               (double)a, (double)c.confirm_mean_a, (double)err, (unsigned long)c.n_confirm,
               c.confirm_ok ? "every pulse within 5 % -- PASS"
                            : "*** a pulse is outside 5 % -- do not trust this level ***",
               c.solved_level);
    } else {
        printf("CAL FAILED: %s", strobe_cal_res_text(c.res));
        if (c.res == STROBE_CAL_RES_REFUSED) printf(" -- %s", strobe_refusal_text(c.refusal));
        printf(". Gate %s.\n", strobe_live_armed() ? "back to 0, still live" : "0");
    }
    if (was_live && !strobe_live_armed()) print_live_ended();
}

static void cmd_strobe_wave(void) {
    const uint16_t *w = NULL;
    size_t n = strobe_live_wave(&w);
    const strobe_live_shot_t *s = strobe_live_last();
    if (n == 0 || !s->valid) {
        printf("no live ADC0 record yet -- fire something in live mode first.\n");
        return;
    }
    // The `capture` block format, so tools/scope.py and the analysis scripts
    // read it unchanged. The extra key=value pairs are ignored by them.
    printf("# capture mask=0x01 n=%u rate=500000 overran=%d strobe=live gate=%u "
           "width=%lu gap=%lu count=%lu pre=%u\n",
           (unsigned)n, s->readback_err ? 1 : 0, s->gate_level,
           (unsigned long)s->burst.width_us, (unsigned long)s->burst.gap_us,
           (unsigned long)s->burst.count, (unsigned)s->meas.pre_samples);
    printf("# columns: ch0\n");
    for (size_t k = 0; k < n; k++) {
        printf("%u\n", w[k]);
        // Live mode has armed the watchdog: keep it fed while CDC drains.
        if ((k % 32u) == 0) pitrac_service();
    }
    printf("# end\n");
}

static void cmd_strobe(int argc, char **argv) {
    if (argc < 2) { strobe_status(); return; }
    const char *sub = argv[1];

    if (!strcmp(sub, "off")) {
        bool was_live = strobe_live_armed();
        strobe_safe_off();
        printf("strobe off: GPIO25 SIO low, engine stopped, gate DAC 0%s.\n"
               "  Dry pulses stay refused for %u ms while Q9's gate decays.\n",
               was_live ? ", live mode OFF" : "", (unsigned)DAC_SETTLE_MS);
        return;
    }

    if (!strcmp(sub, "live")) { cmd_strobe_live(argc, argv); return; }
    if (!strcmp(sub, "cal"))  { cmd_strobe_cal(argc, argv);  return; }
    if (!strcmp(sub, "wave") && argc == 2) { cmd_strobe_wave(); return; }

    if (!strcmp(sub, "gate")) {
        float pct;
        if (argc != 3 || !parse_float(argv[2], &pct) || pct < 0.0f || pct > 100.0f) {
            printf("usage: strobe gate <0-100>   (percent of the DAC; NOTHING WAS SET)\n");
            return;
        }
        uint16_t level = (uint16_t)lroundf(pct * (float)(DAC_TOP + 1u) / 100.0f);
        bool was_live = strobe_live_armed();
        strobe_refusal_t r = strobe_gate_set(level);
        if (r != STROBE_OK) {
            printf("REFUSED: %s. Gate unchanged at level %u (%.2f %%).\n",
                   strobe_refusal_text(r), strobe_gate_level(), gate_pct(strobe_gate_level()));
            if (r == STROBE_REFUSE_STEP || r == STROBE_REFUSE_CEILING) {
                strobe_info_t i;
                strobe_info(&i);
                uint16_t lim = strobe_live_gate_limit(i.live_in.gate_proven);
                printf("  Asked for level %u. Highest allowed now: level %u (%.2f %%) -- proven\n"
                       "  to %u. Each live firing judged within limits moves the limit up.\n",
                       level, lim, gate_pct(lim), i.live_in.gate_proven);
            }
            return;
        }
        float v = gate_dac_volts(strobe_gate_level());
        printf("gate <- level %u of %lu (%.2f %%), settled %u ms\n",
               strobe_gate_level(), (unsigned long)(DAC_TOP + 1u),
               gate_pct(strobe_gate_level()), (unsigned)DAC_SETTLE_MS);
        printf("  nominal %.3f V filtered -> TP3 %.3f V. Measure TP3 and TP2 (+12 V).\n",
               (double)v, (double)(v * STROBE_GATE_AMP_GAIN));
        if (strobe_live_armed())
            printf("  LIVE: the next pulse fires at this setpoint.\n");
        else if (strobe_gate_level() != 0u)
            printf("  Dry pulses are now REFUSED until 'strobe gate 0'.\n");
        if (was_live && !strobe_live_armed()) print_live_ended();
        return;
    }

    if (!strcmp(sub, "pulse") || !strcmp(sub, "clamptest")) {
        bool clamp = (sub[0] == 'c');
        uint32_t w;
        if (argc != 3 || !parse_u32(argv[2], &w)) {
            printf("usage: strobe %s <us>   (NOTHING FIRED)\n", sub);
            return;
        }
        if (clamp && w <= (uint32_t)STROBE_SW_MAX_US) {
            printf("ERR: clamptest is for widths over %d us. Use 'strobe pulse %lu'.\n",
                   STROBE_SW_MAX_US, (unsigned long)w);
            return;
        }
        if (clamp && strobe_live_armed())
            printf("6d CLAMP TEST WITH CURRENT: commanding %lu us at gate level %u (%.2f %%).\n"
                   "  TP4 must show a pulse of about the U5 clamp (135 us on board 1), NOT\n"
                   "  %lu us. An on-time over %u us latches STROBE_CLAMP and ends live mode.\n",
                   (unsigned long)w, strobe_gate_level(), gate_pct(strobe_gate_level()),
                   (unsigned long)w, (unsigned)STROBE_LIVE_MAX_ON_US);
        else if (clamp)
            printf("CLAMP TEST: commanding %lu us. U5 should end Q10's gate pulse at its\n"
                   "  clamp -- board 1 measured 135 us (U9, the same circuit, 122.68 us), so\n"
                   "  expect ~110-%d us. Record GPIO25's width AND Q10's gate width.\n",
                   (unsigned long)w, STROBE_HW_LIMIT_US);
        strobe_burst_t b = { .width_us = w, .gap_us = 0u, .count = 1u };
        strobe_fire(&b, clamp ? (uint32_t)STROBE_CLAMPTEST_MAX_US : (uint32_t)STROBE_SW_MAX_US);
        return;
    }

    if (!strcmp(sub, "burst")) {
        strobe_burst_t b;
        if (argc != 5 || !parse_u32(argv[2], &b.width_us) || !parse_u32(argv[3], &b.gap_us)
                      || !parse_u32(argv[4], &b.count)) {
            printf("usage: strobe burst <width_us> <gap_us> <count>   (NOTHING FIRED)\n");
            return;
        }
        strobe_fire(&b, (uint32_t)STROBE_SW_MAX_US);
        return;
    }

    if (!strcmp(sub, "sched")) {
        float v;
        bool fire = (argc == 4 && !strcmp(argv[3], "fire"));
        if ((argc != 3 && !fire) || !parse_float(argv[2], &v)) {
            printf("usage: strobe sched <m/s> [fire]\n");
            return;
        }
        strobe_schedule_t s;
        strobe_plan_err_t e = strobe_compute_schedule(v, &s);
        if (e == STROBE_PLAN_BAD_SPEED) {
            printf("ERR: speed must be %.1f-%.1f m/s\n",
                   (double)STROBE_V_MIN_MPS, (double)STROBE_V_MAX_MPS);
            return;
        }
        strobe_print_schedule(&s);
        if (e == STROBE_PLAN_OVER_CHARGE) {
            printf("  *** STILL OVER the charge limit at %u pulses -- will not fire ***\n",
                   (unsigned)BURST_PULSES_MIN);
            return;
        }
        if (fire) {
            strobe_burst_t b = { .width_us = s.width_us, .gap_us = s.gap_us, .count = s.count };
            strobe_fire(&b, (uint32_t)STROBE_SW_MAX_US);
        }
        return;
    }

    printf("ERR: '%s' is not a strobe subcommand -- NOTHING WAS DONE.\n"
           "     strobe | gate <pct> | pulse <us> | burst <w> <gap> <n> |\n"
           "     clamptest <us> | sched <m/s> [fire] | off |\n"
           "     live [on [confirm] | off] | cal [A] | wave\n", sub);
}

// The watchdog can reset the board, and a reset drops the latch -- so it needs
// an off switch that does not require a rebuild. It also interacts with
// `bootsel` and `reset`, both of which now disarm it first.
static void cmd_wdog(int argc, char **argv) {
    if (argc >= 2) {
        if (!strcmp(argv[1], "on")) {
            pitrac_watchdog_enable(true);
            // Typed on purpose: it is the operator's now, not live mode's.
            if (strobe_live_armed()) strobe_live_keep_watchdog();
        } else if (!strcmp(argv[1], "off")) {
            if (strobe_live_armed()) {
                printf("REFUSED: live strobe mode is armed and requires the watchdog.\n"
                       "  'strobe live off' first. Watchdog still ARMED.\n");
                return;
            }
            pitrac_watchdog_enable(false);
        } else { printf("usage: wdog [on|off]\n"); return; }
    }
    printf("watchdog : %s\n", pitrac_watchdog_enabled() ? "ARMED (1 s)" : "off");
    printf("  OFF by default. On this board a watchdog reset is a HARD POWER CUT,\n"
           "  not a recovery -- GPIO15 goes high-Z, R12 pulls the latch open, and\n"
           "  whatever was being measured dies with it. See service.h.\n");
    printf("  Live strobe mode (6c/6d) arms it itself, where a hang with current through\n"
           "  a linear-mode FET is a genuinely different risk, and disarms it again on\n"
           "  exit -- unless it was already armed, or you type 'wdog on' while live.\n");
    printf("  'bootsel' and 'reset' disarm it themselves; you do not need to.\n");
}

static void cmd_led(int argc, char **argv) {

        if (argc < 3) { printf("usage: led r|y <0|1>\n"); return; }
        if (strcmp(argv[1], "r") && strcmp(argv[1], "y")) { bad_arg("led", argv[1], "r or y"); return; }
        if (strcmp(argv[2], "0") && strcmp(argv[2], "1")) { bad_arg("level", argv[2], "0 or 1"); return; }
        uint p = (argv[1][0] == 'r') ? PIN_LED_RED : PIN_LED_YELLOW;
        gpio_put(p, argv[2][0] == '1');
        printf("ok\n");
    
}

static void cmd_beam(int argc, char **argv) {

        if (argc < 2) {
            printf("beam     : %s\n", beam_enabled() ? "ON" : "off");
            printf("freq     : %lu Hz  (TOP=%lu)\n",
                   (unsigned long)beam_actual_freq_hz(), (unsigned long)beam_top());
            printf("duty     : %.2f %%  (level %lu)\n", (double)(beam_duty() * 100.0f),
                   (unsigned long)(beam_duty() * (beam_top() + 1)));
            // The number that actually sets LED current and junction temperature.
            // Differs from the commanded duty whenever U9's one-shot truncates
            // the high phase -- notably during `beam clamp`.
            printf("effective: %.2f %%  at the LED, after the U9 %u us clamp"
                   "   (ceiling %.0f %%)\n",
                   (double)(beam_effective_duty() * 100.0f),
                   (unsigned)BEAM_ONESHOT_CLAMP_US,
                   (double)(beam_duty_ceiling() * 100.0f));
            printf("phase    : %ld ticks  (%.2f deg)\n", (long)beam_phase_ticks(),
                   (double)(360.0f * beam_phase_ticks() / (beam_top() + 1)));

            // ---- hardware readback -------------------------------------------
            // Everything above is SOFTWARE state and proves nothing about the
            // silicon. These read the actual registers, so "firmware is not
            // driving the pin" and "my probe is wrong" stop looking alike.
            {
                uint sc = pwm_gpio_to_slice_num(PIN_MOD_PWM);
                uint sd = pwm_gpio_to_slice_num(PIN_DEMOD_PWM);
                uint32_t en = pwm_hw->en;

                // Every check below is against what the state SHOULD be RIGHT NOW,
                // not against "running". With the beam off the slices are meant to
                // be disabled, the pads SIO and the counter frozen. Flagging those
                // as faults trains the operator to ignore the readback, which
                // defeats the reason it exists -- telling "firmware is not driving
                // the pin" apart from "my probe is wrong".
                bool bon = beam_enabled();

                printf("-- hardware readback --   (checked against beam %s)\n",
                       bon ? "ON" : "off");
                printf("PWM_EN   : 0x%03lx  slice %u(car) %s   slice %u(dem) %s\n",
                       (unsigned long)en,
                       sc, ((((en >> sc) & 1u) != 0) == bon)
                               ? (bon ? "ENABLED ok" : "off ok") : "*** MISMATCH ***",
                       sd, ((((en >> sd) & 1u) != 0) == bon)
                               ? (bon ? "ENABLED ok" : "off ok") : "*** MISMATCH ***");

                printf("funcsel  : GPIO%u=%d %s   GPIO%u=%d %s   (4 = PWM, 5 = SIO)\n",
                       PIN_MOD_PWM,   (int)gpio_get_function(PIN_MOD_PWM),
                       ((gpio_get_function(PIN_MOD_PWM) == GPIO_FUNC_PWM) == bon)
                           ? (bon ? "ok" : "ok - SIO while off is deliberate")
                           : "*** MISMATCH ***",
                       PIN_DEMOD_PWM, (int)gpio_get_function(PIN_DEMOD_PWM),
                       ((gpio_get_function(PIN_DEMOD_PWM) == GPIO_FUNC_PWM) == bon)
                           ? "ok" : "*** MISMATCH ***");

                // RP2350 pads reset ISOLATED. gpio_set_function() clears it; if
                // this ever reads 1 the pad is disconnected no matter what the
                // peripheral is doing.
                printf("pad ISO  : GPIO%u=%lu  GPIO%u=%lu   (1 = pad ISOLATED, no output)\n",
                       PIN_MOD_PWM,
                       (unsigned long)((pads_bank0_hw->io[PIN_MOD_PWM]
                                        & PADS_BANK0_GPIO0_ISO_BITS) ? 1u : 0u),
                       PIN_DEMOD_PWM,
                       (unsigned long)((pads_bank0_hw->io[PIN_DEMOD_PWM]
                                        & PADS_BANK0_GPIO0_ISO_BITS) ? 1u : 0u));

                printf("slice %-2u : top %lu  cc 0x%08lx  div 0x%04lx  csr 0x%02lx\n",
                       sc, (unsigned long)pwm_hw->slice[sc].top,
                       (unsigned long)pwm_hw->slice[sc].cc,
                       (unsigned long)pwm_hw->slice[sc].div,
                       (unsigned long)pwm_hw->slice[sc].csr);
                printf("slice %-2u : top %lu  cc 0x%08lx  div 0x%04lx  csr 0x%02lx\n",
                       sd, (unsigned long)pwm_hw->slice[sd].top,
                       (unsigned long)pwm_hw->slice[sd].cc,
                       (unsigned long)pwm_hw->slice[sd].div,
                       (unsigned long)pwm_hw->slice[sd].csr);

                // The decisive test: is the counter actually advancing? At div=1
                // and 150 MHz it moves ~300 counts in 2 us, well inside one
                // 1440-count period, so the samples must differ if it is running.
                unsigned a = pwm_hw->slice[sc].ctr; busy_wait_us(2);
                unsigned b = pwm_hw->slice[sc].ctr; busy_wait_us(2);
                unsigned c = pwm_hw->slice[sc].ctr;
                printf("ctr(car) : %u -> %u -> %u   %s\n", a, b, c,
                       (a == b && b == c)
                           ? (bon ? "*** FROZEN - should be counting ***"
                                  : "frozen (ok, beam is off)")
                           : (bon ? "counting (slice is live)"
                                  : "*** COUNTING - beam is off! ***"));
            }
            printf("rails    : %s\n", power_rails_ready() ? "up" : "DOWN -- beam cannot run");
            return;
        }

        if (!strcmp(argv[1], "off")) { beam_enable(false); printf("beam off\n"); return; }

        if (!strcmp(argv[1], "on")) {
            if (!beam_enable(true)) {
                printf("ERR: +5V rail is open (state %s). The MCP1416 gate driver and D11\n"
                       "     both run from the switched rail. Use 'on' first.\n",
                       power_state_name(power_fsm_state()));
                return;
            }
            printf("beam ON at %lu Hz, %.2f %% duty\n",
                   (unsigned long)beam_actual_freq_hz(), (double)(beam_duty() * 100.0f));
            if (beam_duty() > 0.05f)
                printf("NOTE: >5%% duty from cold. Watch the ballast resistors (R73/R74)\n"
                       "      and D11 -- ~0.95 A average from +5V at 30%%.\n");
            return;
        }

        if (!strcmp(argv[1], "freq")) {
            if (argc < 3) { printf("usage: beam freq <hz>\n"); return; }
            uint32_t f;
            if (!parse_u32_in(argv[2], 1u, SYSCLK_HZ / 2u, &f)) {
                printf("ERR: freq '%s' -- want whole Hz, 1-%lu. Nothing was changed.\n",
                       argv[2], (unsigned long)(SYSCLK_HZ / 2u));
                return;
            }
            beam_configure(f, beam_duty(), beam_phase_ticks());
            printf("freq <- %lu Hz requested, %lu Hz actual (TOP=%lu, period %lu counts)\n",
                   (unsigned long)f, (unsigned long)beam_actual_freq_hz(),
                   (unsigned long)beam_top(), (unsigned long)(beam_top() + 1));
            if (beam_actual_freq_hz() != f)
                printf("      (SYSCLK/%lu is not an integer. Rounded to the NEAREST\n"
                       "       achievable period; frequency step here is ~%lu Hz.)\n",
                       (unsigned long)f,
                       (unsigned long)(SYSCLK_HZ / (beam_top() + 1)
                                     - SYSCLK_HZ / (beam_top() + 2)));
            return;
        }

        if (!strcmp(argv[1], "duty")) {
            if (argc < 3) { printf("usage: beam duty <pct>\n"); return; }
            float pct;
            if (!parse_float_in(argv[2], 0.0f, 100.0f, &pct)) { bad_arg("duty", argv[2], "0-100 %"); return; }
            float d = pct / 100.0f;
            float eff;
            if (beam_would_exceed_ceiling(beam_actual_freq_hz(), d, &eff)) {
                printf("REFUSED: %.1f%% commanded -> %.1f%% EFFECTIVE at the LED,\n"
                       "         over the %.0f%% ceiling. (U9's %u us one-shot does not\n"
                       "         truncate a %.1f%% high phase at %lu Hz.)\n",
                       (double)(d * 100.0f), (double)(eff * 100.0f),
                       (double)(beam_duty_ceiling() * 100.0f),
                       (unsigned)BEAM_ONESHOT_CLAMP_US, (double)(d * 100.0f),
                       (unsigned long)beam_actual_freq_hz());
                printf("         %.0f%% is the intended operating point (CR-12).\n",
                       (double)(BEAM_DUTY_OPERATING * 100.0f));
                return;
            }
            beam_set_duty(d);      // preserves TOP; no round-trip through freq
            printf("duty <- %.2f %%  (level %lu of %lu, effective %.2f %% at the LED)\n",
                   (double)(d * 100.0f),
                   (unsigned long)(d * (beam_top() + 1)), (unsigned long)(beam_top() + 1),
                   (double)(beam_effective_duty() * 100.0f));
            return;
        }

        if (!strcmp(argv[1], "ramp")) {
            if (argc < 3) { printf("usage: beam ramp <pct> [step_ms]\n"); return; }
            float pct;
            uint32_t ms = 250u;
            if (!parse_float_in(argv[2], 0.0f, 100.0f, &pct)) { bad_arg("duty", argv[2], "0-100 %"); return; }
            if (argc > 3 && !parse_u32_in(argv[3], 1u, 10000u, &ms)) { bad_arg("step", argv[3], "1-10000 ms"); return; }
            float d = pct / 100.0f;
            float eff;
            if (beam_would_exceed_ceiling(beam_actual_freq_hz(), d, &eff)) {
                printf("REFUSED: %.1f%% -> %.1f%% effective, over the %.0f%% ceiling.\n",
                       (double)(d * 100.0f), (double)(eff * 100.0f),
                       (double)(beam_duty_ceiling() * 100.0f));
                return;
            }
            printf("ramping %.1f%% -> %.1f%% in 1%% steps every %lu ms ...\n",
                   (double)(beam_duty() * 100.0f), (double)(d * 100.0f), (unsigned long)ms);
            printf("WATCH R73/R74 and D11. Ctrl the PSU if current climbs unexpectedly.\n");
            beam_ramp_duty(d, ms);
            printf("done -- duty now %.2f %%\n", (double)(beam_duty() * 100.0f));
            return;
        }

        if (!strcmp(argv[1], "phase")) {
            if (argc < 3) { printf("usage: beam phase <0..%lu>\n", (unsigned long)beam_top()); return; }
            int32_t p;
            if (!parse_i32(argv[2], &p)) { bad_arg("phase", argv[2], "whole ticks (wraps modulo TOP+1)"); return; }
            beam_set_phase(p);     // preserves TOP; no round-trip through freq
            printf("phase <- %ld ticks (%.2f deg of one carrier period)\n",
                   (long)beam_phase_ticks(),
                   (double)(360.0f * beam_phase_ticks() / (beam_top() + 1)));
            return;
        }

        // Q1: how wide is the U9 one-shot clamp really? Command a high phase far
        // longer than the clamp and scope TP5 -- the LED pulse is the answer.
        if (!strcmp(argv[1], "clamp")) {
            beam_configure(1000, 0.50f, beam_phase_ticks());
            {
                // Commanded high phase in microseconds, computed from what the
                // hardware actually ended up at rather than from the request --
                // below ~2289 Hz beam_configure() has to engage a clock divider.
                unsigned long lvl = (unsigned long)(beam_duty() * (beam_top() + 1));
                unsigned long hi_us = (unsigned long)((uint64_t)lvl * beam_clkdiv()
                                                      * 1000000u / SYSCLK_HZ);
                printf("Set %lu Hz / %.0f %% -> commanded high phase %lu us"
                       "  (TOP=%lu, clkdiv=%lu)\n",
                       (unsigned long)beam_actual_freq_hz(),
                       (double)(beam_duty() * 100.0f), hi_us,
                       (unsigned long)beam_top(), (unsigned long)beam_clkdiv());
                printf("Scope TP5 and measure the LOW width -- that IS the U9 clamp.\n");
                printf("  Measured 2026-08-13: 122.68 us (BEAM_ONESHOT_CLAMP_US = %u). The\n"
                       "  0.7*R68*C57 = ~86 us estimate was 30 %% short. This is U9 only:\n"
                       "  the strobe's U5 clamp is measured separately in 6a.1.\n",
                       (unsigned)BEAM_ONESHOT_CLAMP_US);
                printf("WARNING: if the LOW width equals the %lu us commanded above, the\n"
                       "         one-shot is NOT clamping and that is not t_w.\n", hi_us);
                printf("LED duty is clamp/period, NOT the %% commanded -- %.1f %% here.\n",
                       (double)(beam_effective_duty() * 100.0f));
            }
            if (!beam_enable(true)) printf("ERR: rails down -- use 'on' first.\n");
            return;
        }

        // Q2: does the '123 recover fast enough to reproduce the commanded duty as
        // frequency climbs? Its timing node has to reset via ~CLR inside each low
        // phase (6.7 us at 104 kHz).
        if (!strcmp(argv[1], "sweep")) {
            if (argc < 6) {
                printf("usage: beam sweep <f0_hz> <f1_hz> <steps> <dwell_ms>\n"
                       "e.g.   beam sweep 5000 250000 25 2000\n");
                return;
            }
            uint32_t f0, f1, n, dw;
            if (!parse_u32_in(argv[2], 1u, SYSCLK_HZ / 2u, &f0) ||
                !parse_u32_in(argv[3], 1u, SYSCLK_HZ / 2u, &f1)) {
                printf("ERR: frequencies must be whole Hz, 1-%lu. Nothing was run.\n",
                       (unsigned long)(SYSCLK_HZ / 2u));
                return;
            }
            if (!parse_u32_in(argv[4], 2u, 1000u, &n)) { bad_arg("steps", argv[4], "2-1000"); return; }
            if (!parse_u32_in(argv[5], 0u, 60000u, &dw)) { bad_arg("dwell", argv[5], "0-60000 ms"); return; }
            if (!power_rails_ready()) { printf("ERR: rails down\n"); return; }

            printf("Sweeping %lu -> %lu Hz in %lu steps, %lu ms each, at %.1f%% duty.\n",
                   (unsigned long)f0, (unsigned long)f1, (unsigned long)n,
                   (unsigned long)dw, (double)(beam_duty() * 100.0f));
            printf("Measure the ACTUAL duty at TP5 at each step and note where it stops\n");
            printf("tracking the commanded value -- that frequency is the answer to Q2.\n\n");
            printf("  step   requested   actual   TOP    level\n");

            beam_enable(true);
            for (uint32_t i = 0; i < n; i++) {
                uint32_t f = f0 + (uint64_t)(f1 - f0) * i / (n - 1);
                beam_configure(f, beam_duty(), beam_phase_ticks());
                printf("  %2lu/%lu  %8lu   %8lu  %5lu  %5lu\n",
                       (unsigned long)(i + 1), (unsigned long)n, (unsigned long)f,
                       (unsigned long)beam_actual_freq_hz(), (unsigned long)beam_top(),
                       (unsigned long)(beam_duty() * (beam_top() + 1)));
                pitrac_yield_ms(dw);
            }
            printf("\nsweep done -- beam left running at %lu Hz. 'beam off' when finished.\n",
                   (unsigned long)beam_actual_freq_hz());
            return;
        }

        printf("? 'beam %s' -- try 'help'\n", argv[1]);
    
}

static void cmd_fault(int argc, char **argv) {

        if (argc > 1 && !strcmp(argv[1], "clear")) {
            // Clear the CODE, and also acknowledge a latched PS_FAULT so the FSM
            // actually leaves that state. Clearing only the code used to leave
            // the panel ring double-blinking (it keys off the FSM state) while
            // the on-board red LED went dark (it keys off the code).
            bool latched = (power_fsm_state() == PS_FAULT);
            fault_clear();
            power_request_fault_ack();
            if (latched)
                printf("cleared, and acknowledging FAULT: the rail drops and we\n"
                       "return to STANDBY. Press the button again to power back up.\n");
            else
                printf("cleared\n");
        }
        else printf("fault = %s\n", fault_name(fault_current()));
    
}

static void cmd_reset(int argc, char **argv) {

        if (!reset_guard_ok(argc, argv, "reset")) return;

        // Same reasoning as `bootsel`: watchdog_reboot() and an armed watchdog
        // both drive the same hardware. Disarm so the reboot we asked for is
        // the one that happens.
        pitrac_watchdog_enable(false);

        // Bare sleep on purpose: we are about to reset, so there is nothing
        // to service and an abort would be meaningless. Just flush stdout.
        printf("resetting...\n"); sleep_ms(50);
        watchdog_reboot(0, 0, 0);
    
}

static void cmd_bootsel(int argc, char **argv) {

        if (!reset_guard_ok(argc, argv, "bootsel")) return;

        // 🔴 DISARM THE WATCHDOG FIRST, or this command does not work.
        //
        // reset_usb_boot() reaches BOOTSEL through the watchdog: it writes the
        // bootrom's magic into the watchdog scratch registers and triggers a
        // reset. An already-armed watchdog owns those same registers and its
        // own reset vector, so the two fight -- and even when BOOTSEL is
        // reached, a still-armed watchdog resets the chip about a second later,
        // before the host can enumerate the RPI-RP2 drive.
        //
        // Symptom: `bootsel` appears to do nothing, or the drive flickers and
        // vanishes. Introduced 2026-08-31 with the watchdog and found the same
        // day, by someone trying to reflash.
        pitrac_watchdog_enable(false);

        printf("rebooting to BOOTSEL...\n"); sleep_ms(50);
        reset_usb_boot(0, 0);
    
}

// ---------------------------------------------------------------------------
typedef struct {
    const char *name;
    const char *alias;      // NULL if none
    void      (*fn)(int argc, char **argv);
    const char *summary;    // one line, used by the help completeness check
} cli_cmd_t;

static const cli_cmd_t k_cmds[] = {
    { "help", "?", cmd_help, "this text" },
    { "id", NULL, cmd_id_cmd, "firmware + chip identity" },
    { "stat", NULL, cmd_stat_cmd, "power state, rails, faults" },
    { "pins", NULL, cmd_pins_cmd, "read every signal pin" },
    { "capture", NULL, cmd_capture_cmd, "block capture -> CSV; 'capture trig' waits for a sound" },
    { "gpio", NULL, cmd_gpio_cmd, "read / drive a pin" },
    { "adc", NULL, cmd_adc, "oversampled read" },
    { "adc5v", NULL, cmd_adc5v, "+5V_IN in volts" },
    { "adc5vcal", NULL, cmd_adc5vcal, "trim the +5V_IN scale" },
    { "adcmode", NULL, cmd_adcmode, "off|idle|armed|burst" },
    { "on", NULL, cmd_on, "request power on" },
    { "off", NULL, cmd_off, "request orderly shutdown" },
    { "forceoff", NULL, cmd_forceoff, "drop the latch immediately" },
    { "pisim", NULL, cmd_pisim, "simulated-Pi input levels" },
    { "panel", NULL, cmd_panel, "J7 panel LEDs and ring patterns" },
    { "threshold", NULL, cmd_threshold, "comparator threshold DAC (3.5)" },
    { "cfg", NULL, cmd_cfg, "flash config: show / save / default" },
    { "level", NULL, cmd_level, "LIVE % of full scale, for aiming the target" },
    { "cal", NULL, cmd_cal, "demod phase, phase model, R98 gain" },
    { "scan", NULL, cmd_scan, "carrier verification sweep (3.6)" },
    { "detect", NULL, cmd_detect, "comparator + PIO transit timer" },
    { "hpf", NULL, cmd_hpf, "gated-HPF baseline: track / hold / test" },
    { "led", NULL, cmd_led, "on-board D5/D6 override" },
    { "wdog", NULL, cmd_wdog, "show / set the hardware watchdog" },
    { "beam", NULL, cmd_beam, "carrier and demod clock (Phase 2)" },
    { "strobe", NULL, cmd_strobe, "strobe: gate DAC, pulses, schedule; dry 6a/6b, live current 6c/6d" },
    { "fault", NULL, cmd_fault, "show / clear the latched fault" },
    { "reset", NULL, cmd_reset, "soft reset (guarded)" },
    { "bootsel", NULL, cmd_bootsel, "reboot to USB storage (guarded)" },
};

static void help_check_undocumented(void) {
    bool gap = false;
    for (size_t i = 0; i < count_of(k_cmds); i++) {
        // A command counts as documented if its name appears anywhere in
        // the help text. Crude, and exactly strong enough: the failure
        // being guarded is a command with NO mention at all.
        if (strstr(k_help, k_cmds[i].name)) continue;
        if (!gap) {
            printf("\n  !! NOT DOCUMENTED ABOVE -- k_help is out of date:\n");
            gap = true;
        }
        printf("  %-26s %s\n", k_cmds[i].name, k_cmds[i].summary);
    }
    if (gap)
        printf("  Add these to k_help. This list comes from the command table,\n"
               "  so it cannot go stale the way the text above can.\n");
}

// While live strobe mode is armed, only these run. Everything else either takes
// the ADC the live readback needs (adc, adcmode, capture, cal, level, scan),
// drives a pin or peripheral live mode depends on (beam, detect, gpio, hpf, led,
// panel, threshold, pisim), writes flash (cfg), or resets the board (reset,
// bootsel). `off` and `forceoff` stay: taking the rail down is always allowed,
// and it ends live mode on the way.
static bool allowed_while_live(const char *name) {
    static const char *const k_ok[] = {
        "help", "?", "id", "stat", "pins", "adc5v", "fault", "off", "forceoff",
        "wdog", "strobe",
    };
    for (size_t i = 0; i < count_of(k_ok); i++)
        if (!strcmp(name, k_ok[i])) return true;
    return false;
}

static void dispatch(int argc, char **argv) {
    // Clear the abort latch for EVERY command, not just the long ones. A key
    // pressed to stop the previous command must not carry over and kill this
    // one -- and it also snapshots the current fault, so a command run while a
    // fault is already latched is not aborted by that pre-existing fault.
    pitrac_abort_clear();

    if (argc == 0) return;
    const char *c = argv[0];

    if (strobe_live_armed() && !allowed_while_live(c)) {
        printf("REFUSED: '%s' is not allowed while live strobe mode is armed.\n"
               "  Allowed: help id stat pins adc5v fault off forceoff wdog strobe.\n"
               "  'strobe live off' first.\n", c);
        return;
    }

    for (size_t i = 0; i < count_of(k_cmds); i++) {
        if (!strcmp(c, k_cmds[i].name) ||
            (k_cmds[i].alias && !strcmp(c, k_cmds[i].alias))) {
            k_cmds[i].fn(argc, argv);
            return;
        }
    }
    printf("? '%s' -- try 'help'\n", c);
}

// ---------------------------------------------------------------------------

static void handle_line(void) {
    char *argv[CLI_MAX_ARGS];
    int argc = 0;
    char *p = s_line;
    while (*p && argc < CLI_MAX_ARGS) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    while (*p == ' ' || *p == '\t') p++;
    if (*p) {
        printf("ERR: more than %d words -- NOTHING was run.\n", CLI_MAX_ARGS);
        printf("> ");
        return;
    }
    dispatch(argc, argv);
    printf("> ");
}

// Pull the phase model out of flash and back into RAM.
//
// cfg_init() pushes adc5v_scale, coalesce and path_mm back into the modules
// that own them, and cfg_apply_beam() pushes carrier and phase into the beam --
// but NOTHING restored the model, so s_phase_model.valid was false after every
// reset while `cfg` cheerfully printed "phase mdl: fitted". The visible symptom
// is `scan carrier` warning that it has no model and measuring every candidate
// at phase 0, which makes its SNR ranking meaningless.
//
// This is the same defect class as the 2026-08-14 one recorded in PROGRESS section
// 10 -- "saved carrier_hz/demod_phase_ticks but never restored them" -- surviving
// in a different field. Found 2026-08-25 by reading a `cfg` dump from the bench.
static void restore_phase_model(void) {
    const pitrac_cfg_t *k = cfg();
    if (!k->phase_f_hi || k->phase_f_hi <= k->phase_f_lo) return;

    s_phase_model = (cal_phase_model_t){0};
    s_phase_model.a0    = k->phase_a0;
    s_phase_model.a1    = k->phase_a1;
    s_phase_model.a2    = k->phase_a2;
    s_phase_model.f_lo  = k->phase_f_lo;
    s_phase_model.f_hi  = k->phase_f_hi;
    s_phase_model.duty  = k->cal_duty;
    s_phase_model.n_points     = 0;      // not stored; 0 = unknown
    s_phase_model.resid_rms_rad = -1.0f; // not stored; -1 = unavailable
    s_phase_model.valid = true;

    if (k->cal_duty > 0.0f) {
        // Recompute rather than trusting the stored flag. A verdict saved by an
        // older build carries that build's test, and the pure_delay test was
        // wrong until 2026-08-24 -- it compared |a0| < 0.15 against a wrapped
        // constant and called a 3.5 %-drift chain dispersive.
        s_phase_model.delay_lo_ns  = cal_model_delay_ns(&s_phase_model, k->phase_f_lo);
        s_phase_model.delay_hi_ns  = cal_model_delay_ns(&s_phase_model, k->phase_f_hi);
        s_phase_model.span_err_deg =
            fabsf(s_phase_model.delay_hi_ns - s_phase_model.delay_lo_ns) * 0.5f
            * 1.0e-9f * (float)k->phase_f_hi * 360.0f;
        s_phase_model.pure_delay = (s_phase_model.span_err_deg < CAL_PURE_DELAY_MAX_DEG);
    } else {
        // No duty recorded, so the geometric term cannot be stripped and the
        // delay is not recoverable. Fall back to whatever verdict was stored.
        s_phase_model.pure_delay = (k->phase_pure_delay != 0u);
    }
}

void cli_init(void) {
    s_len = 0;
    s_overflow = false;
    restore_phase_model();
    printf("\n%s\n> ", k_help);
}

void cli_service(void) {
    int ch;
    while ((ch = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (ch == '\r' || ch == '\n') {
            putchar('\n');
            s_line[s_len] = '\0';
            s_len = 0;
            if (s_overflow) {
                s_overflow = false;
                printf("ERR: line longer than %d characters -- NOTHING was run.\n> ",
                       CLI_MAX_LINE - 1);
                continue;
            }
            handle_line();
        } else if (ch == 8 || ch == 127) {          // backspace / delete
            // Dropped characters were never echoed, so once the operator edits,
            // the screen and the buffer agree again.
            s_overflow = false;
            if (s_len) { s_len--; printf("\b \b"); }
        } else if (ch >= 32 && ch < 127) {
            if (s_len < CLI_MAX_LINE - 1) { s_line[s_len++] = (char)ch; putchar(ch); }
            else s_overflow = true;
        }
    }
}
