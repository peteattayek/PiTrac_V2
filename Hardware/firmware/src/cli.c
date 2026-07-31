// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "cli.h"
#include "board.h"
#include "safe_state.h"
#include "adc_engine.h"
#include "power_fsm.h"
#include "panel.h"
#include "beam.h"

#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"   // clock_get_hz / clk_sys, for the `id` command
#include "pico/bootrom.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CLI_MAX_LINE 96
#define CLI_MAX_ARGS 8

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
    "  fault                      show / 'fault clear'\n"
    "  reset                      soft reset\n"
    "  bootsel                    reboot to USB mass storage\n";

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
    printf("adcmode  : %d\n", (int)adc_engine_mode());
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
        if (!((ADC_VALID_MASK >> ch) & 1u)) {
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
            printf("phase    : %ld ticks  (%.2f deg)\n", (long)beam_phase_ticks(),
                   (double)(360.0f * beam_phase_ticks() / (beam_top() + 1)));
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
            printf("freq <- %lu Hz requested, %lu Hz actual (TOP=%lu)\n",
                   (unsigned long)f, (unsigned long)beam_actual_freq_hz(),
                   (unsigned long)beam_top());
            if (beam_actual_freq_hz() != f)
                printf("      (SYSCLK/%lu is not an integer â€” nearest achievable shown)\n",
                       (unsigned long)f);
            return;
        }

        if (!strcmp(argv[1], "duty")) {
            if (argc < 3) { printf("usage: beam duty <pct>\n"); return; }
            float d = strtof(argv[2], NULL) / 100.0f;
            if (d > 0.35f) {
                printf("REFUSED: %.1f%% exceeds the 35%% design ceiling.\n"
                       "         30%% is the intended operating point (~3.15 W in D11).\n",
                       (double)(d * 100.0f));
                return;
            }
            beam_set_duty(d);      // preserves TOP; no round-trip through freq
            printf("duty <- %.2f %%  (level %lu of %lu)\n", (double)(d * 100.0f),
                   (unsigned long)(d * (beam_top() + 1)), (unsigned long)(beam_top() + 1));
            return;
        }

        if (!strcmp(argv[1], "ramp")) {
            if (argc < 3) { printf("usage: beam ramp <pct> [step_ms]\n"); return; }
            float d = strtof(argv[2], NULL) / 100.0f;
            uint32_t ms = (argc > 3) ? (uint32_t)strtoul(argv[3], NULL, 0) : 250;
            if (d > 0.35f) { printf("REFUSED: >35%% duty\n"); return; }
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
            printf("Setting 1 kHz / 50%% â€” a 500 us commanded high phase.\n");
            printf("Scope TP5. The pulse you see IS the U9 clamp width.\n");
            printf("  .md claims 113 us;  0.7*R68*C57 = 0.7*56k*2.2n = ~86 us.\n");
            printf("  Record the real number â€” it sets STROBE_SW_MAX_US for Phase 6.\n");
            printf("Safe: even 113 us at 1 kHz is only 11%% duty.\n");
            beam_configure(1000, 0.50f, beam_phase_ticks());
            if (!beam_enable(true)) printf("ERR: rails down â€” use 'on' first.\n");
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
        if (argc > 1 && !strcmp(argv[1], "clear")) { fault_clear(); printf("cleared\n"); }
        else printf("fault = %s\n", fault_name(fault_current()));
    }

    else if (!strcmp(c, "reset")) {
        printf("resetting...\n"); sleep_ms(50);
        watchdog_reboot(0, 0, 0);
    }
    else if (!strcmp(c, "bootsel")) {
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
