// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// PiTrac "The one board to rule them all" -- RP2354B firmware
//
// Phase 0  : board bring-up, safe state, CLI, ADC block capture
// Phase 1  : power button + latch, USB-power guard
// Phase 1b : full power FSM incl. Pi soft-shutdown (test against a simulated Pi)
// Phase 8  : the Pi serial link (pi_link.c) and firmware updates from the Pi
//            into an A/B partition, with rollback (fw_update.h)
//
// Phase 7  : the camera handshake (camera.c, PIO1) and a dry camera-gated
//            strobe burst (strobe.c, PIO0), for test shots from the CLI
//
// Core 1 is unused so far. The hot path is hardware instead: the comparator
// edge timing (PIO2), the camera handshake (PIO1) and the strobe burst (PIO0,
// started by the handshake's FIRE with no CPU in between).
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
#include "strobe.h"
#include "camera.h"
#include "service.h"
#include "telemetry.h"
#include "fw_layout.h"
#include "fw_update.h"
#include "pi_link.h"
#include "cli.h"

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"

#include <stdio.h>

// Belt and braces against a build configured for the wrong chip variant.
// RP2354B is an RP2350*B*: 80-pin, 48 GPIOs. If something (an IDE wizard, a
// stale CMake cache) built this for an RP2350A we would get 30 GPIOs, and every
// pin from GPIO31 up -- the beam carrier, the demod clock, the threshold DAC,
// the comparator, all five ADC channels -- would quietly not exist.
// Fail at compile time instead.
_Static_assert(NUM_BANK0_GPIOS >= 48,
    "Built for the wrong chip variant. This board is an RP2354B (RP2350B core, "
    "48 GPIOs). Delete the build/ directory and re-run cmake -- see START_HERE.md.");

// Why this boot happened, for the halt telemetry (the `this boot` label and the
// watchdog count). Must run before anything arms or disarms the watchdog: both
// rewrite the scratch register this reads.
//   - watchdog_enable() marks scratch[4]; a reset with that mark: this hung.
//   - FLASH_UPDATE boot type: a UF2 load or an update from the Pi. Tested before
//     watchdog_caused_reboot(), which on the RP2350 also wants a NORMAL boot type
//     (SDK 2.3.0), so up to image 1.2 these read as power-ons (2026-11-10, 8.9).
//   - SOFTWARE: the reason register set on a NORMAL boot: `reset`, a boot ROM
//     reboot, or the trial watchdog's rollback.
//   - anything else (power-on, brownout, SW2, a debugger) clears the reason.
static tele_boot_t boot_reason(void) {
    if (watchdog_enable_caused_reboot()) return TELE_BOOT_WATCHDOG;
    if (rom_get_last_boot_type() == BOOT_TYPE_FLASH_UPDATE) return TELE_BOOT_FLASH_UPDATE;
    if (watchdog_caused_reboot())        return TELE_BOOT_SOFTWARE;
    return TELE_BOOT_POWER_ON;
}

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

    // Where this image is stored (an A/B partition, or flat at 0), from the boot
    // ROM. Before cfg_init(), whose overlap check needs it.
    fw_layout_init();

    // After adc_engine_init() (it pushes adc5v_scale back) and before the
    // modules that consume calibration.
    cfg_init();

    // Halt telemetry (Phase 8.6): the RAM mirror if a warm reset kept it, else
    // the counts saved in the config record. Before power_fsm_init(), which
    // starts reporting into it; a fault raised before this line is not counted.
    telemetry_init(cfg()->telemetry, boot_reason());

    // The UART block and its RX DMA. Before power_fsm_init(), which from then on
    // decides when GPIO36/37 are connected to it (only while a Pi is up).
    pi_link_init();

    power_fsm_init();
    panel_init();
    // After panel_init() (which must have taken GPIO12 off PWM -- A7) and after
    // pio_alloc_init(). Leaves GPIO25 SIO low and the gate DAC at zero; nothing
    // can pulse until the rail is up and a command is admitted.
    strobe_init();
    // Phase 7: the camera handshake on PIO1. After pio_alloc_init(). Leaves
    // GPIO10 SIO low and the state machine stopped; it drives the trigger only
    // for a handshake, and only with the rail up.
    camera_init();
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
    // The saved comparator threshold, likewise after detect_init(), which starts
    // the DAC at level 0. Image 1.6 on; before it a saved threshold was lost.
    cfg_apply_threshold();

    // Give the USB host a moment to enumerate before the CLI banner, otherwise
    // the first thing you see after a reflash is a truncated help text.
    sleep_ms(500);
    cli_init();

    // TRY BEFORE YOU BUY. If the boot ROM launched this image on trial (a
    // FLASH_UPDATE boot of an unbought TBYB image -- an update from the Pi, or a
    // UF2 just loaded over USB), it buys itself here once init has run and the
    // ADC ring delivers fresh +5V_IN, or reboots so the ROM runs the previous
    // image. HERE, BEFORE THE WATCHDOG LINE BELOW: the ROM armed a 16.7 s
    // watchdog for the trial, and disarming it first would leave a hung trial
    // image running instead of rolling back. The Pi is powered again after an
    // update only from the superloop (fwu_service()), after that disarm, so no
    // reset is pending once it is up. fw_update.h has the whole sequence.
