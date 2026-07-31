// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "power_fsm.h"
#include "board.h"
#include "safe_state.h"
#include "adc_engine.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

// ---------------------------------------------------------------------------

static pstate_t s_ps = PS_STANDBY;
static uint32_t s_t_state;         // ms at entry to the current state
static uint32_t s_t_shutdown_req;  // ms when RPI5_SHUTDOWN was asserted
static bool     s_pi_present;
static bool     s_req_on, s_req_shutdown, s_req_force_off;

static const char *const k_state_names[PS__COUNT] = {
    [PS_STANDBY]       = "STANDBY",
    [PS_POWERING_ON]   = "POWERING_ON",
    [PS_PI_BOOTING]    = "PI_BOOTING",
    [PS_RUNNING]       = "RUNNING",
    [PS_BENCH_RUNNING] = "BENCH_RUNNING",
    [PS_SHUTTING_DOWN] = "SHUTTING_DOWN",
    [PS_FORCE_OFF]     = "FORCE_OFF",
    [PS_FAULT]         = "FAULT",
};

static inline uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
static inline uint32_t since(uint32_t t) { return now_ms() - t; }

static void enter(pstate_t s) { s_ps = s; s_t_state = now_ms(); }

// ---------------------------------------------------------------------------
// Button: active low, debounced. R42 1K + C40 100 nF only give ~100 us of
// hardware filtering, which does nothing for contact bounce.
// ---------------------------------------------------------------------------

static bool     s_btn_stable;      // true = pressed
static bool     s_btn_last_raw;
static uint32_t s_btn_last_change;
static uint32_t s_btn_press_start;
static bool     s_btn_long_fired;

static bool s_evt_short_press;
static bool s_evt_long_press;

static void button_poll(void) {
    bool raw = !gpio_get(PIN_PWR_TOGGLE);   // active low
    uint32_t t = now_ms();

    if (raw != s_btn_last_raw) {
        s_btn_last_raw = raw;
        s_btn_last_change = t;
        return;
    }
    if (t - s_btn_last_change < BUTTON_DEBOUNCE_MS) return;
    if (raw == s_btn_stable) {
        // Held: fire the long-press once, without waiting for release.
        if (s_btn_stable && !s_btn_long_fired &&
            (t - s_btn_press_start) >= BUTTON_LONG_PRESS_MS) {
            s_btn_long_fired = true;
            s_evt_long_press = true;
        }
        return;
    }

    s_btn_stable = raw;
    if (raw) {
        s_btn_press_start = t;
        s_btn_long_fired = false;
    } else {
        // Release. A short press only counts if the long press didn't already
        // fire, otherwise a 6-second hold would emit both events.
        if (!s_btn_long_fired) s_evt_short_press = true;
    }
}

static bool take_short_press(void) { bool e = s_evt_short_press; s_evt_short_press = false; return e; }
static bool take_long_press(void)  { bool e = s_evt_long_press;  s_evt_long_press  = false; return e; }

// ---------------------------------------------------------------------------
// Pi-facing signals
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Sustained supply monitor â€” runs the whole time the latch is closed.
//
// The one-shot check at latch time cannot see the supply being pulled afterwards.
// When that happens Q3 stays closed and the +5V rail is quietly back-fed from USB
// VBUS through D8 (a 1 A SS14). Harmless-looking with no Pi; destructive with one.
// ---------------------------------------------------------------------------

static uint32_t s_t_last_v5check;
static uint32_t s_t_v5_low_since;
static bool     s_v5_low;

static void v5_monitor_reset(void) {
    s_v5_low = false;
    s_t_last_v5check = now_ms();
}

// True once +5V_IN has been continuously below the threshold for the debounce
// period. Rate-limited so we are not hammering the ADC from the superloop.
static bool supply_lost(void) {
    uint32_t t = now_ms();
    if (t - s_t_last_v5check < V5_MONITOR_INTERVAL_MS) return false;
    s_t_last_v5check = t;

    if (adc_read_5vin_volts() < V5_MIN_SUSTAINED) {
        if (!s_v5_low) { s_v5_low = true; s_t_v5_low_since = t; return false; }
        return (t - s_t_v5_low_since) >= V5_LOW_DEBOUNCE_MS;
    }
    s_v5_low = false;   // recovered â€” e.g. a strobe burst finished
    return false;
}

