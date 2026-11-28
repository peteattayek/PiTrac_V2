// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "power_fsm.h"
#include "board.h"
#include "safe_state.h"
#include "adc_engine.h"
#include "detect.h"
#include "beam.h"
#include "strobe.h"
#include "camera.h"
#include "telemetry.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

// ---------------------------------------------------------------------------

static pstate_t s_ps = PS_STANDBY;
static uint32_t s_t_state;         // ms at entry to the current state
static uint32_t s_t_shutdown_req;  // ms when RPI5_SHUTDOWN was asserted
static bool     s_pi_present;
static bool     s_req_on, s_req_shutdown, s_req_force_off, s_req_fault_ack;
static bool     s_rail_checked;    // POWERING_ON: the one-shot under-load check has run
static bool     s_pi_seen;         // BENCH_RUNNING: 3V3 high, debounce in progress
static uint32_t s_t_pi_seen;
static bool     s_clean_off;       // SHUTTING_DOWN saw the Pi down: FORCE_OFF is no power cut

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
// Sustained supply monitor -- runs the whole time the latch is closed.
//
// The one-shot check at latch time cannot see the supply being pulled afterwards.
// When that happens Q3 stays closed and the +5V rail is quietly back-fed from USB
// VBUS through D8 (a 1 A SS14). Harmless-looking with no Pi; destructive with one.
// ---------------------------------------------------------------------------

static uint32_t s_t_last_v5check;
static uint32_t s_t_v5_low_since;
static bool     s_v5_low;
static bool     s_v5_unreadable;   // the reading that ended the debounce was not FRESH

static void v5_monitor_reset(void) {
    s_v5_low = false;
    s_v5_unreadable = false;
    s_t_last_v5check = now_ms();
}

// True once +5V_IN has been continuously below the threshold -- or continuously
// UNREADABLE -- for the debounce period. Rate-limited so we are not hammering
// the ADC from the superloop.
//
// Unreadable counts as low (independent review round 5, 2026-10-12): a STALLED
// ring used to hand back stale samples as if fresh, which would have blinded
// this monitor. NOT_SAMPLED is different -- the mode deliberately excludes ch1
// (ARMED while the detector waits, BURST around a strobe firing) -- and leaves
// the debounce exactly as it was: it neither starts nor clears it, and no old
// value is ever judged. The gap that leaves while ARMED is a known limitation
// (PROGRESS.md section 9).
static bool supply_lost(void) {
    uint32_t t = now_ms();
    if (t - s_t_last_v5check < V5_MONITOR_INTERVAL_MS) return false;
    s_t_last_v5check = t;

    float v = 0.0f;
    adc_5vin_status_t s = adc_5vin_read(&v);
    if (s == ADC_5VIN_NOT_SAMPLED) return false;
    if (s != ADC_5VIN_FRESH || v < V5_MIN_SUSTAINED) {
        s_v5_unreadable = (s != ADC_5VIN_FRESH);
        if (!s_v5_low) { s_v5_low = true; s_t_v5_low_since = t; return false; }
        return (t - s_t_v5_low_since) >= V5_LOW_DEBOUNCE_MS;
    }
    s_v5_low = false;   // recovered -- e.g. a strobe burst finished
    s_v5_unreadable = false;
    return false;
}

static inline bool pi_3v3_present(void) { return gpio_get(PIN_PI_3V3_SENSE); }
static inline bool pi_userspace_up(void) { return gpio_get(PIN_RPI5_ON); }
static inline bool latched(void)         { return gpio_get_out_level(PIN_LATCH_CONTROL); }

// A Pi this board is powering right now: found at power-on, the latch closed,
// and its 3V3 up.
static bool pi_powered(void) {
    return latched() && s_pi_present && pi_3v3_present();
}

// ...and not yet judged safe to cut. The halt telemetry's "power cut" means this
// was true when the latch opened, by FORCE_OFF or by a reset (BENCH_P8_PI.md 8.6).
static bool pi_at_risk(void) {
    return pi_powered() && !s_clean_off;
}

