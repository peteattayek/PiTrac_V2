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
#include <math.h>

#define CLI_MAX_LINE 96
#define CLI_MAX_ARGS 8

// Fitted by `cal model`; consumed by `scan carrier`. RAM only until the flash
// config block lands -- re-run `cal model` after a reset.
static cal_phase_model_t s_phase_model;

static char   s_line[CLI_MAX_LINE];
static size_t s_len;

// ---------------------------------------------------------------------------

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
    "                             e.g. 'capture 0x20 2000 250000' = ADC5 @ 250 ksps\n"
    "  adcmode off|idle|armed|burst\n"
    "\n"
    "  on                         request power on (obeys the USB-power guard)\n"
    "  off                        request orderly shutdown\n"
    "  forceoff                   drop the latch immediately\n"
    "  pisim                      show the simulated-Pi input levels\n"
    "\n"
    "  gpio <n>                   read a pin\n"
    "  gpio <n> <0|1>             drive a pin (outputs only; guarded)\n"
    "  led r|y <0|1>              on-board status LEDs (+3V3, work in standby)\n"
    "  panel pwr|rdy <0-100|auto> panel LEDs on J7 (need the +5V rail latched)\n"
    "  panel test                 ramp both LEDs 0->100->0 for a current check\n"
    "  panel pattern <p|auto>     force a ring pattern; p = off powering booting\n"
    "                             running shutdown fault\n"
    "  panel demo                 play every pattern in turn, ~4 s each\n"
    "\n"
    "\n"
    "  -- Phase 2: beam (needs the +5V rail up) --\n"
    "  beam                       show carrier/demod state\n"
    "  beam on | off\n"
    "  beam freq <hz>             default 104167 (TOP=1439, exact 30%% at level 432)\n"
    "  beam duty <pct>            IMMEDIATE. use 'beam ramp' above ~5%%\n"
    "  beam ramp <pct> [step_ms]  gradual, 1%% steps â€” watch temps as it climbs\n"
    "  beam phase <ticks>         demod offset, 0..TOP (1 tick = 6.67 ns = 0.25 deg)\n"
    "  beam clamp                 1 kHz / 50%% â€” scope TP5 to measure the U9 clamp (Q1)\n"
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
    "  hpf test [ms]              establish the GPIO33 polarity EMPIRICALLY.\n"
    "                             *** RUN THIS FIRST -- the sense is unverified ***\n"
    "  detect                     comparator + PIO transit timer status\n"
    "  detect arm | disarm        start/stop the PIO edge timer (resets counters)\n"
    "  detect coalesce <us>       chatter-merge window (U15 has no hysteresis)\n"
    "  detect path <mm>           beam width; no velocity is reported until set\n"
    "  detect cond <0|1|2>        tag passes: 0 nominal 1 far 2 low-reflectance\n"
    "  detect log | stats | clear per-pass CSV / the Phase 4 table / reset\n"
    "  detect wave [seq]          dump a retained waveform as CSV\n"
    "\n"
    "  fault                      show / 'fault clear' (also acks FAULT -> STANDBY)\n"
    "  reset [force]              soft reset. REFUSED while a Pi is powered:\n"
    "                             a reset drops the latch = hard power cut\n"
    "  bootsel [force]            reboot to USB mass storage (same guard)\n";

// ---------------------------------------------------------------------------

static void cmd_id(void) {
    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);
    printf("fw       : pitrac phase0/1/1b\n");
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