static inline bool pi_3v3_present(void) { return gpio_get(PIN_PI_3V3_SENSE); }
static inline bool pi_userspace_up(void) { return gpio_get(PIN_RPI5_ON); }

static void pi_shutdown_assert(bool on) {
#if PI_SHUTDOWN_ACTIVE_LOW
    gpio_put(PIN_RPI5_SHUTDOWN, on ? 0 : 1);
#else
    gpio_put(PIN_RPI5_SHUTDOWN, on ? 1 : 0);
#endif
}

// "Pi is down", two-of-three.
//
// The .md keys this on PI_3V3_SENSE alone, which assumes the Pi 5's header
// 3.3 V rail actually drops at halt. If the PMIC keeps it alive, that
// transition never fires and every shutdown falls through to the 60 s timeout.
//
// RPI5_ON is the good fallback: at halt the SoC is off and Pi GPIO22 reverts to
// its default pull-down, so this reads low regardless of what the 3.3 V rail
// does â€” and independently of whether the systemd unit's ExecStop ran.
//
// Which one is primary gets decided by a one-line measurement in Phase 8.2
// (`sudo halt`, then DMM on J8.1). Until then, accept either.
bool power_pi_is_down(void) {
    return !pi_3v3_present() || !pi_userspace_up();
}

// ---------------------------------------------------------------------------

void power_fsm_init(void) {
    s_btn_last_raw = s_btn_stable = false;
    s_btn_last_change = s_btn_press_start = now_ms();
    s_btn_long_fired = false;
    s_evt_short_press = s_evt_long_press = false;
    s_req_on = s_req_shutdown = s_req_force_off = false;
    s_pi_present = false;
    v5_monitor_reset();
    enter(PS_STANDBY);
}

void power_request_on(void)        { s_req_on = true; }
void power_request_shutdown(void)  { s_req_shutdown = true; }
void power_request_force_off(void) { s_req_force_off = true; }

pstate_t power_fsm_state(void) { return s_ps; }
bool     power_pi_present(void) { return s_pi_present; }
uint32_t power_state_elapsed_ms(void) { return since(s_t_state); }

const char *power_state_name(pstate_t s) {
    if (s >= PS__COUNT) return "?";   // enum is unsigned; s < 0 can never be true
    const char *n = k_state_names[s];
    return n ? n : "?";
}