// J8.37, UART1 and the telemetry mirror, at the end of every step.
//
// RPI5_SHUTDOWN (board.h, "THE LINE'S REST STATE"). The request is a pulse:
// ASSERTED for PI_SHUTDOWN_PULSE_MS from begin_shutdown(), by which time the Pi
// has latched the KEY_POWER event and holding it serves no purpose. Otherwise
// DEASSERTED only while the latch is closed AND the Pi's 3V3 reads up, and
// RELEASED the rest of the time -- so nothing is ever driven high into an
// unpowered Pi, including the moments before its 3V3 comes up and after it
// drops at halt.
static void pi_lines_service(void) {
    bool pulse = s_ps == PS_SHUTTING_DOWN && since(s_t_shutdown_req) < PI_SHUTDOWN_PULSE_MS;
    pi_line_t mode = PI_LINE_RELEASED;
    if (latched() && pulse)                 mode = PI_LINE_ASSERTED;
    else if (latched() && pi_3v3_present()) mode = PI_LINE_DEASSERTED;
    safe_state_pi_shutdown_line(mode);

    // UART1 (J8.8/J8.10, the serial link): connected only while the Pi is up and
    // talking -- the latch closed, its 3V3 up AND RPI5_ON high (safe_state.h).
    // In any state: it follows the Pi, not the FSM, so it drops with RPI5_ON at
    // the end of a shutdown, before the 3V3 does.
    safe_state_pi_uart_lines(latched() && pi_3v3_present() && pi_userspace_up());

    // For the next boot: would a reset now cut a powered Pi?
    telemetry_pi_powered(pi_at_risk());
}

// ---------------------------------------------------------------------------
// RUNNING: the Pi going down when nobody asked (BENCH_P8_PI.md 8.6).
//
// Counted once per drop (TELE_PI_DOWN_UNREQ), and once more if it comes back
// without a power cycle (TELE_PI_BACK_UP) -- a Pi reboot passes through here,
// since RPI5_ON drops with userspace. Counted and shown only: the rail stays up.
// Whether the board should then drop it waits on 8.2 (does the header 3V3 fall
// at halt?), because until then the indication may rest on RPI5_ON alone. The
// session still ends the usual way: the button, or `off`.
// ---------------------------------------------------------------------------
static bool     s_down_seen;       // reading down, debounce running
static uint32_t s_t_down_seen;
static bool     s_down_unreq;      // counted down; watching for it to come back
static bool     s_up_seen;         // ...reading up again, debounce running
static uint32_t s_t_up_seen;
static uint32_t s_t_down_unreq;    // when the drop was counted, for the CLI

static void watch_reset(void) {
    s_down_seen = s_down_unreq = s_up_seen = false;
}

static void watch_pi_down(void) {
    uint32_t t = now_ms();
    bool down = power_pi_is_down();
    if (!s_down_unreq) {
        if (!down) { s_down_seen = false; return; }
        if (!s_down_seen) { s_down_seen = true; s_t_down_seen = t; return; }
        if (t - s_t_down_seen < PI_DOWN_DEBOUNCE_MS) return;
        s_down_unreq   = true;
        s_up_seen      = false;
        s_t_down_unreq = t;
        telemetry_count(TELE_PI_DOWN_UNREQ);
        return;
    }
    if (down) { s_up_seen = false; return; }
    if (!s_up_seen) { s_up_seen = true; s_t_up_seen = t; return; }
    if (t - s_t_up_seen < PI_PRESENT_DEBOUNCE_MS) return;
    s_down_unreq = false;
    s_down_seen  = false;
    telemetry_count(TELE_PI_BACK_UP);
}

// "Pi is down": its 3V3 is gone, or RPI5_ON is low.
//
// MEASURED 2026-11-09 (BENCH_P8_PI.md 8.1 and 8.2, a Pi 5 with its default
// bootloader settings): the header 3.3 V rail does NOT drop at halt. J8.1 held
// ~3.25 V for 12 s after a requested shutdown and 25 s after a `sudo halt`,
// until the latch opened. So PI_3V3_SENSE never fires at halt, and RPI5_ON is
// the only down indicator that works. The 3V3 term stays for a Pi that loses
// its power some other way.
//
// RPI5_ON: the Pi's late shutdown hook (pi/) drives GPIO22 low once every disk
// filesystem is read-only; on the bench it read low within 2.5 s of a request.
// If the hook finds a writable filesystem it leaves the line high, and the
// board waits for PI_SHUTDOWN_MAX_WAIT_MS. Whether GPIO22 would also fall at
// halt without the hook is not measured.
bool power_pi_is_down(void) {
    return !pi_3v3_present() || !pi_userspace_up();
}