static void cmd_capture(int argc, char **argv) {
    if (argc < 4) { printf("usage: capture <mask> <n> <rate_hz>\n"); return; }
    uint     mask = (uint)strtoul(argv[1], NULL, 0);
    size_t   n    = (size_t)strtoul(argv[2], NULL, 0);
    uint32_t rate = (uint32_t)strtoul(argv[3], NULL, 0);

    if ((mask & ~ADC_VALID_MASK) != 0) {
        printf("ERR: mask 0x%02x includes a non-analog channel.\n", mask);
        printf("     valid mask is 0x%02x (ch 0,1,2,5,7).\n", ADC_VALID_MASK);
        printf("     ch3=GPIO43 RPI5_SHUTDOWN and ch4=GPIO44 Threshold_PWM are\n");
        printf("     digital outputs on this board â€” sampling them is a bug.\n");
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

    for (size_t i = 0; i < got; i += nch) {
        for (uint k = 0; k < nch; k++) printf(k ? ",%u" : "%u", b[i + k]);
        printf("\n");
        // The CDC pipe is slower than we can print. Yield occasionally so USB
        // keeps servicing and we don't drop the tail of a long dump.
        if ((i % 256) == 0) tight_loop_contents();
    }
    printf("# end\n");

    if (adc_capture_overran())
        printf("WARN: FIFO overran â€” round-robin phase lost, samples are mislabeled.\n");
}

// Pins that the CLI is allowed to drive. PIN_PULSE_LIMIT_DIS is deliberately
// absent: it defeats the strobe hardware watchdog and there must be no path to
// it until Phase 6d. PIN_LATCH_CONTROL is absent too â€” go through the FSM so
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
    uint p = (uint)strtoul(argv[1], NULL, 0);
    if (p > 47) { printf("ERR: pin out of range\n"); return; }

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
    gpio_put(p, strtoul(argv[2], NULL, 0) ? 1 : 0);
    printf("gpio%u <- %d\n", p, gpio_get_out_level(p));
}

// ---------------------------------------------------------------------------

