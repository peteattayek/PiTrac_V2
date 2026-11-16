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
#include "camera.h"
#include "cam_plan.h"
#include "shot.h"
#include "telemetry.h"
#include "fw_update.h"
#include "fw_layout.h"
#include "pi_link.h"
#include "link_proto.h"

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

// A capture rate outside what the ADC's divider can make (independent review
// 2026-10-15: a slower request wrapped the divider and ran at another rate).
static void bad_rate(const char *got) {
    printf("ERR: rate '%s' -- want %u-%u Hz, the range the ADC can be set to. Nothing was changed.\n",
           got, (unsigned)ADC_RATE_MIN_HZ, (unsigned)ADC_RATE_MAX_HZ);
}

static const char *k_help =
    "\n"
    "PiTrac RP2354 bench CLI\n"
    "  help                       this text\n"
    "  id                         firmware + chip identity\n"
    "  stat                       power state, rails, faults\n"
    "  pins                       read every signal pin\n"
    "  hist                       halt telemetry (Phase 8.6): boots, Pi shutdowns,\n"
    "                             Pi down unrequested, power cuts to a Pi, faults\n"
    "  hist clear confirm         zero the counters (to flash after 10 s in STANDBY)\n"
    "  link                       the Pi serial link (UART1, J8.8/J8.10): lines, counters\n"
    "  upd                        firmware version, A/B partitions, the partner image,\n"
    "                             a staged update from the Pi, the last update's result\n"
    "  upd abort                  discard a staged update; cancel a pending\n"
    "                             power-on of the Pi after an update\n"
    "\n"
    "  adc <ch> [n]               oversampled read (ch: 0,1,2,5,7)\n"
    "  adc ringtest [n]           switch the ring IDLE -> BURST n times (1-20, default\n"
    "                             3), as a live strobe firing does: time when ADC0\n"
    "                             samples arrive, and check the coverage relation\n"
    "                             (samples x cycles/sample = SysTick). Fires nothing.\n"
    "  adc5v                      +5V_IN in volts (divider + cal applied)\n"
    "  adc5vcal <measured_volts>  trim the +5V_IN scale against a DMM\n"
    "  capture <mask> <n> <rate>  block capture -> CSV. mask is a channel bitmask;\n"
    "                             rate " CLI_XSTR(ADC_RATE_MIN_HZ) "-" CLI_XSTR(ADC_RATE_MAX_HZ)
    " Hz (the header gives the rate the ADC ran at)\n"
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
    "  pisim                      show the simulated-Pi input levels and J8.37\n"
    "\n"
    "  gpio <n>                   read a pin\n"
    "  gpio <n> <0|1>             drive a pin (outputs only; guarded). GPIO10, the\n"
    "                             camera trigger: high only with the rail up, no\n"
    "                             fault and no handshake running\n"
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
    "  cfg save                   persist it, with the halt telemetry. Refuses\n"
    "                             unless the machine is quiet\n"
    "  cfg default                reset the in-RAM copy (does not erase flash)\n"
    "  level                      LIVE % of full scale at the CURRENT demod phase:\n"
    "                             an approximate aiming aid. Aim for 50-70 %, then\n"
    "                             'cal demod' decides: its sweep peak and verdict\n"
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
    "                             fired and measured, under the ~70 % ceiling, and\n"
    "                             under the session cap once 'strobe cal' has solved\n"
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
    "                             mode (from gate 0; arms the watchdog unless a Pi is\n"
    "                             powered; limits the CLI)\n"
    "  strobe live off            leave live mode: gate 0, engine stopped\n"
    "                             live: every firing is read back on ADC0. Overcurrent\n"
    "                             or current outside the pulse latches a fault; a\n"
    "                             spike, dip or extra pulse ends live mode\n"
    "  strobe cal [A]             live: solve the gate level for A amps (default 9.0)\n"
    "                             and set the session cap from it\n"
    "  strobe wave                last live ADC0 record as CSV, 2 us per sample\n"
    "\n"
    "  -- Phase 7: camera handshake (PIO1). NO CAMERA ON J4 (CR-09): 7a loopback,\n"
    "  -- 7b a 3.3 V generator. The trigger is driven only with the rail up.\n"
    "  cam                        handshake engine, trigger + strobe pins (read back),\n"
    "                             the strobe gate, the last shot\n"
    "  cam shot [first_us] [m/s] [timeout_us]\n"
    "                             ONE test shot: trigger high, wait for both strobes,\n"
    "                             then a DRY burst (the S15 schedule at m/s) fired by\n"
    "                             the handshake at max(first delay, t_cam), in PIO.\n"
    "                             Defaults " CLI_XSTR(CAM_SHOT_FIRST_DELAY_US) " us, 70 m/s, "
                                  CLI_XSTR(CAM_TIMEOUT_US) " us. No strobes\n"
    "                             by the timeout: the trigger drops, NOTHING fires,\n"
    "                             CAM_TIMEOUT latches ('fault clear').\n"
    "  cam trig [first_us] [timeout_us]\n"
    "                             the handshake alone, no burst (trigger held "
                                  CLI_XSTR(CAM_TRIG_HOLD_US) " us\n"
    "                             after FIRE)\n"
    "\n"
    "  fault                      show / 'fault clear' (also acks FAULT -> STANDBY)\n"
    "  reset [force]              soft reset. REFUSED while a Pi may be powered (its\n"
    "                             3V3 sensed, or a Pi classified) or the board is\n"
    "                             still POWERING_ON, with the latch closed: a reset\n"
    "                             drops the latch = hard power cut. Checked again\n"
    "                             just before the reset, after a 50 ms wait\n"
    "  bootsel [force]            reboot to USB mass storage (same guard).\n"
    "                             both disarm the watchdog just before. USB itself\n"
    "                             cannot reset the board: 1200 baud, picotool -f off\n"
    "  wdog [on|off]              hardware watchdog. OFF by default -- a reset\n"
    "                             DROPS THE LATCH. Live strobe mode arms it itself.\n";

// ---------------------------------------------------------------------------

const char *cli_build_stamp(void) { return __DATE__ " " __TIME__; }

static void print_version(uint32_t v) {
    printf("%u.%u", (unsigned)(v >> 16), (unsigned)(v & 0xFFFFu));
}

static void cmd_id(void) {
    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);
    // Phases the image actually implements. Was hardcoded "phase0/1/1b" and
    // stayed that way through phases 2, 3 and 4 -- a version string nobody
    // updates is worse than none, because it reads as authoritative.
    printf("fw       : pitrac phases 0-4 + mic capture (block/triggered)\n"
           "           + strobe 6a/6b dry tests + 6c/6d live current (guarded, ADC0 per firing)\n"
           "           + ADC0 ring wait + DMA-counted readback, bracketed SRAM coverage (2026-10-12)\n"
           "           + E5-safe DMA stops, record proven by ADC/DMA flags, fresh + intact 5V_IN\n"
           "           + ADC at true 500 ksps (DIV 0), synchronised snapshots\n"
           "           + live readback: fixed floor, pulse shape check, two-sample overcurrent\n"
           "             (2026-10-15)\n"
           "           + Phase 8 prep: J8.37 driven only for a powered Pi, USB reboot paths\n"
           "             closed, halt telemetry ('hist'); session cap from 'strobe cal'\n"
           "             (2026-10-19)\n"
           "           + the Pi serial link ('link') and firmware updates from the Pi into\n"
           "             the other A/B partition, try-before-you-buy ('upd') (2026-10-20)\n"
           "           + Phase 7: the camera handshake (PIO1: trigger, both strobes, t_cam,\n"
           "             timeout) firing a DRY strobe burst at max(first delay, t_cam);\n"
           "             test shots only ('cam') (2026-11-10)\n"
           "           + 'cal demod' refuses under 30 %% and puts the phase back when it\n"
           "             does not commit; a saved threshold restored at boot; 'cfg save'\n"
           "             names the fix (2026-11-11)\n"
           "           + the strobe may fire with a Pi powered; live mode then arms no\n"
           "             watchdog, and ends if a Pi appears or goes away, judged from\n"
           "             its 3V3 as sensed now; 'level' is an aiming aid (2026-11-12)\n"
           "           + 'reset', 'bootsel' and 'cfg save' count a Pi's 3V3 sensed now as a\n"
           "             Pi, before the power state has classified it; 'reset' and\n"
           "             'bootsel' refuse in POWERING_ON and check again just before\n"
           "             the reboot (2026-11-12)\n");
    printf("pending  : mic onset/veto, ball speed -> a fired shot from a detected pass, live\n"
           "           current from the shot path, the Phase 8 shot messages on the link\n");
    printf("version  : ");
    print_version(PITRAC_FW_VERSION);
    {
        const fwu_layout_t *L = fwu_port_layout();
        if (L->partitioned)
            printf("  (partition %c at 0x%06lx)\n", L->run_start < L->other_start ? 'A' : 'B',
                   (unsigned long)L->run_start);
        else
            printf("  (no partition table: flat at 0x%06lx)\n", (unsigned long)L->run_start);
    }
    printf("built    : %s\n", cli_build_stamp());
#if defined(PITRAC_TEST_TRIAL_HANG) && PITRAC_TEST_TRIAL_HANG
    printf("TEST BUILD: PITRAC_TEST_TRIAL_HANG -- never buys itself; not for use\n");
