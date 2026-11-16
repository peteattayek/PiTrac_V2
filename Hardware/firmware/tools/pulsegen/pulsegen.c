// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// pulsegen -- the delayed strobe pair for BENCH_P5_P7_MIC_CAMERA.md 7b, on a spare Raspberry
// Pi Pico (RP2040) or Pico 2 (RP2350). Bench test equipment: it is not PiTrac firmware and
// never runs on the PiTrac board.
//
// Wiring (the Pico's pins 18-23, side by side; all 3.3 V):
//   Pico pin 20, GP15  <-  J4.3  the board's camera trigger (D_Cam_Trigger, through R18 220 R;
//                             J4.4 is the same net, left free for the logic analyser)
//   Pico pin 21, GP16  ->  J4.7  Cam_Strobe 0 (through R19 220 R on the board)
//   Pico pin 22, GP17  ->  J4.8  Cam_Strobe 1 (through R24 220 R)
//   Pico pin 18, GND  ->  J4.2;  pin 23, GND  ->  J4.6
//
// On each rising edge of the trigger, both outputs go high together, a little after the set
// delay: on a Pico 2, board 1's analyser measured 10-18 ns late at J4.4 -> GP16 (2026-11-13),
// about one PIO cycle (6.7 ns; 8 ns on a Pico) of it jitter. The analyser's delay, not the
// set one, is 7b's reference. They stay high for the hold
// (10 ms by default; 7b asks for at least 6 ms), then go low. They idle LOW, as 7b requires:
// `cam shot` refuses while a strobe input reads high. A new edge is accepted only after the
// trigger has been low.
//
// Commands over the Pico's USB serial port (any terminal):
//   d <us>   the delay, 1 to 4000 us, to 0.01 us   (e.g. 'd 150', 'd 200.5')
//   h <ms>   the hold, 6 to 100 ms
//   ?        status
//
// Power the Pico (USB) BEFORE the PiTrac board's rail comes up, and keep it powered while
// the board can drive J4.4: an unpowered Pico's GP15 would take the trigger's current
// through its protection diode.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "pulsegen.pio.h"

#define PIN_TRIG         15u
#define PIN_OUT0         16u   // and PIN_OUT0 + 1
#define OVERHEAD_CYCLES  5     // pulsegen.pio: the synchroniser, the wait, the set

static const PIO s_pio = pio0;
static uint      s_sm, s_off;
static double    s_delay_us = 200.0;
static uint32_t  s_hold_ms  = 10u;
static uint32_t  s_shots;

static uint32_t delay_count(void) {
    double cycles = s_delay_us * (double)clock_get_hz(clk_sys) / 1e6;
    long n = (long)(cycles + 0.5) - OVERHEAD_CYCLES;
    return n < 0 ? 0u : (uint32_t)n;
}

// Stop, reset and re-arm the state machine with the current delay. The outputs are low
// throughout: the PIO's last `set` left them low, or they were never raised.
static void arm(void) {
    pio_sm_config c = pulsegen_program_get_default_config(s_off);
    sm_config_set_in_pins(&c, PIN_TRIG);
    sm_config_set_set_pins(&c, PIN_OUT0, 2);
    pio_sm_init(s_pio, s_sm, s_off, &c);
    pio_sm_set_pins_with_mask(s_pio, s_sm, 0u, 3u << PIN_OUT0);
    pio_interrupt_clear(s_pio, 0);
    pio_sm_set_enabled(s_pio, s_sm, true);
    pio_sm_put(s_pio, s_sm, delay_count());
}