static void dispatch(int argc, char **argv) {
    if (argc == 0) return;
    const char *c = argv[0];

    if (!strcmp(c, "help") || !strcmp(c, "?"))      { printf("%s", k_help); }
    else if (!strcmp(c, "id"))                       { cmd_id(); }
    else if (!strcmp(c, "stat"))                     { cmd_stat(); }
    else if (!strcmp(c, "pins"))                     { cmd_pins(); }
    else if (!strcmp(c, "capture"))                  { cmd_capture(argc, argv); }
    else if (!strcmp(c, "gpio"))                     { cmd_gpio(argc, argv); }

    else if (!strcmp(c, "adc")) {
        if (argc < 2) { printf("usage: adc <ch> [n]\n"); return; }
        uint ch = (uint)strtoul(argv[1], NULL, 0);
        uint n  = (argc > 2) ? (uint)strtoul(argv[2], NULL, 0) : 64;
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
    else if (!strcmp(c, "adc5v")) {
        float v = adc_read_5vin_volts();
        printf("+5V_IN = %.3f V  (latch guard %.2f V -> %s)\n",
               (double)v, (double)V5_MIN_FOR_LATCH,
               v < V5_MIN_FOR_LATCH ? "USB-only, latch INHIBITED"
                                    : "external supply, latch permitted");
        printf("         measured refs: USB-only 4.85 V, bench/Meanwell 5.20 V\n");
    }
    else if (!strcmp(c, "adc5vcal")) {
        if (argc < 2) { printf("usage: adc5vcal <volts measured with a DMM at +5V_IN>\n"); return; }
        float meas = strtof(argv[1], NULL);
        float raw  = adc_read_volts(ADC_CH_5VIN, 256) * V5IN_DIVIDER;
        if (raw > 0.1f && meas > 0.1f) {
            adc_set_5vin_scale(meas / raw);
            printf("scale <- %.4f  (raw %.3f V, measured %.3f V)\n",
                   (double)adc_get_5vin_scale(), (double)raw, (double)meas);
            printf("NOTE: RAM only â€” not persisted to flash yet.\n");
        } else printf("ERR: implausible values\n");
    }
    else if (!strcmp(c, "adcmode")) {
        if (argc < 2) { printf("usage: adcmode off|idle|armed|burst\n"); return; }
        if      (!strcmp(argv[1], "off"))   adc_engine_set_mode(ADC_MODE_OFF);
        else if (!strcmp(argv[1], "idle"))  adc_engine_set_mode(ADC_MODE_IDLE);
        else if (!strcmp(argv[1], "armed")) adc_engine_set_mode(ADC_MODE_ARMED);
        else if (!strcmp(argv[1], "burst")) adc_engine_set_mode(ADC_MODE_BURST);
        else { printf("ERR: unknown mode\n"); return; }
        printf("adcmode = %d\n", (int)adc_engine_mode());
    }

    else if (!strcmp(c, "on"))       { power_request_on();        printf("requested on\n"); }
    else if (!strcmp(c, "off"))      { power_request_shutdown();  printf("requested shutdown\n"); }
    else if (!strcmp(c, "forceoff")) { power_request_force_off(); printf("FORCE OFF\n"); }

    else if (!strcmp(c, "pisim")) {
        printf("PI_3V3_SENSE(24) = %d   <- feed 3.3V into the top of R45 to assert\n",
               gpio_get(PIN_PI_3V3_SENSE));
        printf("RPI5_ON(0)       = %d   <- jumper J8.15 to J8.1 (+3V3) to assert\n",
               gpio_get(PIN_RPI5_ON));
        printf("RPI5_SHUTDOWN(43)= %d   -> scope J8.37 (%s)\n",
               gpio_get_out_level(PIN_RPI5_SHUTDOWN),
               PI_SHUTDOWN_ACTIVE_LOW ? "ACTIVE LOW" : "ACTIVE HIGH");
        printf("pi_is_down       = %d\n", power_pi_is_down());
    }

    else if (!strcmp(c, "panel")) {
        if (argc < 2) {
            printf("usage: panel pwr|rdy <0-100|auto>\n"
                   "       panel pattern <off|powering|booting|running|shutdown|fault|auto>\n"
                   "       panel test | panel demo\n");
            return;
        }

        if (!power_rails_ready())
            printf("NOTE: +5V rail is open (state %s) â€” the panel LEDs are fed from the\n"
                   "      SWITCHED rail and will stay dark. Use 'on' first.\n",
                   power_state_name(power_fsm_state()));

        // --- ramp, for a current / thermal check ---------------------------
        if (!strcmp(argv[1], "test")) {
            printf("ramping both panel LEDs 0->100->0 ...\n");
            for (int p = 0; p <= 100; p += 5) { panel_override(p, p); sleep_ms(80); }
            for (int p = 100; p >= 0; p -= 5) { panel_override(p, p); sleep_ms(80); }
            panel_override(-1, -1);
            printf("done â€” both returned to automatic\n");
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
                       p == PANEL_PAT_OFF      ? "(dark â€” what STANDBY looks like)"      :
                       p == PANEL_PAT_POWERING ? "(fast breath â€” rails coming up)"       :
                       p == PANEL_PAT_BOOTING  ? "(slow breath â€” waiting on the Pi)"     :
                       p == PANEL_PAT_RUNNING  ? "(solid â€” ready)"                       :
                       p == PANEL_PAT_SHUTDOWN ? "(fast blink â€” teardown)"               :
                                                 "(double-blink â€” fault)");
                panel_force_pattern(p);
                for (int i = 0; i < 400; i++) { panel_update(); sleep_ms(10); }
            }
            panel_force_pattern(-1);
            printf("done â€” ring returned to automatic (now: %s)\n",
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
        if (argc < 3) { printf("usage: panel pwr|rdy <0-100|auto>\n"); return; }
        bool is_pwr = (argv[1][0] == 'p');
        int  pct    = !strcmp(argv[2], "auto") ? -1 : (int)strtol(argv[2], NULL, 0);
        panel_override(is_pwr ? pct : -2, is_pwr ? -2 : pct);
        printf("%s <- %s\n", is_pwr ? "PWR_LED(J7.2)" : "RDY_LED(J7.6)",
               pct < 0 ? "auto" : argv[2]);
    }

    else if (!strcmp(c, "threshold")) {
        if (argc >= 3 && !strcmp(argv[1], "duty")) {
            detect_threshold_set_duty(strtof(argv[2], NULL) / 100.0f);
        } else if (argc >= 3 && !strcmp(argv[1], "volts")) {
            detect_threshold_set_volts(strtof(argv[2], NULL));
        } else if (argc >= 2 && !strcmp(argv[1], "sweep")) {
            float lo   = (argc > 2) ? strtof(argv[2], NULL) / 100.0f : 0.0f;
            float hi   = (argc > 3) ? strtof(argv[3], NULL) / 100.0f : 1.0f;
            uint16_t n = (argc > 4) ? (uint16_t)strtoul(argv[4], NULL, 0) : 64u;
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
        printf("           vref %.3f V (nominal 3.3; measured +3V3 was 3.246 -- TP8 is the truth)\n",
               (double)detect_threshold_vref());
        printf("D_Comparator(46) = %d   (%s -- active HIGH, R103 10K pull-up)\n",
               gpio_get(PIN_D_COMPARATOR),
               detect_comparator() ? "ABOVE threshold" : "below threshold");
        printf("settle %u ms per change (dominant pole 2.62 ms; the 10 ms in the .md is ~4 tau)\n",
               (unsigned)DAC_SETTLE_MS);
    }

    else if (!strcmp(c, "cfg")) {
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
            printf("phase mdl: %s\n", k->phase_f_hi
                   ? (k->phase_pure_delay ? "fitted, pure delay" : "fitted, dispersive")
                   : "not fitted");
            const char *why = cfg_save_blocked_reason();
            printf("save     : %s\n", why ? why : "permitted now");
        }
    }

    else if (!strcmp(c, "cal")) {
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
            printf("sweeping demod phase, static reflector required, ~26 s ...\n");
            bool ok = cal_demod_phase(beam_actual_freq_hz(), CAL_PHASE_POINTS,
                                      CAL_CHOP_CYCLES, CAL_CHOP_HZ_DEFAULT, &r);
            adc_engine_set_mode(ADC_MODE_IDLE);
            printf("\nfit      : %ld ticks   (grid argmax %ld)\n",
                   (long)r.best_ticks, (long)r.argmax_ticks);
            printf("amplitude: %.1f codes%s\n", (double)r.amplitude,
                   (r.amplitude > ADC_FULL_SCALE)
                       ? "   *** ABOVE FULL SCALE -- impossible for a real signal ***" : "");
            printf("saturated: %.0f %% of sweep points against the rail  (limit %.0f %%)%s\n",
                   (double)(r.sat_frac * 100.0f), (double)(CAL_SAT_MAX * 100.0f),
                   (r.sat_frac >= CAL_SAT_MAX) ? "  *** FAIL ***" : "");
            printf("quad null: %.1f codes  (want |null| < %.0f %% of amplitude = %.1f)%s\n",
                   (double)r.quad_null, (double)(CAL_QUAD_NULL_MAX * 100.0f),
                   (double)(CAL_QUAD_NULL_MAX * r.amplitude),
                   (fabsf(r.quad_null) >= CAL_QUAD_NULL_MAX * r.amplitude) ? "  *** FAIL ***" : "");
            printf("h2/h1    : %.3f  (asymmetric distortion, limit %.2f)%s\n",
                   (double)r.h2_ratio, (double)CAL_H2_MAX,
                   (r.h2_ratio >= CAL_H2_MAX) ? "  *** FAIL ***" : "");
            printf("h3/h1    : %.3f  (SYMMETRIC CLIPPING, limit %.2f; square wave = 0.333)%s\n",
                   (double)r.h3_ratio, (double)CAL_H3_MAX,
                   (r.h3_ratio >= CAL_H3_MAX) ? "  *** FAIL ***" : "");
            printf("warm     : %s\n", r.warm ? "yes" : "NO -- see above");
            cfg_mut()->cal_warm = r.warm ? 1u : 0u;
            if (ok) {
                beam_set_phase(r.best_ticks);
                printf("phase <- %ld ticks. Record it in PROGRESS.md section 6.\n",
                       (long)r.best_ticks);
            } else {
                printf("NOT COMMITTED -- see the *** FAIL *** lines above.\n");
                if (r.sat_frac >= CAL_SAT_MAX || r.h3_ratio >= CAL_H3_MAX ||
                    r.amplitude > ADC_FULL_SCALE) {
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
            uint32_t f0 = (argc > 2) ? (uint32_t)strtoul(argv[2], NULL, 0) : 80000u;
            uint32_t f1 = (argc > 3) ? (uint32_t)strtoul(argv[3], NULL, 0) : 200000u;
            uint32_t n  = (argc > 4) ? (uint32_t)strtoul(argv[4], NULL, 0) : 5u;
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
            printf("range %lu..%lu Hz, %u points, residual %.4f rad\n",
                   (unsigned long)s_phase_model.f_lo, (unsigned long)s_phase_model.f_hi,
                   s_phase_model.n_points, (double)s_phase_model.resid_rms_rad);
            if (s_phase_model.pure_delay)
                printf("PURE DELAY: no constant term, no curvature. One phase_ticks value\n"
                       "  covers every carrier -- a tick is a fixed 6.67 ns and the offset\n"
                       "  is a fixed time, so it does not scale with frequency.\n");
            else
                printf("NOT a pure delay -- the filter poles contribute. The model is\n"
                       "  required; do NOT hold one tick count across a frequency sweep.\n");
            printf("*** DO NOT EXTRAPOLATE outside %lu..%lu Hz. ***\n",
                   (unsigned long)s_phase_model.f_lo, (unsigned long)s_phase_model.f_hi);
            return;
        }
        if (argc >= 3 && !strcmp(argv[1], "gain")) {
            float peak = strtof(argv[2], NULL);
            float frac = (argc > 3) ? strtof(argv[3], NULL) : 0.67f;
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
            printf("%lu..%lu Hz, residual %.4f rad, %s\n",
                   (unsigned long)s_phase_model.f_lo, (unsigned long)s_phase_model.f_hi,
                   (double)s_phase_model.resid_rms_rad,
                   s_phase_model.pure_delay ? "pure delay" : "dispersive");
    }

    else if (!strcmp(c, "scan")) {
        if (argc < 2 || strcmp(argv[1], "carrier")) {
            printf("usage: scan carrier [f0] [f1] [n] [force]\n"); return;
        }
        uint32_t f0 = (argc > 2) ? (uint32_t)strtoul(argv[2], NULL, 0) : 80000u;
        uint32_t f1 = (argc > 3) ? (uint32_t)strtoul(argv[3], NULL, 0) : 200000u;
        uint32_t n  = (argc > 4) ? (uint32_t)strtoul(argv[4], NULL, 0) : 16u;
        bool force  = (argc > 5) && !strcmp(argv[5], "force");
        if (!beam_enabled()) { printf("REFUSED: beam is off.\n"); return; }
        if (!s_phase_model.valid)
            printf("WARNING: no phase model. Every candidate will be measured at phase 0,\n"
                   "  i.e. at a DIFFERENT phase error each, and the SNR ranking will be\n"
                   "  meaningless. Run 'cal model' first.\n");
        adc_engine_set_mode(ADC_MODE_ARMED);
        cal_scan_point_t best;
        bool ok = cal_scan_carrier(f0, f1, n, &s_phase_model, force, &best);
        adc_engine_set_mode(ADC_MODE_IDLE);
        if (ok) printf("\nPut the winner in board.h and record it in PROGRESS.md section 6.\n");
    }

    else if (!strcmp(c, "detect")) {
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
            detect_set_coalesce_us((uint32_t)strtoul(argv[2], NULL, 0));
        }
        if (argc >= 3 && !strcmp(argv[1], "path")) {
            detect_set_path_mm(strtof(argv[2], NULL));
            printf("beam path <- %.2f mm\n", (double)detect_path_mm());
            return;
        }
        if (argc >= 3 && !strcmp(argv[1], "cond")) {
            detect_set_condition((uint8_t)strtoul(argv[2], NULL, 0));
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
            const detect_wave_t *w = (argc > 2)
                ? detect_wave_for((uint32_t)strtoul(argv[2], NULL, 0))
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

    else if (!strcmp(c, "hpf")) {
        if (argc >= 2 && !strcmp(argv[1], "test")) {
            uint32_t w = (argc > 2) ? (uint32_t)strtoul(argv[2], NULL, 0) : 3000u;
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
                   "  leakage and the baseline walks off (~11 mV/s measured on this board).\n");
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
        printf("  *** polarity is a compiled HYPOTHESIS until 'hpf test' has run ***\n");
    }

    else if (!strcmp(c, "led")) {
        if (argc < 3) { printf("usage: led r|y <0|1>\n"); return; }
        uint p = (argv[1][0] == 'r') ? PIN_LED_RED : PIN_LED_YELLOW;
        gpio_put(p, strtoul(argv[2], NULL, 0) ? 1 : 0);
        printf("ok\n");
    }

    else if (!strcmp(c, "beam")) {
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
            printf("rails    : %s\n", power_rails_ready() ? "up" : "DOWN â€” beam cannot run");
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
                       "      and D11 â€” ~0.95 A average from +5V at 30%%.\n");
            return;
        }

        if (!strcmp(argv[1], "freq")) {
            if (argc < 3) { printf("usage: beam freq <hz>\n"); return; }
            uint32_t f = (uint32_t)strtoul(argv[2], NULL, 0);
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
            float d = strtof(argv[2], NULL) / 100.0f;
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
            float d = strtof(argv[2], NULL) / 100.0f;
            uint32_t ms = (argc > 3) ? (uint32_t)strtoul(argv[3], NULL, 0) : 250;
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
            printf("done â€” duty now %.2f %%\n", (double)(beam_duty() * 100.0f));
            return;
        }

        if (!strcmp(argv[1], "phase")) {
            if (argc < 3) { printf("usage: beam phase <0..%lu>\n", (unsigned long)beam_top()); return; }
            int32_t p = (int32_t)strtol(argv[2], NULL, 0);
            beam_set_phase(p);     // preserves TOP; no round-trip through freq
            printf("phase <- %ld ticks (%.2f deg of one carrier period)\n",
                   (long)beam_phase_ticks(),
                   (double)(360.0f * beam_phase_ticks() / (beam_top() + 1)));
            return;
        }

        // Q1: how wide is the U9 one-shot clamp really? Command a high phase far
        // longer than the clamp and scope TP5 â€” the LED pulse is the answer.
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
                printf("  .md claims 113 us;  0.7*R68*C57 = 0.7*56k*2.2n = ~86 us.\n");
                printf("  Record it -- it sets STROBE_SW_MAX_US for Phase 6.\n");
                printf("WARNING: if the LOW width equals the %lu us commanded above, the\n"
                       "         one-shot is NOT clamping and that is not t_w.\n", hi_us);
                printf("LED duty is clamp/period, NOT the %% commanded -- ~%lu %% if t_w=86us.\n",
                       (unsigned long)(86ul * beam_actual_freq_hz() / 10000ul));
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
            uint32_t f0 = (uint32_t)strtoul(argv[2], NULL, 0);
            uint32_t f1 = (uint32_t)strtoul(argv[3], NULL, 0);
            uint32_t n  = (uint32_t)strtoul(argv[4], NULL, 0);
            uint32_t dw = (uint32_t)strtoul(argv[5], NULL, 0);
            if (n < 2) { printf("ERR: need >= 2 steps\n"); return; }
            if (!power_rails_ready()) { printf("ERR: rails down\n"); return; }

            printf("Sweeping %lu -> %lu Hz in %lu steps, %lu ms each, at %.1f%% duty.\n",
                   (unsigned long)f0, (unsigned long)f1, (unsigned long)n,
                   (unsigned long)dw, (double)(beam_duty() * 100.0f));
            printf("Measure the ACTUAL duty at TP5 at each step and note where it stops\n");
            printf("tracking the commanded value â€” that frequency is the answer to Q2.\n\n");
            printf("  step   requested   actual   TOP    level\n");

            beam_enable(true);
            for (uint32_t i = 0; i < n; i++) {
                uint32_t f = f0 + (uint64_t)(f1 - f0) * i / (n - 1);
                beam_configure(f, beam_duty(), beam_phase_ticks());
                printf("  %2lu/%lu  %8lu   %8lu  %5lu  %5lu\n",
                       (unsigned long)(i + 1), (unsigned long)n, (unsigned long)f,
                       (unsigned long)beam_actual_freq_hz(), (unsigned long)beam_top(),
                       (unsigned long)(beam_duty() * (beam_top() + 1)));
                sleep_ms(dw);
            }
            printf("\nsweep done â€” beam left running at %lu Hz. 'beam off' when finished.\n",
                   (unsigned long)beam_actual_freq_hz());
            return;
        }

        printf("? 'beam %s' â€” try 'help'\n", argv[1]);
    }

    else if (!strcmp(c, "fault")) {
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

    else if (!strcmp(c, "reset")) {
        if (!reset_guard_ok(argc, argv, "reset")) return;
        printf("resetting...\n"); sleep_ms(50);
        watchdog_reboot(0, 0, 0);
    }
    else if (!strcmp(c, "bootsel")) {
        if (!reset_guard_ok(argc, argv, "bootsel")) return;
        printf("rebooting to BOOTSEL...\n"); sleep_ms(50);
        reset_usb_boot(0, 0);
    }

    else printf("? '%s' â€” try 'help'\n", c);
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
    dispatch(argc, argv);
    printf("> ");
}

void cli_init(void) {
    s_len = 0;
    printf("\n%s\n> ", k_help);
}

void cli_service(void) {
    int ch;
    while ((ch = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (ch == '\r' || ch == '\n') {
            putchar('\n');
            s_line[s_len] = '\0';
            s_len = 0;
            handle_line();
        } else if (ch == 8 || ch == 127) {          // backspace / delete
            if (s_len) { s_len--; printf("\b \b"); }
        } else if (ch >= 32 && ch < 127) {
            if (s_len < CLI_MAX_LINE - 1) { s_line[s_len++] = (char)ch; putchar(ch); }
        }
    }
}