#endif
    printf("board    : pitrac_ltb_v1 (RP2354B, 48 GPIO, 2MB internal flash)\n");
    printf("uid      : ");
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) printf("%02x", uid.id[i]);
    printf("\n");
    printf("sysclk   : %u Hz\n", (unsigned)clock_get_hz(clk_sys));
    printf("NOTE: for the die revision, 'bootsel' and then 'picotool info -a' on the\n");
    printf("      host (USB can no longer reboot a running board). Erratum E9\n");
    printf("      (input+pulldown latching ~2.2V) affects GPIO0/8/9 here.\n");
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
//
// "A Pi" is power_pi_maybe_present(): the FSM's classification OR the Pi's 3V3
// sensed now (image 1.10, review finding V19-F1). Until 1.9 this read the FSM's
// flag alone, which stays false while a Pi's 3V3 is already up -- for
// PI_PRESENT_DEBOUNCE_MS of late detection in BENCH_RUNNING, and in POWERING_ON
// before RAIL_SETTLE_MS -- so a plain `reset` there cut a powered Pi.
//
// And not in POWERING_ON at all (image 1.11, review finding V110-F1): the latch
// has just closed and the FSM has not yet decided whether a Pi is there, which
// is exactly when one raises its 3V3 (~5.6 ms after +5 V, BENCH_P8_PI.md 8.3).
// Undecided is not "no Pi". BENCH_RUNNING, RUNNING or STANDBY come within
// PI_DETECT_WINDOW_MS.
//
// The check is made TWICE per command: on entry, and again after the 50 ms
// output wait, immediately before the reboot (reset_still_ok() below). In 1.10
// a Pi whose 3V3 rose during that wait was cut: the entry check had passed.
// ---------------------------------------------------------------------------
static bool reset_guard_ok(int argc, char **argv, const char *cmd) {
    // A latch that is feeding J8, and a Pi that may be on it -- or the FSM still
    // deciding whether there is one.
    bool deciding = power_fsm_state() == PS_POWERING_ON;
    if (!((power_pi_maybe_present() || deciding) && gpio_get_out_level(PIN_LATCH_CONTROL)))
        return true;
    if (argc > 1 && !strcmp(argv[1], "force")) return true;

    // Not yet classified: in BENCH_RUNNING an `off` inside the FSM's debounce is
    // still a hard cut (no Pi to shut down), so the Pi is classified first.
    bool known = power_pi_present();
    bool sensed = power_pi_3v3_sensed();
    printf("REFUSED: %s, and the latch is closed (state %s).\n"
           "  '%s' resets the pads -> GPIO15 high-Z -> R12 pulls the latch open ->\n"
           "  the Pi loses +5V instantly. No shutdown, no sync, no warning.\n"
           "  %s\n"
           "  If you really mean it: '%s force'\n",
           known  ? "a Pi is present and powered"
         : sensed ? "a Pi's 3V3 is sensed (GPIO24), not yet classified"
                  : "the board is still looking for a Pi",
           power_state_name(power_fsm_state()), cmd,
           known  ? "Shut the Pi down first ('off', or the panel button), then retry."
         : sensed ? "Wait until 'stat' shows the Pi (PI_BOOTING or RUNNING, normally within\n"
                    "  0.3 s), then shut it down ('off', or the panel button) and retry."
                  : "Wait until 'stat' shows BENCH_RUNNING (no Pi: retry then) or a Pi's\n"
                    "  state (shut it down first); up to 3 s after 'on'.",
           cmd);
    return false;
}

// The same guard, AFTER the command's 50 ms output wait and immediately before
// its reboot (image 1.11, review finding V110-F1). The wait is a bare sleep, so
// the FSM does not run during it: the latch, its state and its classification
// are as they were at entry, and only the Pi's 3V3 can have changed. Called
// BEFORE the watchdog is disarmed, so a refusal here leaves it as it was.
static bool reset_still_ok(int argc, char **argv, const char *cmd) {
    if (reset_guard_ok(argc, argv, cmd)) return true;
    printf("  Nothing was done: the Pi's 3V3 rose during the 50 ms wait before '%s'.\n", cmd);
    return false;
}