bool power_rails_ready(void) {
    switch (s_ps) {
        case PS_RUNNING:
        case PS_BENCH_RUNNING:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------

static void begin_shutdown(void) {
    // Nothing high-energy may run during teardown, ever.
    // (Detection/strobe lockout hooks land here in phases 3 and 6.)
    // The panel LEDs are owned by panel.c and follow the state automatically.
    gpio_put(PIN_SYSTEM_READY, 0);

    pi_shutdown_assert(true);
    s_t_shutdown_req = now_ms();
    enter(PS_SHUTTING_DOWN);
}

void power_fsm_step(void) {
    button_poll();

    // The escape hatch works from anywhere.
    if (s_req_force_off || take_long_press()) {
        s_req_force_off = false;
        enter(PS_FORCE_OFF);
    }

    // Supply monitor: active in every state where the latch is closed and the
    // rail has had time to settle. Deliberately NOT in POWERING_ON, which does
    // its own one-shot check against the lower V5_MIN_UNDER_LOAD floor once the
    // 250 ms settle has elapsed.
    switch (s_ps) {
        case PS_PI_BOOTING:
        case PS_RUNNING:
        case PS_BENCH_RUNNING:
        case PS_SHUTTING_DOWN:
            if (supply_lost()) {
                // Cannot sustain the rail from USB, so an orderly Pi shutdown is
                // not on the table â€” it would take 15 s of trying to run a Pi 5
                // through a 1 A diode. Drop the latch now and make the reason
                // legible instead of letting it brown out mysteriously.
                fault_raise(FAULT_SUPPLY_LOST);
                enter(PS_FORCE_OFF);
            }
            break;
        default:
            break;
    }

    switch (s_ps) {

    case PS_STANDBY: {
        bool go = s_req_on || take_short_press();
        s_req_on = false;
        if (!go) break;

        // The guard that makes USB-C bench work safe. On USB alone the board
        // sits at ~4.6-4.7 V through D8, which is below the Pi 5's brownout
        // threshold AND below the boost's 4.74 V UVLO. Latching there browns
        // out everything. Firmware-enforced only â€” there is no hardware interlock.
        float v5 = adc_read_5vin_volts();
        if (v5 < V5_MIN_FOR_LATCH) {
            fault_raise(FAULT_USB_POWER_ONLY);
            enter(PS_FAULT);
            break;
        }

        gpio_put(PIN_LATCH_CONTROL, 1);
        v5_monitor_reset();
        enter(PS_POWERING_ON);
        break;
    }

    case PS_POWERING_ON:
        // No PGOOD and no VIR tap exist on this board (all eight ADC-capable
        // pins are committed), so boost readiness is timed open-loop: ~86 ms
        // soft start plus margin.
        if (since(s_t_state) < RAIL_SETTLE_MS) break;

        // Confirm the rail actually came up and didn't collapse under load.
        // Deliberately a much lower bar than V5_MIN_FOR_LATCH: the Pi and the
        // boost are drawing now, and a real supply is allowed to sag.
        if (adc_read_5vin_volts() < V5_MIN_UNDER_LOAD) {
            fault_raise(FAULT_RAIL_COLLAPSE);
            enter(PS_FORCE_OFF);
            break;
        }

        s_pi_present = pi_3v3_present();
        if (s_pi_present) {
            enter(PS_PI_BOOTING);
        } else {
            // No Pi on the header. This is the normal bench path for phases
            // 1-6 and it stays in the final firmware â€” it is how development
            // always happens.
            enter(PS_BENCH_RUNNING);
        }
        break;

    case PS_PI_BOOTING:
        if (pi_userspace_up()) { enter(PS_RUNNING); break; }
        // The Pi vanished mid-boot (or was never really there).
        if (!pi_3v3_present()) { fault_raise(FAULT_NO_PI_DETECTED); enter(PS_FORCE_OFF); break; }
        if (since(s_t_state) > PI_BOOT_TIMEOUT_MS) {
            fault_raise(FAULT_PI_BOOT_TIMEOUT);
            // Deliberately do NOT cut power: leave the rails up so the Pi can
            // be debugged over its own console, and keep the CLI responsive.
            enter(PS_FAULT);
        }
        break;

    case PS_RUNNING:
        if (s_req_shutdown || take_short_press()) {
            s_req_shutdown = false;
            begin_shutdown();
        }
        break;

    case PS_BENCH_RUNNING:
        // No Pi to shut down: a press just drops the rail.
        if (s_req_shutdown || take_short_press()) {
            s_req_shutdown = false;
            enter(PS_FORCE_OFF);
        }
        break;

    case PS_SHUTTING_DOWN: {
        uint32_t t = since(s_t_shutdown_req);

        // Release the request line after the pulse; the Pi has latched the
        // KEY_POWER event by now and holding it asserted serves no purpose.
        if (t >= PI_SHUTDOWN_PULSE_MS) pi_shutdown_assert(false);

        // Swallow presses during teardown: must not re-latch, must not
        // re-request. (The 5 s hold escape hatch is handled above.)
        (void)take_short_press();
        s_req_shutdown = false;

        // Two guard times, both load-bearing.
        //
        // MIN_HOLDOFF is an absolute floor: never drop the latch sooner than
        // this after the request even if the down-indicator already says the Pi
        // is gone. A glitch on a 1K sense line must not be able to yank power
        // mid-filesystem-sync.
        if (t < PI_SHUTDOWN_MIN_HOLDOFF_MS) break;

        if (power_pi_is_down()) { enter(PS_FORCE_OFF); break; }

        if (t > PI_SHUTDOWN_MAX_WAIT_MS) {
            fault_raise(FAULT_PI_SHUTDOWN_TIMEOUT);
            enter(PS_FORCE_OFF);
        }
        break;
    }

    case PS_FORCE_OFF:
        gpio_put(PIN_LATCH_CONTROL, 0);
        gpio_put(PIN_SYSTEM_READY, 0);
        pi_shutdown_assert(false);
        s_pi_present = false;
        // Drop back to standby. We stay alive on +3V3 the whole time.
        enter(PS_STANDBY);
        break;

    case PS_FAULT:
        // Latched. A button press acknowledges and returns to standby with the
        // rail down; the fault code itself stays readable over the CLI.
        if (take_short_press() || s_req_shutdown) {
            s_req_shutdown = false;
            fault_clear();
            enter(PS_FORCE_OFF);
        }
        break;

    default:
        enter(PS_STANDBY);
        break;
    }
}