// ---------------------------------------------------------------------------

void power_fsm_init(void) {
    s_btn_last_raw = s_btn_stable = false;
    s_btn_last_change = s_btn_press_start = now_ms();
    s_btn_long_fired = false;
    s_evt_short_press = s_evt_long_press = false;
    s_req_on = s_req_shutdown = s_req_force_off = s_req_fault_ack = false;
    s_pi_present = false;
    s_rail_checked = false;
    s_pi_seen = false;
    s_clean_off = false;
    watch_reset();
    v5_monitor_reset();
    enter(PS_STANDBY);
    pi_lines_service();      // J8.37 released: the latch is open
}

bool power_request_on(void) {
    if (s_ps != PS_STANDBY || s_req_shutdown || s_req_force_off) return false;
    s_req_on = true;
    return true;
}

void power_request_shutdown(void) {
    s_req_on = false;
    // Preserve a shutdown requested during startup, but not one made while off.
    s_req_shutdown = (s_ps != PS_STANDBY && s_ps != PS_FORCE_OFF);
}

bool power_request_shutdown_orderly(void) {
    if (s_ps != PS_RUNNING) return false;
    power_request_shutdown();
    return true;
}

void power_request_force_off(void) {
    s_req_on = s_req_shutdown = false;
    s_req_force_off = true;
}

void power_request_fault_ack(void) { s_req_fault_ack = true; }

pstate_t power_fsm_state(void) { return s_ps; }
bool     power_pi_present(void) { return s_pi_present; }
bool     power_pi_3v3_sensed(void) { return pi_3v3_present(); }
bool     power_pi_maybe_present(void) { return s_pi_present || pi_3v3_present(); }

bool power_pi_down_unrequested(uint32_t *for_ms) {
    bool d = s_ps == PS_RUNNING && s_down_unreq;
    if (for_ms) *for_ms = d ? since(s_t_down_unreq) : 0u;
    return d;
}
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
    // The panel LEDs are owned by panel.c and follow the state automatically.
    //
    // The beam goes dark at the START of teardown, not a holdoff
    // later when the Pi finally reports down -- there is no reason to keep
    // driving D11 through a shutdown, and PI_SHUTDOWN_MAX_WAIT_MS is long.
    beam_enable(false);
    // Same for the strobe: pulse pin low, engine stopped, gate setpoint zero.
    strobe_safe_off();
    // And the camera trigger: a camera on J4 is powered from the Pi, which is
    // about to go down, and a trigger driven into an unpowered camera back-powers
    // its 1.8 V I/O rail (CR-09).
    camera_safe_off();
    gpio_put(PIN_SYSTEM_READY, 0);

    // The request itself is a PI_SHUTDOWN_PULSE_MS pulse on J8.37, which
    // pi_lines_service() starts at the end of this step.
    s_t_shutdown_req = now_ms();
    s_clean_off = false;
    telemetry_count(TELE_PI_SHUTDOWN_REQ);
    enter(PS_SHUTTING_DOWN);
}