static void cmd_stat(void) {
    printf("state    : %s (%lu ms)\n", power_state_name(power_fsm_state()),
           (unsigned long)power_state_elapsed_ms());
    printf("fault    : %s\n", fault_name(fault_current()));
    float v5 = 0.0f;
    adc_5vin_status_t s5 = adc_5vin_read(&v5);
    printf("5V_IN    : %.3f V   %s\n"
           "           (latch guard %.2f V, load floor %.2f V, scale %.4f)\n",
           (double)v5,
           s5 == ADC_5VIN_FRESH       ? "fresh"
         : s5 == ADC_5VIN_NOT_SAMPLED ? "NOT SAMPLED in this ADC mode -- the last good value"
         : s5 == ADC_5VIN_INVALID     ? "*** INVALID -- FIFO overflow/underflow, DMA or conversion error; ring restarted; last good value ***"
                                      : "*** STALLED -- the ring is not advancing; last good value ***",
           (double)V5_MIN_FOR_LATCH, (double)V5_MIN_UNDER_LOAD, (double)adc_get_5vin_scale());
    printf("latch    : %d   railsready %d\n",
           gpio_get_out_level(PIN_LATCH_CONTROL), power_rails_ready());
    printf("pi       : present %d  3v3 %d  userspace %d  isdown %d\n",
           power_pi_present(), gpio_get(PIN_PI_3V3_SENSE),
           gpio_get(PIN_RPI5_ON), power_pi_is_down());
    uint32_t down_ms = 0;
    if (power_pi_down_unrequested(&down_ms))
        printf("           *** DOWN WITHOUT A REQUEST, %lu s ago -- counted ('hist'). The rail\n"
               "           stays up: 'off' or the button for an orderly end. ***\n",
               (unsigned long)(down_ms / 1000u));
    printf("J8.37    : %s\n", safe_state_pi_line_name(safe_state_pi_shutdown_mode()));
    printf("button   : %s\n", gpio_get(PIN_PWR_TOGGLE) ? "released" : "PRESSED");
    {
        // Labelled by the read above, not by age == 0: that read itself takes
        // ~1 ms, so a fresh value is already 1 ms old here (review round 5).
        uint32_t age = adc_5vin_age_ms();
        printf("adc      : mode %d   ring %s   5V_IN %s\n",
               (int)adc_engine_mode(),
               adc_ring_running() ? "RUNNING" : "*** STOPPED ***",
               s5 == ADC_5VIN_FRESH ? "fresh" : (age == UINT32_MAX ? "never read" : "HELD"));
        // ch1 is not in the ARMED {5,7} or BURST {0} round-robin, so the value is
        // held rather than the ADC being stopped to go and fetch one (A1). Only
        // a FRESH reading resets the age (adc_5vin_read).
        if (s5 != ADC_5VIN_FRESH && age != UINT32_MAX)
            printf("           +5V_IN last sampled %lu ms ago\n", (unsigned long)age);
    }
    // A watchdog that can reset the board -- and so drop the latch -- must be
    // visible. It defaults OFF and is armed only deliberately; see service.h.
    printf("watchdog : %s%s\n",
           pitrac_watchdog_enabled() ? "ARMED (1 s)" : "off",
           pitrac_watchdog_enabled() ? "   a reset here DROPS THE LATCH" : "");
    printf("halt     : boots %lu, Pi down unrequested %lu, power cuts to a Pi %lu,\n"
           "           faults %lu -- 'hist'\n",
           (unsigned long)telemetry_get(TELE_BOOTS),
           (unsigned long)telemetry_get(TELE_PI_DOWN_UNREQ),
           (unsigned long)telemetry_get(TELE_PI_POWER_CUT),
           (unsigned long)telemetry_get(TELE_FAULTS));
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
    // Not a level: released is neither 0 nor 1 (board.h, "THE LINE'S REST STATE").
    printf("  %-24s %s\n", "RPI5_SHUTDOWN(43)",
           safe_state_pi_line_name(safe_state_pi_shutdown_mode()));
#if PI_SHUTDOWN_ACTIVE_LOW
    printf("  (RPI5_SHUTDOWN is ACTIVE LOW: driven 1 = deasserted)\n");
#else
    printf("  (RPI5_SHUTDOWN is ACTIVE HIGH: driven 0 = deasserted)\n");
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
    if (argc > 4 && !parse_u32_in(argv[4], ADC_RATE_MIN_HZ, ADC_RATE_MAX_HZ, &rate)) {
        bad_rate(argv[4]); return;
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
    // The rate the divider will really run at (it rounds, and clamps near 500 k);
    // the range check above guarantees one exists.
    const uint32_t run_hz = adc_capture_rate_for(rate);
    printf("armed: ch%u, threshold %u codes, %lu Hz, %.0f%% pre-trigger.\n",
           ch, thr, (unsigned long)run_hz, (double)(pref * 100.0f));
    // "  window" must stay the line prefix: tools/scope.py keys on it.
    printf("  window %.1f ms. Trigger the event now (%s). Any key aborts.\n",
           (double)ADC_CAPTURE_MAX_SAMPLES * 1000.0 / (double)run_hz,
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
    if (!parse_u32_in(argv[3], ADC_RATE_MIN_HZ, ADC_RATE_MAX_HZ, &rate)) {
        bad_rate(argv[3]); return;
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
// the USB-power guard cannot be bypassed. So is PIN_RPI5_SHUTDOWN since
// 2026-10-19: the FSM sets it on every pass, and a typed level could drive an
// unpowered Pi. PIN_CAM_TRIGGER is not raw-writable since 2026-11-10 (independent
// review H7-F3): `gpio 10 <0|1>` goes through camera_manual_trigger(), which
// refuses a high with the rail down, a fault latched, or a handshake running.
static bool gpio_writable(uint p) {
    switch (p) {
        case PIN_LED_RED: case PIN_LED_YELLOW:
        case PIN_PWR_BTN_LED: case PIN_READY_LED:
        case PIN_USB_ENABLE:
        case PIN_HPF_TOGGLE: case PIN_SYSTEM_READY:
        case PIN_IRQ_OUT:
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

    if (p == PIN_CAM_TRIGGER) {
        // Exactly 0 or 1, then the camera's own rules (camera.h).
        if (strcmp(argv[2], "0") && strcmp(argv[2], "1")) { bad_arg("level", argv[2], "0 or 1"); return; }
        bool high = argv[2][0] == '1';
        cam_start_t r = (high && shot_busy()) ? CAM_START_BUSY : camera_manual_trigger(high);
        if (r != CAM_START_OK) {
            printf("REFUSED: %s. gpio%u unchanged at %d.\n", camera_start_text(r), p,
                   gpio_get_out_level(p));
            return;
        }
        printf("gpio%u <- %d\n", p, gpio_get_out_level(p));
        return;
    }

    if (!gpio_writable(p)) {
        printf("ERR: gpio%u is not CLI-writable.\n", p);
        if (p == PIN_PULSE_LIMIT_DIS)
            printf("     GPIO27 defeats the strobe hardware watchdog. Test only,\n"
                   "     and not before Phase 6d. There is no CLI path by design.\n");
        if (p == PIN_LATCH_CONTROL)
            printf("     GPIO15 is the +5V latch. Use 'on'/'off' so the USB-power\n"
                   "     guard is applied.\n");
        if (p == PIN_RPI5_SHUTDOWN)
            printf("     GPIO43 (J8.37) belongs to the power FSM, which sets it on every\n"
                   "     pass: driven only with the latch closed and the Pi's 3V3 up,\n"
                   "     released otherwise, and asserted only for the %d ms shutdown\n"
                   "     request ('off').\n",
                   PI_SHUTDOWN_PULSE_MS);
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

// The coverage relation between two bracketed snapshots, as the live check
// sees it: residual = measured centre - samples x cps; window = (b1 + b2) / 2 +
// the DMA spread. 32-bit long is enough: a live window is < 5 M cycles.
static long coverage_residual(size_t n, uint32_t span_lo, uint32_t b1, uint32_t b2,
                              uint32_t cps) {
    long twice = 2L * (long)span_lo + (long)b2 - (long)b1 - 2L * (long)n * (long)cps;
    return twice / 2L;
}

static void print_coverage(const char *label, bool snaps_ok, size_t n, uint32_t span_lo,
                           uint32_t b1, uint32_t b2, uint32_t cps) {
    if (!snaps_ok) {
        printf("%s: *** NOT COVERED -- snapshot failed (ring not ticking, or restarted) ***\n",
               label);
        return;
    }
    bool ok = strobe_live_coverage_ok(span_lo, b1, b2, n, cps);
    printf("%s: %u samples, %lu cycles, brackets %lu/%lu (<= %u), residual %+ld\n"
           "             vs %u/sample, window +-%lu: %s\n",
           label, (unsigned)n, (unsigned long)span_lo, (unsigned long)b1, (unsigned long)b2,
           (unsigned)STROBE_LIVE_SNAP_BRACKET_MAX, coverage_residual(n, span_lo, b1, b2, cps),
           (unsigned)cps,
           (unsigned long)((b1 + b2) / 2u + STROBE_LIVE_DMA_SPREAD_CYCLES),
           ok ? "covered"
              : (b1 > STROBE_LIVE_SNAP_BRACKET_MAX || b2 > STROBE_LIVE_SNAP_BRACKET_MAX)
                    ? "*** NOT COVERED -- a snapshot bracket too wide ***"
                    : "*** NOT COVERED -- samples missing or extra ***");
}

// `adc ringtest [n]`: the IDLE -> BURST switch a live firing makes, observed.
// 2026-10-12, board 1: ~440 us after that switch the ring held only 122 ADC0
// samples. This shows when they really start arriving, and measures the
// coverage relation (bracketed snapshots, warm and cold XIP cache) without
// firing. Up to 5 runs print in full; more print a summary.
static void cmd_adc_ringtest(int argc, char **argv) {
    uint32_t reps = 3u;
    if (argc > 2 && !parse_u32_in(argv[2], 1u, 200u, &reps)) {
        bad_arg("count", argv[2], "1-200"); return;
    }
    // `detect arm` leaves the engine in IDLE, so the mode alone does not show it
    // (independent review 2026-10-12): BURST would destroy the armed detector's
    // detect and mic history.
    if (detect_armed()) {
        printf("REFUSED: the detector is armed. 'detect disarm' first. Nothing was changed.\n");
        return;
    }
    if (adc_engine_mode() != ADC_MODE_IDLE) {
        printf("REFUSED: the ADC engine is not in IDLE ('adcmode idle' first). Nothing was changed.\n");
        return;
    }
    printf("adc ringtest: IDLE -> BURST (ADC0 alone, 500 ksps = 0.5 samples/us), then\n"
           "the DMA's write index over %u us. Expect samples from ~0 us and ~%u by\n"
           "%u us. A live firing waits for %u samples (%u us of baseline).\n",
           (unsigned)ADC_PROBE_WINDOW_US, (unsigned)(ADC_PROBE_WINDOW_US / 2u),
           (unsigned)ADC_PROBE_WINDOW_US, (unsigned)STROBE_LIVE_RING_READY_SAMPLES,
           (unsigned)STROBE_LIVE_PRE_US);
    const bool full = reps <= 5u;
    uint32_t n_pairs = 0, n_covered = 0, n_snapfail = 0;
    uint32_t b_max = 0, first_max = 0, ready_max = 0;
    long r_min = 0, r_max = 0;
    bool any_r = false, any_never = false;
    for (uint32_t k = 0; k < reps; k++) {
        adc_ring_probe_t p;
        if (!adc_ring_probe(&p)) { printf("  run %lu: not run (engine not in IDLE)\n",
                                          (unsigned long)(k + 1u)); return; }
        if (p.first_us == UINT32_MAX || p.ready_us == UINT32_MAX) any_never = true;
        else {
            if (p.first_us > first_max) first_max = p.first_us;
            if (p.ready_us > ready_max) ready_max = p.ready_us;
        }
        for (int c = 0; c < 2; c++) {
            const adc_ring_pair_t *q = &p.pair[c];
            n_pairs++;
            if (!q->ok) { n_snapfail++; continue; }
            if (strobe_live_coverage_ok(q->span_lo, q->b1, q->b2, q->samples, q->cps)) n_covered++;
            if (q->b1 > b_max) b_max = q->b1;
            if (q->b2 > b_max) b_max = q->b2;
            long r = coverage_residual(q->samples, q->span_lo, q->b1, q->b2, q->cps);
            if (!any_r || r < r_min) r_min = r;
            if (!any_r || r > r_max) r_max = r;
            any_r = true;
        }
        if (full) {
            printf("  run %lu: switch %lu us; first sample ", (unsigned long)(k + 1u),
                   (unsigned long)p.switch_us);
            if (p.first_us == UINT32_MAX) printf("NEVER");
            else                          printf("+%lu us", (unsigned long)p.first_us);
            printf("; %u written ", (unsigned)STROBE_LIVE_RING_READY_SAMPLES);
            if (p.ready_us == UINT32_MAX) printf("NEVER");
            else                          printf("+%lu us", (unsigned long)p.ready_us);
            printf("; %u by +%lu us%s%s\n", (unsigned)p.written_end, (unsigned long)p.window_us,
                   p.over ? "; FIFO OVERFLOW" : "", p.conv_err ? "; CONVERSION ERROR FLAG" : "");
            printf("         written at:");
            for (size_t j = 0; j < ADC_PROBE_POINTS; j++)
                printf(" +%lu:%u", (unsigned long)p.at_us[j], (unsigned)p.written_at[j]);
            printf("\n         first %u samples:", (unsigned)ADC_PROBE_HEAD);
            for (size_t j = 0; j < ADC_PROBE_HEAD; j++) printf(" %u", p.head[j]);
            printf("  (TP4 at 0 A reads ~10)\n");
            for (int c = 0; c < 2; c++) {
                const adc_ring_pair_t *q = &p.pair[c];
                print_coverage(c == 0 ? "         pair, warm cache" : "         pair, cold cache",
                               q->ok, q->samples, q->span_lo, q->b1, q->b2, q->cps);
            }
        }
        pitrac_yield_ms(20);
    }
    printf("summary: %lu runs; first sample <= +%lu us, %u written <= +%lu us%s\n",
           (unsigned long)reps, (unsigned long)first_max, (unsigned)STROBE_LIVE_RING_READY_SAMPLES,
           (unsigned long)ready_max, any_never ? "; *** NEVER on some run ***" : "");
    printf("         coverage pairs (%u us apart, warm + cold): %lu of %lu covered, %lu snapshot\n"
           "         failures; widest bracket %lu cycles (<= %u); residual %+ld .. %+ld cycles\n",
           (unsigned)ADC_PROBE_PAIR_US, (unsigned long)n_covered, (unsigned long)n_pairs,
           (unsigned long)n_snapfail, (unsigned long)b_max, (unsigned)STROBE_LIVE_SNAP_BRACKET_MAX,
           r_min, r_max);
    printf("         %s\n", n_covered == n_pairs
           ? "PASS: every pair covered."
           : "*** FAIL: a live readback in these conditions would be NO USABLE DATA ***");
}

static void cmd_adc(int argc, char **argv) {

        if (argc >= 2 && !strcmp(argv[1], "ringtest")) { cmd_adc_ringtest(argc, argv); return; }
        if (argc < 2) { printf("usage: adc <ch> [n] | adc ringtest [n]\n"); return; }
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
        // Trim the reading the power FSM decides on: ch1 of the IDLE ring, through
        // adc_5vin_read(). Until 2026-10-15 the raw value came from a blocking
        // single-channel read, which settles fully through the 50 kOhm divider and
        // reads ~0.6 % above the ring at 500 ksps, so the trim did not reach
        // `stat` (PROGRESS section 9, 2026-10-12).
        if (adc_engine_mode() != ADC_MODE_IDLE) {
            printf("REFUSED: adc5vcal trims the IDLE ring's +5V_IN reading, the one the\n"
                   "  power FSM uses; the ADC is in mode %d. 'adcmode idle' first. Nothing changed.\n",
                   (int)adc_engine_mode());
            return;
        }
        float now = 0.0f;
        adc_5vin_status_t st = adc_5vin_read(&now);
        if (st != ADC_5VIN_FRESH) {
            printf("REFUSED: no fresh +5V_IN reading from the ring (status %d). Nothing changed.\n",
                   (int)st);
            return;
        }
        float raw  = now / adc_get_5vin_scale();      // the scale is always > 0.5
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
        printf("RPI5_SHUTDOWN(43)= %s\n"
               "                   -> scope J8.37 (%s). Driven only with the latch\n"
               "                   closed and PI_3V3_SENSE up; released otherwise.\n",
               safe_state_pi_line_name(safe_state_pi_shutdown_mode()),
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
                const char *hint = cfg_save_unblock_hint();
                printf("REFUSED: %s. Nothing was saved.\n", why);
                printf("  To save: %s.\n", hint ? hint : "make the machine quiet first");
                printf("  Why: a 4 KB erase holds interrupts off for tens of ms, and during\n"
                       "  that window the power FSM does not run -- so the V5_MIN_SUSTAINED\n"
                       "  monitor is blind. That is the monitor that stops a Pi being fed\n"
                       "  through a 1 A diode.\n");
                return;
            }
            if (cfg_save())
                printf("saved: %s, seq %lu -- calibration and the halt telemetry\n",
                       cfg_source(), (unsigned long)cfg()->seq);
            else
                printf("SAVE FAILED (readback did not verify)\n");
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
            if (why) {
                const char *hint = cfg_save_unblock_hint();
                printf("save     : refused now: %s\n"
                       "           to save: %s\n", why, hint ? hint : "make the machine quiet first");
            } else {
                printf("save     : permitted now\n");
            }
        }
    
}

static void cmd_level(int argc, char **argv) {
    (void)argc;
    (void)argv;

        if (!beam_enabled())      { printf("REFUSED: beam is off.\n"); return; }
        if (!power_rails_ready()) { printf("REFUSED: +5V rail is open.\n"); return; }
        if (detect_hpf_mode() != HPF_TRACK)
            printf("WARNING: HPF is in HOLD. A chopped reading needs TRACK -- 'hpf track'.\n");

        // The CURRENT phase only (image 1.5's reading, kept from 1.7). 1.6's all-phase
        // version stepped the phase faster than the HPF settles and clipped; see
        // the note above CAL_LEVEL_CYCLES in cal.h (review finding V16-F1). From 1.8
        // it is worded as an aiming aid, not a verdict (1.7 review note V17-N1).
        printf("live level: chopping at %u Hz, reading the peak at the CURRENT phase\n"
               "  (%ld ticks, duty %.2f %%). An approximate aiming aid, not a verdict:\n"
               "  one reading at one phase, which can sit below 'cal demod's sweep peak\n"
               "  even after a phase is committed. On a board that has never committed\n"
               "  a phase it can read near 0 beside a good target: to set the level from\n"
               "  scratch, run 'cal demod' and follow its verdict.\n",
               (unsigned)CAL_CHOP_HZ_DEFAULT, (long)beam_phase_ticks(),
               (double)(beam_duty() * 100.0f));
        printf("AIM for 50-70 %% of full scale, then run 'cal demod': its sweep peak and\n"
               "  verdict decide (it will not commit under %.0f %%).\n"
               "  Too low: aim the board at a plain wall or a white card, ~2 m away, and\n"
               "  move it closer. Too high: further away, or a card over D12. Then CLAMP\n"
               "  IT -- a hand cannot hold still for the 128 s 'cal model' takes.\n"
               "Any key stops; gives up after %u s.\n\n",
               (double)(CAL_PEAK_MIN_FRAC * 100.0f), (unsigned)(LEVEL_MAX_MS / 1000u));

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
                                : (pct < CAL_PEAK_MIN_FRAC * 100.0f)
                                                ? "*** very low -- more light ***"
                                : (pct < 50.0f) ? "low  -- more light"
                                                : "aim OK -- 'cal demod' decides";
            printf("  peak %5.0f = %3.0f %%  |%s|  %s\n",
                   (double)pk, (double)pct, bar, verdict);

            // pitrac_service() consumes the keystroke inside the yield, so a
            // local getchar() here would almost never see one. Ask the latch.
            if (pitrac_abort_pending()) break;
        }
        adc_engine_set_mode(ADC_MODE_IDLE);
        printf("\nstopped. Beam left ON at %.2f %% duty; phase unchanged at %ld ticks.\n",
               (double)(beam_duty() * 100.0f), (long)beam_phase_ticks());
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
            // The sweep steps the phase across the grid and leaves it on the last
            // point. Up to image 1.5 a sweep that was NOT committed (or aborted)
            // left the beam there, 1417 ticks at 104 kHz, instead of on the phase
            // it had. Kept here and put back below whenever nothing is committed.
            int32_t phase_before = beam_phase_ticks();
            // Print the frequency. `cal model` used to leave the beam at
            // 200 kHz, and without this line nothing on screen said which
            // carrier was actually being calibrated.
            printf("sweeping demod phase at %lu Hz (TOP+1 = %lu), ~26 s ...\n",
                   (unsigned long)beam_actual_freq_hz(),
                   (unsigned long)(beam_top() + 1u));
            bool ok = cal_demod_phase(beam_actual_freq_hz(), CAL_PHASE_POINTS,
                                      CAL_CHOP_CYCLES, CAL_CHOP_HZ_DEFAULT, &r);
            adc_engine_set_mode(ADC_MODE_IDLE);
            if (!ok || !cal_demod_commit_ok(&r)) beam_set_phase(phase_before);
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
                       ? "  *** ABOVE FULL SCALE -- impossible for a real signal ***"
                   : r.too_small
                       ? "  *** TOO SMALL -- under the floor below ***" : "",
                   (double)r.form_factor, (double)(r.duty * 100.0f));
            printf("floor    : %.0f %% of full scale to commit (target 50-70 %%)%s\n",
                   (double)(CAL_PEAK_MIN_FRAC * 100.0f),
                   r.too_small ? "  *** FAIL ***" : "");
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
            // Image 1.6: committed only if clean AND above the floor. `ok` alone
            // (cal_demod_phase()'s return) is the quality verdict; `cal model`
            // still uses that for its points.
            if (ok && cal_demod_commit_ok(&r)) {
                // These two describe the COMMITTED phase, so they are written
                // with it. Up to 1.5 they were written before the verdict, and a
                // refused sweep at another duty relabelled the phase still held.
                cfg_mut()->cal_warm = r.warm ? 1u : 0u;
                // The duty is part of the answer, not context: phase_ticks carries
                // a -(duty/2)*(TOP+1) geometric term, so a phase without the duty
                // it was measured at cannot be used at any other duty.
                cfg_mut()->cal_duty = r.duty;
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
                } else if (r.too_small) {
                    printf("  THE SIGNAL IS TOO SMALL. Add LIGHT: take any card off D12; then\n"
                           "  aim the board at a plain wall or a white card, starting ~2 m away,\n"
                           "  and move it closer or angle it. Clamp the target, then run\n"
                           "  'cal demod' again: the aim is 50-70 %% of full scale.\n");
                } else {
                    printf("  Check the reflector, the HPF mode and that the beam is on.\n");
                }
                printf("  The phase is back at %ld ticks, where it was before the sweep.\n",
                       (long)beam_phase_ticks());
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
    // Booked only once the firing is admitted and the ring is ready -- r->booked
    // says whether it was (independent review 2026-10-12).
    if (r->live && r->outcome != STROBE_RUN_INVALID)
        printf("           at gate level %u (%.2f %%, TP3 nominal %.2f V); %.2f mC %s\n",
               r->gate_level, gate_pct(r->gate_level), gate_tp3_volts(r->gate_level),
               (double)r->booked_mc, r->booked ? "booked" : "NOT booked");

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
    if (r->outcome == STROBE_RUN_NO_ADC) {
        if (r->booked) {
            printf("           ADC0 ring ready at +%lu us, then NOT ticking at the enable.\n"
                   "           Nothing fired (the charge was booked first).\n",
                   (unsigned long)r->ring_ready_us);
        } else {
            printf("           ADC0 ring: first sample ");
            if (r->ring_first_us == UINT32_MAX) printf("NEVER");
            else                                printf("+%lu us", (unsigned long)r->ring_first_us);
            printf("; %u samples not written by +%lu us. Nothing fired.\n",
                   (unsigned)STROBE_LIVE_RING_READY_SAMPLES, (unsigned long)r->ring_ready_us);
        }
        printf("           Run 'adc ringtest' and send the output.\n");
        return;
    }
    if (r->outcome == STROBE_RUN_NO_FEED) {
        printf("           %lu PIO words; nothing was enabled%s. GPIO25 after: %s\n",
               (unsigned long)r->words, r->booked ? " (the charge was booked first)" : "",
               r->pin_low_after ? "low, SIO" : "*** HIGH ***");
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
           "           (%.3f A); floor %u codes. Verdict: %s%s\n",
           (unsigned)m->n_samples, (unsigned)m->pre_samples, (double)m->baseline_code,
           (double)strobe_code_to_amps(m->baseline_code), m->threshold_code,
           strobe_verdict_text(m->verdict),
           s->readback_err ? " (RECORD NOT PROVEN COMPLETE -- see 'record:' below)" : "");
    if (s->readback_err) {
        static const char *const k_why[] = {
            "ADC not free-running at the stop", "READY did not return", "FIFO not drained",
            "DMA stop did not complete", "DMA bus error", "FIFO OVERFLOW (conversions lost)",
            "FIFO underflow", "conversion-error sample",
        };
        printf("           record:");
        for (unsigned b = 0; b < 8u; b++)
            if (s->freeze_flags & (1u << b)) printf(" [%s]", k_why[b]);
        printf("\n");
    }
    printf("           ring: first sample +%lu us, %u ready +%lu us after the switch;\n",
           (unsigned long)s->ring_first_us, (unsigned)STROBE_LIVE_RING_READY_SAMPLES,
           (unsigned long)s->ring_ready_us);
    print_coverage("           enable->post", s->cov_snapshots, s->cov_samples, s->cov_span_lo,
                   s->cov_b1, s->cov_b2, s->cov_cps);
    printf("           baseline split by clock %u, by DMA count %u (the smaller is used,\n"
           "           >= %u needed)\n",
           (unsigned)s->pre_by_time, (unsigned)s->pre_by_index,
           (unsigned)STROBE_LIVE_PRE_MIN_SAMPLES);
    // An unusable record says nothing either way: no "no current" from it
    // (independent review 2026-10-15) -- nor from one not proven complete.
    if (m->verdict == STROBE_VERDICT_NO_DATA) {
        printf("           NO MEASUREMENT: too little baseline or record to judge (see the\n"
               "           lines above). Nothing is known about current in this firing.\n");
        return;
    }
    // Which overcurrent rule fired: the pulse list alone may not show it (two of
    // three samples, or a lone sample over I_PEAK_STOP outside any pulse).
    if (m->verdict == STROBE_VERDICT_OVERCURRENT) {
        printf("           OVERCURRENT (%.1f A sustained, %.1f A in one sample):",
               (double)STROBE_LIVE_I_STOP_A, (double)STROBE_LIVE_I_PEAK_STOP_A);
        if (m->max_amps > STROBE_LIVE_I_STOP_A)
            printf(" plateau %.3f A;", (double)m->max_amps);
        if (m->max_held_a > STROBE_LIVE_I_STOP_A)
            printf(" two samples in a row at %.3f A;", (double)m->max_held_a);
        if (m->over_2of3)
            printf(" two of three samples over from +%lu us;", (unsigned long)m->over_2of3_us);
        if (m->max_peak_a > STROBE_LIVE_I_PEAK_STOP_A)
            printf(" a sample at %.3f A;", (double)m->max_peak_a);
        printf("\n");
    }
    if (!m->detected && s->readback_err) {
        printf("           no current pulse found, but the record is NOT proven complete:\n"
               "           that is not evidence of no current.\n");
        return;
    }
    if (!m->detected) {
        printf("           no current pulse above the ~%.0f mA floor\n",
               1000.0 * (double)strobe_code_to_amps((float)STROBE_LIVE_DETECT_MIN_CODES));
        // Lone samples over the floor are not a pulse, but they are not hidden
        // either (until 2026-10-15 this line also covered pulses that the old
        // half-peak threshold had missed).
        if ((float)m->peak_code > m->baseline_code + (float)STROBE_LIVE_DETECT_MIN_CODES)
            printf("           but single samples were above it, the highest %.3f A at +%lu us:\n"
                   "           ADC noise, or a pulse shorter than ~4 us -- compare with TP4\n",
                   (double)m->max_peak_a, (unsigned long)m->peak_us);
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
    if (m->shape != STROBE_SHAPE_CLEAN) {
        const double margin_a =
            (double)strobe_code_to_amps((float)STROBE_LIVE_SHAPE_MARGIN_CODES);
        const bool over = m->shape == STROBE_SHAPE_OVERSHOOT;
        printf("  NOT CLEAN: pulse %lu %s -- %.3f A at +%lu us against a settled %.3f A;\n"
               "           bound %.3f A (settled x %.2f %s %.3f A)\n",
               (unsigned long)(m->shape_pulse + 1u), strobe_shape_text(m->shape),
               (double)m->shape_a, (unsigned long)m->shape_at_us,
               (double)m->shape_settled_a, (double)m->shape_limit_a,
               over ? 1.0 + (double)STROBE_LIVE_SHAPE_TOL : 1.0 - (double)STROBE_LIVE_SHAPE_TOL,
               over ? "+" : "-", margin_a);
    }
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
        printf("watchdog : %s\n",
               !l->watchdog_armed ? (l->pi_at_arm
                                     ? "off: not armed with a Pi powered (a reset would cut its power)"
                                     : "*** OFF ***")
               : i->live_owns_wdog ? "ARMED by live mode; disarmed when live mode ends"
                                   : "ARMED by 'wdog on'; stays armed when live mode ends");
        uint16_t lim = strobe_live_raise_limit(l->gate_proven, l->session_cap);
        printf("staircase: proven to level %u (%.2f %%); may raise to %u (%.2f %%, TP3 %.2f V)\n"
               "           ceiling %u (%.2f %%, TP3 %.2f V)\n",
               l->gate_proven, gate_pct(l->gate_proven), lim, gate_pct(lim),
               gate_tp3_volts(lim), (unsigned)STROBE_LIVE_GATE_CEILING,
               gate_pct(STROBE_LIVE_GATE_CEILING), gate_tp3_volts(STROBE_LIVE_GATE_CEILING));
        if (l->session_cap)
            printf("cap      : level %u (%.2f %%, TP3 %.2f V), from 'strobe cal': where the\n"
                   "           current could first reach the %.1f A stop. Re-run the cal to move it.\n",
                   l->session_cap, gate_pct(l->session_cap), gate_tp3_volts(l->session_cap),
                   (double)STROBE_LIVE_I_STOP_A);
        else
            printf("cap      : none yet -- a solved 'strobe cal' sets one\n");
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

// strobe_live_exit_count() as of the last notice: an exit is announced once.
static uint32_t s_live_exit_shown;

// Whatever told the operator live mode ended -- this notice, or a command's
// own wording (`strobe live off`, `strobe off`) -- acknowledges it.
static void live_exit_ack(void) { s_live_exit_shown = strobe_live_exit_count(); }

// Live mode can end inside a command -- a verdict, a fault, the rail -- or in
// the background. Say so where the operator is looking, not only in `strobe`.
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
    live_exit_ack();
}

// Called by dispatch() before and after every command.
static void live_exit_notice(void) {
    if (strobe_live_exit_count() != s_live_exit_shown) print_live_ended();
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
           fault_name(fault_current()), strobe_pi_text(i.snap.pi));

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
    // NO_ADC fired nothing: strobe_live_last() is still the PREVIOUS firing.
    if (r.live && r.outcome != STROBE_RUN_INVALID && r.outcome != STROBE_RUN_REFUSED &&
        r.outcome != STROBE_RUN_NO_ADC && r.outcome != STROBE_RUN_NO_FEED)
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
           "  - scope on TP4 (135 mV/A) and TP3, grounds on the R66 GND-pad wires\n"
           "    (not TP1 or TP5); FLIR on Q9 and HS1; 'adc 0' < 10 codes\n"
           "  - nothing conductive near D12 (36 V); no probe tips in fine-pitch areas\n"
           "  - beam off; detector disarmed; ADC idle; gate at 0\n"
           "  - a Pi on J8 only by BENCH_P8_PI.md 8.10, with the owner's go: then\n"
           "    the PSU limit >= 5 A\n"
           "Live mode then:\n"
           "  - arms the 1 s watchdog, and disarms it again on exit if it armed it.\n"
           "    NOT with a Pi powered: a watchdog reset would cut the Pi's power.\n"
           "    A Pi that appears or goes away while live ends live mode, and so\n"
           "    does a Pi whose 3V3 and power state disagree.\n"
           "  - limits the CLI to: help id stat pins hist adc5v fault off forceoff wdog\n"
           "    strobe\n"
           "  - gate <= level %u (%.1f %%, TP3 ~%.2f V); each raise at most %u levels\n"
           "    (%.1f %%) above the highest level already fired and measured; once\n"
           "    'strobe cal' solves, never above its session cap\n"
           "  - reads every firing back on ADC0, against a fixed ~%.0f mA floor:\n"
           "    a plateau, or two of any three samples in a row, over %.1f A, or one\n"
           "    sample over %.1f A, latches STROBE_OVERCURRENT; current before the\n"
           "    pulse, after it, or on longer than %u us latches STROBE_CLAMP; a spike\n"
           "    or dip beyond +-%.0f %% (+%.2f A) of a pulse's settled level, or more\n"
           "    pulses than fired, ends live mode without a fault. Each leaves the gate\n"
           "    at 0.\n"
           "  - >= %lu ms between firings; <= %.1f mC per %lu s, booked at %.1f A\n"
           "  - ends itself after %lu s with no strobe command\n",
           (unsigned)STROBE_LIVE_GATE_CEILING, gate_pct(STROBE_LIVE_GATE_CEILING),
           gate_tp3_volts(STROBE_LIVE_GATE_CEILING), (unsigned)STROBE_LIVE_GATE_STEP_MAX,
           gate_pct(STROBE_LIVE_GATE_STEP_MAX),
           1000.0 * (double)strobe_code_to_amps((float)STROBE_LIVE_DETECT_MIN_CODES),
           (double)STROBE_LIVE_I_STOP_A,
           (double)STROBE_LIVE_I_PEAK_STOP_A, (unsigned)STROBE_LIVE_MAX_ON_US,
           100.0 * (double)STROBE_LIVE_SHAPE_TOL,
           (double)strobe_code_to_amps((float)STROBE_LIVE_SHAPE_MARGIN_CODES),
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
        if (was) live_exit_ack();
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
        printf("LIVE: ARMED. Gate 0; the staircase starts from level 0. %s\n"
               "  First step (6c): 'strobe pulse 20' at gate 0 -- expect no current --\n"
               "  then 'strobe gate 3', 'strobe pulse 20', and compare every result\n"
               "  with TP4. Raise in 3 %% steps; each firing judged OK moves the limit up.\n",
               pitrac_watchdog_enabled()
                   ? "Watchdog ARMED (1 s)."
                   : "Watchdog NOT armed: a Pi is powered, and a reset would cut its power.");
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
        printf("  Session cap: level %u (%.2f %%, TP3 nominal %.2f V). No raise above it until\n"
               "  the next cal or re-arm: it is where the current could first reach the\n"
               "  %.1f A stop, at %.3f A per level -- the steepest any board can have.\n",
               c.session_cap, gate_pct(c.session_cap), gate_tp3_volts(c.session_cap),
               (double)STROBE_LIVE_I_STOP_A, (double)STROBE_SESSION_CAP_A_PER_LEVEL);
    } else {
        printf("CAL FAILED: %s", strobe_cal_res_text(c.res));
        if (c.res == STROBE_CAL_RES_REFUSED) printf(" -- %s", strobe_refusal_text(c.refusal));
        printf(". Gate %s.\n", strobe_live_armed() ? "back to 0, still live" : "0");
        if (strobe_live_armed())
            printf("  No session cap: the ceiling and the staircase apply, as before a cal.\n");
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
        if (was_live) live_exit_ack();
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
            if (r == STROBE_REFUSE_STEP || r == STROBE_REFUSE_CEILING ||
                r == STROBE_REFUSE_SESSION_CAP) {
                strobe_info_t i;
                strobe_info(&i);
                uint16_t lim = strobe_live_raise_limit(i.live_in.gate_proven,
                                                       i.live_in.session_cap);
                printf("  Asked for level %u. Highest allowed now: level %u (%.2f %%) -- proven\n"
                       "  to %u. Each live firing judged within limits moves the staircase up.\n",
                       level, lim, gate_pct(lim), i.live_in.gate_proven);
                if (i.live_in.session_cap)
                    printf("  Session cap: level %u, from the last 'strobe cal'. The staircase\n"
                           "  cannot pass it.\n", i.live_in.session_cap);
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

// ---------------------------------------------------------------------------
// Phase 7: the camera handshake (BENCH_P5_P7_MIC_CAMERA.md 7a/7b).
//
// A test shot starts at the handshake: no beam, no detector. shot.c runs it from
// the superloop; this command starts it, yields until it is logged, and prints
// it. Times from the trigger's rise are the PIO's own counts (cam_plan.h), exact
// to one 5-cycle loop; the "CPU saw" times are polled and only coarse.
// ---------------------------------------------------------------------------

static float cyc_us(uint32_t cycles, uint32_t hz) { return cam_cycles_to_us(cycles, hz); }

static void print_shot(const shot_result_t *s) {
    const cam_run_t *c = &s->cam;
    const uint32_t hz = c->plan.sys_hz ? c->plan.sys_hz : (uint32_t)clock_get_hz(clk_sys);
    printf("shot     : #%lu (%s) -- %s\n", (unsigned long)s->seq,
           s->test ? "test" : "detected pass", shot_end_text(s->end));

    if (s->end == SHOT_END_REFUSED) {
        if (s->plan_err != CAM_PLAN_OK)
            printf("           plan: %s\n", cam_plan_err_text(s->plan_err));
        else if (s->req.strobe && s->strobe.outcome != STROBE_RUN_TIMEOUT &&
                 s->cam_start == CAM_START_OK) {
            printf("           strobe: %s\n", strobe_run_text(s->strobe.outcome));
            if (s->strobe.outcome == STROBE_RUN_REFUSED)
                printf("           %s\n", strobe_refusal_text(s->strobe.refusal));
            if (s->strobe.outcome == STROBE_RUN_INVALID)
                printf("           %s\n", strobe_burst_err_text(s->strobe.burst_err));
        } else {
            printf("           camera: %s\n", camera_start_text(s->cam_start));
        }
        return;
    }
    if (!c->valid) return;

    printf("plan     : first delay %lu us, timeout %lu us, hold %lu us -> D %lu, T %lu, H %lu\n"
           "           (5-cycle loops at %lu Hz = %.1f ns)\n",
           (unsigned long)s->cam_req.first_delay_us, (unsigned long)s->cam_req.timeout_us,
           (unsigned long)s->cam_req.hold_us, (unsigned long)c->plan.d,
           (unsigned long)c->plan.t, (unsigned long)c->plan.h, (unsigned long)hz,
           (double)((double)CAM_PIO_LOOP_CYCLES * 1.0e9 / (double)hz));

    const cam_decoded_t *d = &c->res;
    if (c->words == 0u) {
        printf("handshake: %s (no word from the engine)\n", camera_run_text(c->state));
    } else if (d->outcome == CAM_OUT_TIMEOUT) {
        printf("handshake: TIMEOUT -- both strobes not seen. Trigger dropped at %.3f us\n"
               "           after it rose; no FIRE was raised.\n",
               (double)cyc_us(cam_timeout_release_cycles(&c->plan), hz));
    } else if (d->outcome == CAM_OUT_EARLY || d->outcome == CAM_OUT_LATE) {
        printf("handshake: both strobes high at t_cam = %.3f us (to one loop, %.1f ns)\n",
               (double)cyc_us(d->detect_cycles, hz), (double)((double)CAM_PIO_LOOP_CYCLES * 1.0e9 / (double)hz));
        if (d->outcome == CAM_OUT_EARLY)
            printf("           within the first delay: FIRE at %.3f us (the first delay)\n",
                   (double)cyc_us(d->fire_cycles, hz));
        else
            printf("           AFTER the first delay (%lu us): FIRE at once, %.3f us\n"
                   "           (the delay clamped to zero -- no underflow)\n",
                   (unsigned long)s->cam_req.first_delay_us, (double)cyc_us(d->fire_cycles, hz));
        printf("           trigger released at %.3f us (FIRE + the hold)\n",
               (double)cyc_us(cam_release_after_fire_cycles(&c->plan, d->fire_cycles), hz));
    } else {
        printf("handshake: %s\n", cam_outcome_text(d->outcome));
    }
    printf("           engine: %s. Words %08lx %08lx. CPU saw them %lu / %lu us\n"
           "           after the start. Trigger pad low afterwards: %s\n",
           camera_run_text(c->state), (unsigned long)c->word0, (unsigned long)c->word1,
           (unsigned long)c->cpu_ready_us, (unsigned long)c->cpu_end_us,
           c->trigger_low_after ? "yes" : "*** NO ***");

    if (s->req.strobe) {
        const strobe_burst_t *b = &s->req.burst;
        printf("strobe   : DRY burst %lu x %lu us, gap %lu us -- %s\n",
               (unsigned long)b->count, (unsigned long)b->width_us, (unsigned long)b->gap_us,
               strobe_run_text(s->strobe.outcome));
        if (s->end == SHOT_END_FIRED)
            printf("           %u pulses; the first rising edge %.1f-%.1f us after the trigger\n"
                   "           (FIRE + %u-%u us: the burst engine's 1 MHz start)\n",
                   (unsigned)s->pulses_fired,
                   (double)(cyc_us(d->fire_cycles, hz) + (float)STROBE_PIO_GATE_TO_EDGE_US),
                   (double)(cyc_us(d->fire_cycles, hz) + (float)STROBE_PIO_GATE_TO_EDGE_US + 1.0f),
                   (unsigned)STROBE_PIO_GATE_TO_EDGE_US, (unsigned)STROBE_PIO_GATE_TO_EDGE_US + 1u);
        if (s->end == SHOT_END_CAM_TIMEOUT || s->end == SHOT_END_ENGINE ||
            s->end == SHOT_END_ABORTED)
            printf("           strobe gate: %s\n", strobe_gate_seen_text(s->gate));
    }
    if (s->end == SHOT_END_CAM_TIMEOUT)
        printf("fault    : CAM_TIMEOUT latched -- 'fault clear' before the next shot\n");
    if (s->end == SHOT_END_ENGINE)
        printf("fault    : INTERNAL latched -- an engine did something its program cannot.\n"
               "           Save this output; 'cam' shows the engines' state.\n");
}

static void cam_status(void) {
    camera_info_t i;
    camera_info(&i);
    printf("engine   : PIO%u SM%u, program %u..%u (%u instructions), %s, PC %u (%s)\n"
           "           TX %u RX %u, clock %lu Hz (one loop = %.1f ns)\n",
           i.pio_index, i.sm, i.offset, i.offset + i.length - 1u, i.length,
           i.sm_enabled ? "RUNNING" : "stopped", i.pc, camera_pc_label(i.pc),
           i.tx_level, i.rx_level, (unsigned long)i.sys_hz, (double)((double)CAM_PIO_LOOP_CYCLES * 1.0e9 / (double)i.sys_hz));
    printf("pins     : D_Cam_Trigger(10) on %s, pad %d, SIO out %d\n"
           "           Cam_Strobe_0(8) %d   Cam_Strobe_1(9) %d\n",
           i.trigger_pio ? "PIO1" : "SIO", i.trigger_pad, i.trigger_out_sio,
           i.strobe0_pad, i.strobe1_pad);
    strobe_info_t s;
    strobe_info(&s);
    printf("gate     : strobe engine PC %u (gate %u, burst %u..%u)%s; FIRE flag (PIO0 irq %u) %d\n",
           s.sm_pc, s.gate_offset, s.pio_offset, s.pio_offset + 11u,
           s.gated ? ", a camera-gated burst ARMED" : "", (unsigned)PIO_IRQ_CAM_FIRE, s.fire_flag);
    printf("sequencer: %s\n", shot_state_name(shot_state()));
    printf("limits   : timeout %u us (%u-%u), first delay <= %u us, hold <= %u us\n",
           (unsigned)CAM_TIMEOUT_US, (unsigned)CAM_TIMEOUT_MIN_US, (unsigned)CAM_TIMEOUT_MAX_US,
           (unsigned)CAM_FIRST_DELAY_MAX_US, (unsigned)CAM_HOLD_MAX_US);
    printf("J4       : NO CAMERA (CR-09). 7a: J4.3 jumpered to J4.7 and J4.8.\n"
           "           7b: a 3.3 V generator on J4.7/J4.8, triggered from J4.3.\n");
    shot_result_t r;
    if (shot_last(&r)) print_shot(&r);
    else               printf("shot     : none since boot\n");
}

// Start a test shot, yield until it is logged, print it.
static void run_test_shot(const shot_test_req_t *q) {
    shot_result_t r;
    if (!shot_test_start(q, &r)) { print_shot(&r); return; }
    // The shot's own deadlines end it; this bound only keeps the CLI from hanging.
    const uint32_t hold = q->strobe ? strobe_burst_duration_us(&q->burst) + CAM_HOLD_MARGIN_US
                                    : q->hold_us;
    const uint64_t limit = time_us_64() + (uint64_t)q->timeout_us + hold
                         + 3u * CAM_ENGINE_MARGIN_US + 1000000u;
    while (shot_busy() && time_us_64() < limit) (void)pitrac_yield_ms(1);
    if (shot_busy()) {
        printf("*** the shot did not end within %lu us: 'cam' shows where it is ***\n",
               (unsigned long)(limit - r.cam.t_start_us));
        return;
    }
    if (shot_last(&r)) print_shot(&r);
}

static void cmd_cam(int argc, char **argv) {
    if (argc < 2) { cam_status(); return; }
    const char *sub = argv[1];
    shot_test_req_t q;
    memset(&q, 0, sizeof q);
    q.first_delay_us = CAM_SHOT_FIRST_DELAY_US;
    q.timeout_us     = CAM_TIMEOUT_US;

    if (!strcmp(sub, "shot")) {
        float v = CAM_SHOT_SPEED_MPS;
        if (argc > 5 ||
            (argc > 2 && !parse_u32_in(argv[2], 0u, CAM_FIRST_DELAY_MAX_US, &q.first_delay_us)) ||
            (argc > 3 && !parse_float_in(argv[3], STROBE_V_MIN_MPS, STROBE_V_MAX_MPS, &v)) ||
            (argc > 4 && !parse_u32_in(argv[4], CAM_TIMEOUT_MIN_US, CAM_TIMEOUT_MAX_US, &q.timeout_us))) {
            printf("usage: cam shot [first_us 0-%u] [m/s %.0f-%.0f] [timeout_us %u-%u]   (NOTHING FIRED)\n",
                   (unsigned)CAM_FIRST_DELAY_MAX_US, (double)STROBE_V_MIN_MPS,
                   (double)STROBE_V_MAX_MPS, (unsigned)CAM_TIMEOUT_MIN_US, (unsigned)CAM_TIMEOUT_MAX_US);
            return;
        }
        strobe_schedule_t sc;
        if (strobe_compute_schedule(v, &sc) != STROBE_PLAN_OK) {
            printf("ERR: no burst within the charge limit at %.1f m/s -- NOTHING FIRED\n", (double)v);
            return;
        }
        q.strobe = true;
        q.burst.width_us = sc.width_us;
        q.burst.gap_us   = sc.gap_us;
        q.burst.count    = sc.count;
        printf("cam shot : first delay %lu us, timeout %lu us; DRY burst = S15 at %.1f m/s:\n"
               "           %lu x %lu us, every %lu us. J4: NO CAMERA (CR-09).\n",
               (unsigned long)q.first_delay_us, (unsigned long)q.timeout_us, (double)v,
               (unsigned long)sc.count, (unsigned long)sc.width_us, (unsigned long)sc.period_us);
        run_test_shot(&q);
        return;
    }
    if (!strcmp(sub, "trig")) {
        if (argc > 4 ||
            (argc > 2 && !parse_u32_in(argv[2], 0u, CAM_FIRST_DELAY_MAX_US, &q.first_delay_us)) ||
            (argc > 3 && !parse_u32_in(argv[3], CAM_TIMEOUT_MIN_US, CAM_TIMEOUT_MAX_US, &q.timeout_us))) {
            printf("usage: cam trig [first_us 0-%u] [timeout_us %u-%u]   (NOTHING DRIVEN)\n",
                   (unsigned)CAM_FIRST_DELAY_MAX_US, (unsigned)CAM_TIMEOUT_MIN_US,
                   (unsigned)CAM_TIMEOUT_MAX_US);
            return;
        }
        q.strobe  = false;
        q.hold_us = CAM_TRIG_HOLD_US;
        printf("cam trig : the handshake alone: first delay %lu us, timeout %lu us, hold %u us.\n"
               "           No burst. J4: NO CAMERA (CR-09).\n",
               (unsigned long)q.first_delay_us, (unsigned long)q.timeout_us,
               (unsigned)CAM_TRIG_HOLD_US);
        run_test_shot(&q);
        return;
    }
    printf("ERR: '%s' is not a cam subcommand -- NOTHING WAS DONE.\n"
           "     cam | cam shot [first_us] [m/s] [timeout_us] | cam trig [first_us] [timeout_us]\n",
           sub);
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
            if (strobe_live_watchdog_required()) {
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
           "  exit -- unless it was already armed, or you type 'wdog on' while live.\n"
           "  Not with a Pi powered (image 1.8): there the reset would cut the Pi's\n"
           "  power, and U5 and the PIO already bound every pulse and burst.\n");
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

// ---------------------------------------------------------------------------
// Phase 8.6 halt telemetry (telemetry.h). The RP2354 is the witness that knows
// whether IT asked the Pi to shut down, and it outlives what the Pi's own log
// may not.
// ---------------------------------------------------------------------------
static void cmd_link(int argc, char **argv) {
    if (argc != 1) { printf("usage: link   (read-only)\n"); return; }
    pi_link_stat_t s;
    pi_link_status(&s);
    printf("lines    : %s\n", s.connected
           ? "CONNECTED to UART1: a Pi is up (latch closed, its 3V3 up, RPI5_ON high)"
           : "released: no Pi up, so nothing is driven into J8.10 (connected only with\n"
             "           the latch closed, the Pi's 3V3 up and RPI5_ON high)");
    printf("uart     : %s, %lu baud 8N1 (asked %u)\n",
           s.ready ? "ready" : "NOT READY: no free DMA channel for the receive ring",
           (unsigned long)s.baud, (unsigned)LINK_BAUD);
    printf("traffic  : rx %lu B, tx %lu B, connected %lu time(s) this boot\n",
           (unsigned long)s.rx_bytes, (unsigned long)s.tx_bytes, (unsigned long)s.connects);
    printf("frames   : %lu good, %lu CRC errors, %lu cut short, %lu junk bytes\n",
           (unsigned long)s.frames, (unsigned long)s.crc_errors, (unsigned long)s.gap_drops,
           (unsigned long)s.junk);
    if (s.requests)
        printf("requests : %lu answered (%lu of an unknown type); last 0x%02x, %lu ms ago\n",
               (unsigned long)s.requests, (unsigned long)s.unknown, (unsigned)s.last_type,
               (unsigned long)(to_ms_since_boot(get_absolute_time()) - s.last_ms));
    else
        printf("requests : none this boot\n");
}

static void cmd_upd(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "abort")) {
        fwu_abort();
        printf("staged update discarded (RAM). An install that had begun stops with no new\n"
               "  image in the partner partition: sector 0 goes last. Any pending power-on\n"
               "  for the Pi is cancelled: 'on' powers it.\n");
        return;
    }
    if (argc != 1) { printf("usage: upd | upd abort   (NOTHING WAS CHANGED)\n"); return; }

    const fwu_layout_t *L = fwu_port_layout();
    fwu_status_t s;
    fwu_status(&s);

    printf("version  : ");
    print_version(PITRAC_FW_VERSION);
    printf("   built %s\n", cli_build_stamp());
    printf("boot     : %s boot%s\n", fw_layout_boot_type_name(L->boot_type),
           !s.trial ? "" : s.bought ? ", ON TRIAL: bought itself at the end of init"
                                    : ", ON TRIAL: NOT bought");
    if (L->partitioned) {
        const char me = L->run_start < L->other_start ? 'A' : 'B';
        printf("layout   : this image in partition %c, 0x%06lx-0x%06lx\n", me,
               (unsigned long)L->run_start, (unsigned long)(L->run_start + L->run_size - 1u));
        printf("           partner %c 0x%06lx-0x%06lx; config 0x%06lx-0x%06lx\n",
               me == 'A' ? 'B' : 'A', (unsigned long)L->other_start,
               (unsigned long)(L->other_start + L->other_size - 1u),
               (unsigned long)L->cfg_start, (unsigned long)(L->cfg_start + L->cfg_size - 1u));
        fwu_image_t img;
        const uint8_t *p = fwu_port_read(L->other_start);
        // Only a first sector of all 0xFF or all 0x00 is known to hold nothing the
        // boot ROM could start a block from. That this firmware's parser reads no
        // IMAGE_DEF there is no proof of none (fw_update.h, review R4-F1).
        unsigned ff = 0u, zero = 0u;
        for (uint32_t i = 0; p && i < FWU_SECTOR; i++) {
            ff += p[i] == 0xFFu;
            zero += p[i] == 0x00u;
        }
        if (p && fwu_parse_image(p, FWU_SECTOR, &img)) {
            printf("partner  : version ");
            print_version(img.version);
            printf("%s%s\n", img.tbyb ? ", NOT bought (a trial that failed, or never ran)"
                                      : ", bought: the fallback",
                   img.exe_arm_s_rp2350 ? "" : ", NOT an RP2350 Arm image");
        } else if (ff == FWU_SECTOR) {
            printf("partner  : erased: no image\n");
        } else if (zero == FWU_SECTOR) {
            printf("partner  : cleared to zeros: no image\n");
        } else {
            printf("partner  : not erased, and no IMAGE_DEF this firmware can read: UNKNOWN\n");
        }
    } else {
        printf("layout   : %s.\n"
               "           Updates from the Pi need the partition table, installed once\n"
               "           over USB (BENCH_P8_PI.md 8.7).\n", L->why_not);
    }

    printf("staged   : %s", fwu_state_name(s.state));
    switch (s.state) {
    case FWU_RECEIVING:
        printf(", %lu of %lu B", (unsigned long)s.received, (unsigned long)s.size);
        break;
    case FWU_VERIFIED:
        printf(": version ");
        print_version(s.staged_version);
        printf(", %lu B, SHA-256 checked; UPD_APPLY from the Pi installs it", (unsigned long)s.size);
        break;
    case FWU_ARMED:
        printf(": waiting for the Pi to be powered down (%lu s so far)",
               (unsigned long)(s.armed_for_ms / 1000u));
        break;
    case FWU_INSTALLING:
        printf(": step %lu of %lu", (unsigned long)s.install_step, (unsigned long)s.install_steps);
        break;
    default:
        break;
    }
    printf("\n");
    if (s.last_error != FWU_OK) printf("last err : %s\n", fwu_err_name(s.last_error));
    if (s.result != FWU_RESULT_NONE) {
        printf("update   : %s, ", fwu_result_name(s.result));
        print_version(s.result_from);
        printf(" -> ");
        print_version(s.result_to);
        printf("%s\n", s.result_this_boot ? " (decided at this boot)" : " (an earlier boot)");
        printf("           %s\n", fwu_detail_name(s.result, s.result_detail));
        if (s.result == FWU_RESULT_INSTALL_FAILED && s.result_detail == FWU_E_CLEANUP)
            printf("WARNING  : the partner may hold an image a reset would launch WITHOUT a\n"
                   "           trial. Reflash over USB first: 'bootsel' launches no image.\n"
                   "           No other reset or power cycle until then.\n");
    }
    if (s.repower_pending)
        printf("pending  : the Pi is powered again ~1 s into STANDBY ('upd abort' cancels)\n");
}

static void cmd_hist(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "clear") && !strcmp(argv[2], "confirm")) {
        telemetry_clear();
        printf("halt telemetry cleared in RAM. Flash follows after %u s in STANDBY, or\n"
               "  with 'cfg save'.\n", (unsigned)(TELEMETRY_SAVE_QUIET_MS / 1000u));
        return;
    }
    if (argc != 1) {
        printf("usage: hist | hist clear confirm   (NOTHING WAS CHANGED)\n");
        return;
    }

    printf("halt telemetry (BENCH_P8_PI.md 8.6), counted by the RP2354:\n");
    for (int k = 0; k < (int)TELE__COUNTERS; k++)
        printf("  %-38s %10lu\n", telemetry_counter_name((tele_counter_t)k),
               (unsigned long)telemetry_get((tele_counter_t)k));
    uint32_t req  = telemetry_get(TELE_PI_SHUTDOWN_REQ);
    uint32_t done = telemetry_get(TELE_PI_SHUTDOWN_CLEAN) + telemetry_get(TELE_PI_SHUTDOWN_TIMEOUT);
    if (req > done)
        printf("  %-38s %10lu\n", "  ...neither: cut short, or in progress",
               (unsigned long)(req - done));

    if (telemetry_get(TELE_FAULTS)) {
        uint32_t boot = telemetry_word(TELE_W_LAST_FAULT_BOOT);
        printf("last fault: %s, %lu s into boot %lu%s\n",
               fault_name((fault_t)telemetry_word(TELE_W_LAST_FAULT)),
               (unsigned long)telemetry_word(TELE_W_LAST_FAULT_UPTIME), (unsigned long)boot,
               boot == telemetry_get(TELE_BOOTS) ? " (this one)" : "");
    }
    printf("this boot : %s\n",
           telemetry_boot_name((tele_boot_t)telemetry_word(TELE_W_BOOT_REASON)));
    printf("loaded    : %s\n", telemetry_source_name(telemetry_source()));

    cfg_telemetry_stat_t t;
    cfg_telemetry_status(&t);
    if (!telemetry_dirty())
        printf("flash     : up to date -- %s\n", cfg_source());
    else if (t.gave_up)
        printf("flash     : *** NOT SAVED: %lu automatic writes did not verify this boot, so\n"
               "            they have stopped. 'cfg save' still tries. ***\n",
               (unsigned long)t.failures);
    else if (t.retry_wait)
        printf("flash     : *** the last write did not verify (%lu this boot); retry in %lu s ***\n",
               (unsigned long)t.failures, (unsigned long)(t.retry_in_ms / 1000u));
    else
        printf("flash     : not saved yet -- written after %u s in STANDBY%s%s\n",
               (unsigned)(TELEMETRY_SAVE_QUIET_MS / 1000u),
               power_fsm_state() == PS_STANDBY ? "" : ", now ",
               power_fsm_state() == PS_STANDBY ? "" : power_state_name(power_fsm_state()));
    printf("RAM keeps the counts through a reset (unless a BOOTSEL session overwrote it:\n"
           "then flash); flash keeps them through a power cycle.\n");
}

// The Pi-down counters and the flash failures as last announced: each new one
// is announced once, where the operator is looking (like live_exit_notice()).
static uint32_t s_down_shown, s_up_shown, s_tele_fail_shown;

static void background_notices(void) {
    uint32_t down = telemetry_get(TELE_PI_DOWN_UNREQ), up = telemetry_get(TELE_PI_BACK_UP);
    if (down > s_down_shown)
        printf("*** THE PI WENT DOWN WITHOUT A REQUEST from the board. Counted in 'hist'.\n"
               "    The rail stays up: 'off' or the button for an orderly end. ***\n");
    if (up > s_up_shown)
        printf("*** The Pi read up again after going down unrequested: a Pi reboot.\n"
               "    Counted in 'hist'. ***\n");
    s_down_shown = down;     // also resyncs after 'hist clear confirm'
    s_up_shown   = up;

    cfg_telemetry_stat_t t;
    cfg_telemetry_status(&t);
    if (t.failures != s_tele_fail_shown) {
        s_tele_fail_shown = t.failures;
        if (t.gave_up)
            printf("*** Halt telemetry: %lu flash writes did not verify this boot; the\n"
                   "    automatic writes have stopped. The counts are safe in RAM until a\n"
                   "    power cycle; 'cfg save' still tries. ***\n", (unsigned long)t.failures);
        else
            printf("*** Halt telemetry: the flash write did not verify (%lu this boot). The\n"
                   "    counts are safe in RAM; it retries in %lu s in STANDBY. ***\n",
                   (unsigned long)t.failures, (unsigned long)(TELEMETRY_SAVE_RETRY_MS / 1000u));
    }
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

// `reset` and `bootsel`, in this order (image 1.11, review finding V110-F1):
//   1. the guard;
//   2. the message, and a 50 ms bare sleep so USB carries it out;
//   3. the guard AGAIN (reset_still_ok()), immediately before the commit;
//   4. the watchdog disarmed, and the reboot.
// Until 1.10 the watchdog was disarmed first and the guard ran only at 1: a Pi
// whose 3V3 rose during the 50 ms was cut. Now a refusal at 3 leaves the
// watchdog as it was. An armed watchdog (1 s, fed by the superloop pass just
// before this command) cannot expire in the 50 ms.
//
// Bare sleep on purpose, not pitrac_yield_ms(): with the FSM frozen, nothing
// can close the latch during the wait (a button press would, through a yield),
// so the only input that can change before step 3 is the Pi's 3V3.
//
// What remains after step 3: for `reset`, the instant between that read and
// watchdog_reboot(0, 0, 0)'s trigger (delay 0: immediate). For `bootsel`, also
// the boot ROM's own 10 ms inside reset_usb_boot() (SDK 2.3.0 on RP2350:
// rom_reboot(BOOTSEL, 10 ms)), which nothing can check after it is called.
static void cmd_reset(int argc, char **argv) {

        if (!reset_guard_ok(argc, argv, "reset")) return;

        printf("resetting...\n"); sleep_ms(50);
        if (!reset_still_ok(argc, argv, "reset")) return;

        // Same reasoning as `bootsel`: watchdog_reboot() and an armed watchdog
        // both drive the same hardware. Disarm so the reboot we asked for is
        // the one that happens -- after the last check, so a refusal keeps it.
        pitrac_watchdog_enable(false);
        watchdog_reboot(0, 0, 0);

}

static void cmd_bootsel(int argc, char **argv) {

        if (!reset_guard_ok(argc, argv, "bootsel")) return;

        printf("rebooting to BOOTSEL...\n"); sleep_ms(50);
        if (!reset_still_ok(argc, argv, "bootsel")) return;

        // 🔴 DISARM THE WATCHDOG BEFORE reset_usb_boot(), or this command does
        // not work. (After the last check, so a refusal keeps it: image 1.11.)
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
    { "hist", NULL, cmd_hist, "halt telemetry: boots, Pi shutdowns, power cuts (8.6)" },
    { "link", NULL, cmd_link, "the Pi serial link: lines and counters" },
    { "upd", NULL, cmd_upd, "version, A/B partitions, updates from the Pi" },
    { "capture", NULL, cmd_capture_cmd, "block capture -> CSV; 'capture trig' waits for a sound" },
    { "gpio", NULL, cmd_gpio_cmd, "read / drive a pin" },
    { "adc", NULL, cmd_adc, "oversampled read; 'adc ringtest' times the BURST switch" },
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
    { "cam", NULL, cmd_cam, "camera handshake: status, a test shot, the handshake alone (Phase 7)" },
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
// and it ends live mode on the way. `hist` only reads, or clears RAM.
static bool allowed_while_live(const char *name) {
    static const char *const k_ok[] = {
        "help", "?", "id", "stat", "pins", "hist", "adc5v", "fault", "off", "forceoff",
        "wdog", "strobe",
    };
    for (size_t i = 0; i < count_of(k_ok); i++)
        if (!strcmp(name, k_ok[i])) return true;
    return false;
}

static void dispatch_command(int argc, char **argv);

static void dispatch(int argc, char **argv) {
    // Clear the abort latch for EVERY command, not just the long ones. A key
    // pressed to stop the previous command must not carry over and kill this
    // one -- and it also snapshots the current fault, so a command run while a
    // fault is already latched is not aborted by that pre-existing fault.
    pitrac_abort_clear();

    // Live mode can end with no command running to say so -- the idle timeout,
    // a hold condition, the power FSM taking the rail down -- or inside a
    // command that does not print it. Announce any unacknowledged exit before
    // this command runs and again after it: on 2026-10-12 an unannounced idle
    // exit turned the operator's next `strobe pulse` into a dry one.
    live_exit_notice();
    background_notices();
    dispatch_command(argc, argv);
    live_exit_notice();
    background_notices();
}

static void dispatch_command(int argc, char **argv) {
    if (argc == 0) return;
    const char *c = argv[0];

    if (strobe_live_armed() && !allowed_while_live(c)) {
        printf("REFUSED: '%s' is not allowed while live strobe mode is armed.\n"
               "  Allowed: help id stat pins hist adc5v fault off forceoff wdog strobe.\n"
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
    // Announce what happens from now on, not the history ('hist' has that).
    s_down_shown = telemetry_get(TELE_PI_DOWN_UNREQ);
    s_up_shown   = telemetry_get(TELE_PI_BACK_UP);
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