static void status(void) {
    uint32_t hz = clock_get_hz(clk_sys);
    printf("pulsegen -- BENCH_P5_P7_MIC_CAMERA.md 7b\n"
           "  GP15 (pin 20) <- J4.3 trigger; GP16 (pin 21) -> J4.7, GP17 (pin 22) -> J4.8;"
           " GND pin 18 -> J4.2, pin 23 -> J4.6\n");
    printf("  delay %.2f us (%lu + %d cycles at %lu Hz, %.1f ns each); hold %lu ms; %lu shots\n",
           s_delay_us, (unsigned long)delay_count(), OVERHEAD_CYCLES, (unsigned long)hz,
           1e9 / (double)hz, (unsigned long)s_hold_ms, (unsigned long)s_shots);
    printf("  trigger now %d, outputs %d %d. 'd <us>' delay, 'h <ms>' hold, '?' this.\n",
           gpio_get(PIN_TRIG), gpio_get(PIN_OUT0), gpio_get(PIN_OUT0 + 1u));
}

static void command(char *line) {
    char *arg = line + 1;
    while (*arg == ' ') arg++;
    char *end = NULL;
    if (line[0] == 'd') {
        double v = strtod(arg, &end);
        if (end == arg || *end != '\0' || v < 1.0 || v > 4000.0) {
            printf("ERR: d <us>, 1 to 4000 (e.g. 'd 150'). Delay unchanged.\n");
            return;
        }
        s_delay_us = (double)(long)(v * 100.0 + 0.5) / 100.0;
        arm();
        printf("delay %.2f us\n", s_delay_us);
    } else if (line[0] == 'h') {
        long v = strtol(arg, &end, 10);
        if (end == arg || *end != '\0' || v < 6 || v > 100) {
            printf("ERR: h <ms>, 6 to 100. Hold unchanged.\n");
            return;
        }
        s_hold_ms = (uint32_t)v;
        printf("hold %lu ms\n", (unsigned long)s_hold_ms);
    } else if (line[0] == '?' || line[0] == '\0') {
        status();
    } else {
        printf("ERR: 'd <us>', 'h <ms>' or '?'\n");
    }
}

int main(void) {
    stdio_init_all();

    gpio_init(PIN_TRIG);
    gpio_set_dir(PIN_TRIG, GPIO_IN);
    gpio_pull_down(PIN_TRIG);

    s_off = (uint)pio_add_program(s_pio, &pulsegen_program);
    s_sm  = (uint)pio_claim_unused_sm(s_pio, true);
    // Low, then outputs, then handed to the PIO: never a high glitch.
    pio_sm_set_pins_with_mask(s_pio, s_sm, 0u, 3u << PIN_OUT0);
    pio_sm_set_pindirs_with_mask(s_pio, s_sm, 3u << PIN_OUT0, 3u << PIN_OUT0);
    pio_gpio_init(s_pio, PIN_OUT0);
    pio_gpio_init(s_pio, PIN_OUT0 + 1u);
    arm();

    absolute_time_t banner = make_timeout_time_ms(2500);   // after USB enumerates
    bool shown = false;
    char line[32];
    size_t n = 0;
    for (;;) {
        if (!shown && time_reached(banner)) { status(); shown = true; }

        if (pio_interrupt_get(s_pio, 0)) {          // the outputs have just gone high
            s_shots++;
            sleep_ms(s_hold_ms);
            pio_interrupt_clear(s_pio, 0);          // the PIO drops both
            pio_sm_put(s_pio, s_sm, delay_count()); // and waits for the next edge
            printf("shot %lu: both strobes high %.2f us after the trigger's rise (set; the"
                   " analyser measures it), %lu ms\n",
                   (unsigned long)s_shots, s_delay_us, (unsigned long)s_hold_ms);
        }

        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) continue;
        if (ch == '\r' || ch == '\n') {
            if (n == 0 && ch == '\n') continue;     // the LF of a CRLF
            line[n] = '\0';
            printf("\n");
            command(line);
            n = 0;
        } else if ((ch == '\b' || ch == 127) && n > 0) {
            n--;
        } else if (ch >= ' ' && n < sizeof line - 1u) {
            line[n++] = (char)ch;
            putchar(ch);
        }
    }
}