#if defined(PITRAC_TEST_TRIAL_HANG) && PITRAC_TEST_TRIAL_HANG
    // TEST BUILD ONLY (CMakeLists.txt, PITRAC_TEST_TRIAL_HANG): on trial, hang
    // here -- no buy, no watchdog disarm -- so the boot ROM's trial watchdog
    // must put the previous image back (BENCH_P8_PI.md 8.7). The latch is open
    // and stays open: nothing is powered.
    if (fwu_port_layout()->buy_pending) {
        printf("TEST BUILD (PITRAC_TEST_TRIAL_HANG): on trial, hanging before the buy.\n"
               "The boot ROM's watchdog should run the previous image within ~17 s.\n");
        for (;;) tight_loop_contents();
    }
#endif
    fwu_boot_check();

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
    // so live strobe mode (6c/6d, `strobe live on confirm`) arms it itself and
    // disarms it again on exit -- rather than an ambient default nobody
    // remembers is on. Not with a Pi powered (image 1.8): the Phase 8 decision
    // (owner, 2026-11-12) is that a watchdog reset there is a hard power cut to
    // the Pi, so live mode runs without it, and a session ends if the Pi's state
    // changes (strobe_plan.h, "A PI ON J8").
    //
    // `wdog on` arms it for a session. Phase 8 will want it re-evaluated as the
    // FSM enters and leaves PS_RUNNING; the hook is pitrac_watchdog_enable()
    // and the decision belongs in power_fsm.c next to those transitions.
    pitrac_watchdog_enable(false);

    // The superloop. pitrac_service() IS the background work -- the FSM, the
    // detect FIFO, the indicators -- and every blocking command path reaches the
    // same function through pitrac_yield_ms().
    //
    // That is the whole point: it is not possible for a yield to service less
    // than the superloop does, because there is only one definition of it. See
    // service.h for what used to happen instead.
    for (;;) {
        pitrac_service();
        cli_service();     // NOT in pitrac_service() -- it would re-enter dispatch
        // The Pi's requests, here for the same reason: one can start an update
        // or a shutdown, which must not happen inside a blocking command.
        pi_link_service();
        // NOT in pitrac_service() either: it may write flash, which must never
        // happen inside a blocking command. Between commands, STANDBY only.
        cfg_telemetry_service();
        // The same for an update's install (STANDBY, latch open, a sector per
        // pass), and the Pi's power after an update boot.
        fwu_service();
        tight_loop_contents();
    }
}