void power_fsm_step(void) {
    button_poll();

    // A fault acknowledgement only means anything while a fault is actually
    // latched. Drop a stale one otherwise, or a `fault clear` typed in STANDBY
    // would sit pending and instantly self-acknowledge the NEXT fault to occur.
    if (s_ps != PS_FAULT) s_req_fault_ack = false;

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
                // not on the table -- it would take a holdoff of trying to run a Pi 5
                // through a 1 A diode. Drop the latch now and make the reason
                // legible instead of letting it brown out mysteriously.
                fault_raise(s_v5_unreadable ? FAULT_SUPPLY_UNREADABLE : FAULT_SUPPLY_LOST);
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
        // out everything. Firmware-enforced only -- there is no hardware interlock.
        // Only a FRESH reading may close the latch: a stale or unavailable one
        // proves nothing about the supply now.
        float v5 = 0.0f;
        if (adc_5vin_read(&v5) != ADC_5VIN_FRESH) {
            fault_raise(FAULT_SUPPLY_UNREADABLE);
            enter(PS_FAULT);
            break;
        }
        if (v5 < V5_MIN_FOR_LATCH) {
            fault_raise(FAULT_USB_POWER_ONLY);
            enter(PS_FAULT);
            break;
        }

        gpio_put(PIN_LATCH_CONTROL, 1);
        v5_monitor_reset();
        s_rail_checked = false;
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
        //
        // Once only, at the settle boundary: adc_5vin_read() waits ~1 ms for
        // fresh ring samples, which is fine once but not on every superloop pass
        // through this state. Unreadable is not "probably fine": FORCE_OFF.
        if (!s_rail_checked) {
            float v5 = 0.0f;
            adc_5vin_status_t s5 = adc_5vin_read(&v5);
            if (s5 != ADC_5VIN_FRESH) {
                fault_raise(FAULT_SUPPLY_UNREADABLE);
                enter(PS_FORCE_OFF);
                break;
            }
            if (v5 < V5_MIN_UNDER_LOAD) {
                fault_raise(FAULT_RAIL_COLLAPSE);
                enter(PS_FORCE_OFF);
                break;
            }
            s_rail_checked = true;
        }

        // Pi detection is a WINDOW, not a single sample. See PI_DETECT_WINDOW_MS
        // in board.h: a Pi 5 merely slow to raise its header 3V3 used to be
        // classified as absent, which made the NEXT button press a hard
        // FORCE_OFF against a Pi that was still booting.
        if (pi_3v3_present()) {
            s_pi_present = true;
            enter(PS_PI_BOOTING);
            break;
        }
        if (since(s_t_state) >= PI_DETECT_WINDOW_MS) {
            // No Pi on the header. This is the normal bench path for phases
            // 1-6 and it stays in the final firmware -- it is how development
            // always happens.
            s_pi_present = false;
            s_pi_seen    = false;
            enter(PS_BENCH_RUNNING);
        }
        break;

    case PS_PI_BOOTING:
        if (pi_userspace_up()) { watch_reset(); enter(PS_RUNNING); break; }
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
            break;
        }
        watch_pi_down();
        break;

    case PS_BENCH_RUNNING:
        // LATE Pi detection. A Pi slower than PI_DETECT_WINDOW_MS still gets
        // recognised here, which is what stops that window's exact value from
        // being safety-critical. Without this, a slow Pi stays classified as
        // absent and the press below becomes a hard power cut on a booting Pi.
        //
        // Debounced: a bare edge would promote us to PI_BOOTING, and a
        // subsequent de-assert raises FAULT_NO_PI_DETECTED and drops the rail --
        // in the middle of a beam ramp, say.
        if (pi_3v3_present()) {
            if (!s_pi_seen) {
                s_pi_seen = true;
                s_t_pi_seen = now_ms();
            } else if (since(s_t_pi_seen) >= PI_PRESENT_DEBOUNCE_MS) {
                s_pi_present = true;
                enter(PS_PI_BOOTING);
                break;
            }
        } else {
            s_pi_seen = false;
        }

        // No Pi to shut down: a press just drops the rail.
        if (s_req_shutdown || take_short_press()) {
            s_req_shutdown = false;
            enter(PS_FORCE_OFF);
        }
        break;

    case PS_SHUTTING_DOWN: {
        uint32_t t = since(s_t_shutdown_req);

        // The request pulse on J8.37 is pi_lines_service()'s: asserted until
        // PI_SHUTDOWN_PULSE_MS, then deasserted (or released, once the Pi's 3V3
        // is gone).

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

        if (power_pi_is_down()) {
            // An orderly end: FORCE_OFF is no power cut, and neither is a reset
            // from here on. The count and that change are ONE mirror commit, so a
            // reset on either side of it is counted once: as a cut before it, as
            // clean after it (review 2026-10-19, F2).
            s_clean_off = true;
            telemetry_release_pi(TELE_PI_SHUTDOWN_CLEAN);
            enter(PS_FORCE_OFF);
            break;
        }

        if (t > PI_SHUTDOWN_MAX_WAIT_MS) {
            telemetry_count(TELE_PI_SHUTDOWN_TIMEOUT);
            fault_raise(FAULT_PI_SHUTDOWN_TIMEOUT);
            enter(PS_FORCE_OFF);
        }
        break;
    }

    case PS_FORCE_OFF:
        // No command from the old power cycle may restart or stop the next one.
        s_req_on = s_req_shutdown = s_req_force_off = s_req_fault_ack = false;

        // A powered Pi losing its rail other than after a clean shutdown (8.6):
        // the escape hatch, a timeout, a supply fault, a fault acknowledged with
        // the rails up. The count and "no Pi at risk any more" are ONE mirror
        // commit, made before the latch drops: a reset before it is counted at
        // the next boot, a reset after it is already counted, so one physical cut
        // is one count wherever the reset lands (review 2026-10-19, F2).
        telemetry_release_pi(pi_at_risk() ? TELE_PI_POWER_CUT : TELE__COUNTERS);
        s_clean_off = false;

        // Before the latch, not after. U10 (MCP1416) and D11 both run from the
        // SWITCHED +5V rail, so a carrier still toggling on GPIO31 when the rail
        // goes is 3.3 V logic into an unpowered gate driver -- the same
        // back-feed condition GPIO33 is driven low for just below.
        //
        // This is also what makes `off` mean off. beam.c's s_on is pure firmware
        // state; nothing here used to clear it, so the slices stayed enabled and
        // the pads stayed in GPIO_FUNC_PWM across a rail-down. The beam looked
        // off only because it had no power, and the next `on` relit D11 with no
        // `beam on`, no duty ramp and no cold-LED warning. Every route to
        // rail-down lands here, so this covers the button and the escape hatch
        // and the fault paths, not just the CLI.
        beam_enable(false);
        // The strobe too, and for the same back-feed reason: U5 runs from the
        // always-on +3V3, so a high GPIO25 would drive U8 (MCP1416, on +12 V
        // derived from VIR) while it is losing power. Gate DAC to zero as well,
        // so the next `on` never starts with a current setpoint -- and live
        // strobe mode ends here too, restoring the watchdog it armed.
        strobe_safe_off();
        // The camera trigger too (GPIO10 SIO low, handshake stopped), for the same
        // reason as at the start of a shutdown: never drive J4 into a camera
        // whose power is going.
        camera_safe_off();
        // UART1 too, and BEFORE the latch: its idle-high TX through 220R is the
        // strongest thing this board could drive into a Pi losing power.
        safe_state_pi_uart_lines(false);
        gpio_put(PIN_LATCH_CONTROL, 0);
        // And J8.37 with it: nothing driven into a Pi that is losing power.
        safe_state_pi_shutdown_line(PI_LINE_RELEASED);
        gpio_put(PIN_SYSTEM_READY, 0);
        // GPIO33 must go low with the rail. U14 (the gated-HPF mux) runs from
        // +5VA, and SEL held high into an unpowered mux back-feeds the analog
        // rail through its protection structures -- the same condition
        // safe_state.c drives GPIO33 low at boot to prevent. Without this, an
        // ordinary `hpf hold` followed by `off` walks straight into it, because
        // nothing else ever puts the pin back.
        detect_hpf_safe_off();
        s_pi_present = false;
        watch_reset();
        // Drop back to standby. We stay alive on +3V3 the whole time.
        enter(PS_STANDBY);
        break;

    case PS_FAULT:
        // Latched. A button press acknowledges and returns to standby with the
        // rail down; the fault code itself stays readable over the CLI until
        // then. `fault clear` over the CLI does exactly the same thing, so the
        // two routes cannot leave the indicators disagreeing.
        if (take_short_press() || s_req_shutdown || s_req_fault_ack) {
            s_req_shutdown = false;
            s_req_fault_ack = false;
            fault_clear();
            enter(PS_FORCE_OFF);
        }
        break;

    default:
        // Defensive only -- no transition reaches here. Route through FORCE_OFF
        // rather than straight to STANDBY so it cannot become the one path that
        // skips the teardown (latch drop, GPIO33 low, J8.37 released).
        enter(PS_FORCE_OFF);
        break;
    }

    pi_lines_service();
}
