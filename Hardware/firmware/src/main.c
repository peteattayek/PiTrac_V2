// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// PiTrac "Second Board To Rule Them All" â€” RP2354B firmware
//
// Phase 0  : board bring-up, safe state, CLI, ADC block capture
// Phase 1  : power button + latch, USB-power guard
// Phase 1b : full power FSM incl. Pi soft-shutdown (test against a simulated Pi)
//
// Core 1 is unused so far. It will own the hot path (comparator edge timing,
// speed math, camera handshake, strobe burst) from Phase 3 onward.
//
// See PROGRESS.md for what is done, what is next, and the measurements that
// still have to be taken. See src/board.h before touching any pin.

#include "board.h"
#include "safe_state.h"
#include "adc_engine.h"
#include "power_fsm.h"
#include "panel.h"
#include "beam.h"
#include "detect.h"
#include "pio_alloc.h"
#include "config_store.h"
#include "shot.h"
#include "service.h"
#include "cli.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

// Belt and braces against a build configured for the wrong chip variant.
// RP2354B is an RP2350*B*: 80-pin, 48 GPIOs. If something (an IDE wizard, a
// stale CMake cache) built this for an RP2350A we would get 30 GPIOs, and every
// pin from GPIO31 up â€” the beam carrier, the demod clock, the threshold DAC,
// the comparator, all five ADC channels â€” would quietly not exist.
// Fail at compile time instead.
_Static_assert(NUM_BANK0_GPIOS >= 48,
    "Built for the wrong chip variant. This board is an RP2354B (RP2350B core, "
    "48 GPIOs). Delete the build/ directory and re-run cmake â€” see START_HERE.md.");

int main(void) {
    // FIRST. Before stdio, before peripherals, before anything. Every output
    // that could energise something lands at its safe level here.
    safe_state_init();

    // Before ANY pio_add_program() anywhere: pio_set_gpio_base() refuses once
    // a block has instructions loaded, and GPIO46 is unreachable at base 0.
    pio_alloc_init();

    stdio_init_all();

    adc_engine_init();
    adc_engine_set_mode(ADC_MODE_IDLE);

    // After adc_engine_init() (it pushes adc5v_scale back) and before the
    // modules that consume calibration.
    cfg_init();

    power_fsm_init();
    panel_init();
    // Configures the PWM slices but leaves GPIO31/39 as SIO outputs driven low.
    // The beam stays dark until 'beam on' is typed, and cannot start at boot.
    beam_init();
    // Takes GPIO44 from SIO to PWM at duty 0. Leaves GPIO33 low and GPIO46 as
    // the input safe_state() already configured.
    detect_init();
    shot_init();

    // AFTER beam_init() and detect_init(), because beam_init() writes the
    // compile-time default carrier and would overwrite anything applied earlier.
    // This is what makes `cfg save` actually mean something across a reset.
    cfg_apply_beam();

    // Give the USB host a moment to enumerate before the CLI banner, otherwise
    // the first thing you see after a reflash is a truncated help text.
    sleep_ms(500);
    cli_init();

    // The watchdog stays OFF by default. Decided 2026-08-31, after arming it
    // by default cost a reflash cycle and two bench sessions.
    //
    // The Phase 1b policy -- armed only while no Pi is powered -- is about
    // protecting a Pi from a hung MCU. On the bench there is no Pi, so what it
    // actually buys is a reset when the firmware hangs, and on this board a
    // reset DROPS THE LATCH. That is not a recovery; it is a power cut in the
    // middle of whatever was being measured, and a hang is already obvious to
    // an operator sitting in front of the board.
    //
    // It also interacts with the bootrom: reset_usb_boot() reaches BOOTSEL
    // through the watchdog's own scratch registers, so an armed watchdog broke
    // `bootsel` outright until both commands learned to disarm it.
    //
    // WHERE IT EARNS ITS PLACE IS PHASE 6. A hang with 9 A running through a
    // linear-mode FET is a genuinely different risk from a hang on the bench,
    // and that is the point to arm it -- deliberately, in the strobe code,
    // rather than as an ambient default nobody remembers is on.
    //
    // `wdog on` arms it for a session. Phase 8 will want it re-evaluated as the
    // FSM enters and leaves PS_RUNNING; the hook is pitrac_watchdog_enable()
    // and the decision belongs in power_fsm.c next to those transitions.
    pitrac_watchdog_enable(false);

    // The superloop is deliberately two lines. pitrac_service() IS the
    // background work -- the FSM, the detect FIFO, the indicators -- and every
    // blocking command path reaches the same function through pitrac_yield_ms().
    //
    // That is the whole point: it is not possible for a yield to service less
    // than the superloop does, because there is only one definition of it. See
    // service.h for what used to happen instead.
    for (;;) {
        pitrac_service();
        cli_service();     // NOT in pitrac_service() -- it would re-enter dispatch
        tight_loop_contents();
    }
}
