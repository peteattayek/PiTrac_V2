// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// strobe_fsm_test.c -- the REAL power_fsm.c, service.c, telemetry.c and strobe.c
// together (with camera.c, shot.c and the strobe policy), against the register
// model of engine_test.c. Added for image 1.9, review finding V18-F1:
//
//   1.8 bound a live strobe session, and decided whether live mode arms the
//   watchdog, from power_pi_present() alone. That is the power FSM's debounced,
//   conservative classification: in RUNNING it stays true after the Pi's 3V3 goes
//   away, and in BENCH_RUNNING a late Pi's 3V3 is up for PI_PRESENT_DEBOUNCE_MS
//   before it turns true. engine_test toggled a free bool in place of the FSM, so
//   it could not see that. Here the FSM is the real one, stepped through
//   pitrac_service() in the firmware's own order.
//
// The harness (the register model and the stubs) is adapted from the 1.8
// independent review's probe, captures/review_2026-11-12/independent_v18_probes/
// integration_probe.c. The cases below are new: they assert what 1.9 must do,
// where the probe's witnesses asserted what 1.8 got wrong.
//
// Image 1.10, review finding V19-F1: the same FSM windows under `reset`,
// `bootsel` and `cfg save`'s guard. Those are copied verbatim out of cli.c and
// config_store.c by tests/extract_functions.py (fsm_guards_extract.c, included
// below), the way the 1.9 review's probe copied them, and run against the real
// FSM: before, during and after late detection, and in POWERING_ON.
//
// Image 1.11, review finding V110-F1: the Pi's 3V3 rising DURING the commands'
// own 50 ms wait (g_rise_in_sleep, the way the 1.10 review's probe raised it),
// and the watchdog left as it was by a refusal at the second check.
//
// Image 1.12, review note V111-N2: the same rise while the command's message is
// going out, before the sleep (g_rise_in_print). The second check refuses it too,
// and the message no longer pins every rise on the 50 ms wait.

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdk_standin.h"
#include "board.h"
#include "strobe.h"
#include "camera.h"
#include "cam_plan.h"
#include "shot.h"
#include "safe_state.h"
#include "power_fsm.h"
#include "service.h"
#include "beam.h"
#include "detect.h"
#include "adc_engine.h"
#include "dma_safe.h"
#include "pio_alloc.h"

#include "strobe_burst.pio.h"     // PICO_NO_HARDWARE: the instruction arrays only
#include "strobe_gate.pio.h"
#include "cam_handshake.pio.h"

static const char *test_name;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, test_name, #c); \
    return false; } } while (0)

// ===========================================================================
// The register model
// ===========================================================================
#define NPINS 48
static struct { gpio_function_t fn; bool out, dir_out, in; } g_pin[NPINS];

typedef struct {
    bool     enabled;
    unsigned pc;
    unsigned tx;                 // TX FIFO level
    uint32_t rx[8];
    unsigned rx_head, rx_n;
    // Diagnostics: the state at the most recent PC read (the gate witness).
    bool     pc_read_enabled;
} sm_model_t;

pio_hw_t standin_pio[3];
static sm_model_t g_sm[3][4];
static bool     g_irq[3][8];
static bool     g_pio_drive[3][32];      // what each block's SMs drive on a pin
static pwm_hw_t g_pwm;
pwm_hw_t *pwm_hw = &g_pwm;
static bool g_complete_burst;
static unsigned g_firings;
static uint64_t g_now_us = 1000000u;
static bool     g_pad25_sio_low_at_pc_read;   // GPIO25 SIO, driven low, at the last PIO0 SM0 PC read

static unsigned blk(PIO p) { return (unsigned)(p - standin_pio); }

void standin_hard_assert(int ok, const char *what, const char *file, int line) {
    if (ok) return;
    fprintf(stderr, "hard_assert(%s) failed at %s:%d\n", what, file, line);
    exit(3);
}

uint32_t        clock_get_hz(int clk) { (void)clk; return 150000000u; }
uint64_t        time_us_64(void) { return g_now_us; }
absolute_time_t get_absolute_time(void) { return g_now_us; }
uint32_t        to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000u); }
void            busy_wait_us(uint64_t us) { g_now_us += us; }
uint32_t        save_and_disable_interrupts(void) { return 0u; }
void            restore_interrupts(uint32_t s) { (void)s; }

bool gpio_get(uint p) {
    switch (g_pin[p].fn) {
        case GPIO_FUNC_SIO:  return g_pin[p].dir_out ? g_pin[p].out : g_pin[p].in;
        case GPIO_FUNC_PIO0: return g_pio_drive[0][p & 31u];
        case GPIO_FUNC_PIO1: return g_pio_drive[1][p & 31u];
        case GPIO_FUNC_PIO2: return g_pio_drive[2][p & 31u];
        default:             return g_pin[p].in;
    }
}
bool            gpio_get_out_level(uint p) { return g_pin[p].out; }
bool            gpio_is_dir_out(uint p) { return g_pin[p].dir_out; }
gpio_function_t gpio_get_function(uint p) { return g_pin[p].fn; }
void            gpio_put(uint p, bool v) { g_pin[p].out = v; }
void            gpio_set_dir(uint p, bool out) { g_pin[p].dir_out = out; }
void            gpio_set_function(uint p, gpio_function_t fn) { g_pin[p].fn = fn; }

uint pio_get_index(PIO p) { return blk(p); }
uint pio_get_funcsel(PIO p) { return (uint)GPIO_FUNC_PIO0 + blk(p); }
uint pio_get_gpio_base(PIO p) { return p == pio2 ? 16u : 0u; }
void pio_sm_claim(PIO p, uint sm) { (void)p; (void)sm; }
int  pio_add_program(PIO p, const pio_program_t *prog) { (void)p; return 32 - (int)prog->length; }
int  pio_add_program_at_offset(PIO p, const pio_program_t *prog, uint off) { (void)p; (void)prog; return (int)off; }
void pio_sm_set_enabled(PIO p, uint sm, bool en) {
    g_sm[blk(p)][sm].enabled = en;
    if (g_complete_burst && p == pio0 && sm == 0u && en) { g_firings++; g_irq[0][0] = true; }
    if (en) p->ctrl |= 1u << sm; else p->ctrl &= ~(1u << sm);
}
void pio_sm_clear_fifos(PIO p, uint sm) { g_sm[blk(p)][sm].tx = 0; g_sm[blk(p)][sm].rx_n = 0; }
void pio_sm_restart(PIO p, uint sm) { (void)p; (void)sm; }   // the PC is NOT reset (datasheet)
uint pio_encode_jmp(uint addr) { return addr & 31u; }        // JMP always: opcode 000, cond 000
void pio_sm_exec(PIO p, uint sm, uint instr) {
    if ((instr >> 13) == 0u) g_sm[blk(p)][sm].pc = instr & 31u;     // only jmps are exec'd
}
void pio_sm_set_pins_with_mask(PIO p, uint sm, uint32_t v, uint32_t m) {
    (void)sm;
    for (unsigned i = 0; i < 32; i++) if (m & (1u << i)) g_pio_drive[blk(p)][i] = (v >> i) & 1u;
}
void pio_sm_set_pindirs_with_mask(PIO p, uint sm, uint32_t d, uint32_t m) { (void)p; (void)sm; (void)d; (void)m; }
void pio_gpio_init(PIO p, uint pin) { g_pin[pin].fn = (gpio_function_t)(GPIO_FUNC_PIO0 + blk(p)); }
uint pio_get_dreq(PIO p, uint sm, bool tx) { (void)p; (void)sm; (void)tx; return 0u; }
uint pio_sm_get_pc(PIO p, uint sm) {
    sm_model_t *s = &g_sm[blk(p)][sm];
    s->pc_read_enabled = s->enabled;
    if (p == pio0 && sm == 0u)
        g_pad25_sio_low_at_pc_read = g_pin[PIN_STROBE_PULSE].fn == GPIO_FUNC_SIO &&
                                     g_pin[PIN_STROBE_PULSE].dir_out && !g_pin[PIN_STROBE_PULSE].out;
    return s->pc;
}
uint pio_sm_get_tx_fifo_level(PIO p, uint sm) { return g_sm[blk(p)][sm].tx; }
uint pio_sm_get_rx_fifo_level(PIO p, uint sm) { return g_sm[blk(p)][sm].rx_n; }
bool pio_sm_is_rx_fifo_empty(PIO p, uint sm) { return g_sm[blk(p)][sm].rx_n == 0u; }
void pio_sm_put(PIO p, uint sm, uint32_t d) { (void)d; if (g_sm[blk(p)][sm].tx < 4u) g_sm[blk(p)][sm].tx++; }
uint32_t pio_sm_get(PIO p, uint sm) {
    sm_model_t *s = &g_sm[blk(p)][sm];
    if (!s->rx_n) return 0u;
    uint32_t v = s->rx[s->rx_head];
    s->rx_head = (s->rx_head + 1u) % 8u;
    s->rx_n--;
    return v;
}
bool pio_interrupt_get(PIO p, uint n) { return g_irq[blk(p)][n & 7u]; }
void pio_interrupt_clear(PIO p, uint n) { g_irq[blk(p)][n & 7u] = false; }

void strobe_burst_program_init(PIO p, uint sm, uint off, uint pin) { (void)pin; g_sm[blk(p)][sm].pc = off; }
void cam_handshake_program_init(PIO p, uint sm, uint off, uint a, uint b, uint c) {
    (void)a; (void)b; (void)c; g_sm[blk(p)][sm].pc = off;
}

static unsigned g_dma_last_count;
int                dma_claim_unused_channel(bool req) { (void)req; return 3; }
dma_channel_config dma_channel_get_default_config(uint ch) { (void)ch; dma_channel_config c = {0}; return c; }
void channel_config_set_transfer_data_size(dma_channel_config *c, enum dma_channel_transfer_size s) { (void)c; (void)s; }
void channel_config_set_read_increment(dma_channel_config *c, bool i) { (void)c; (void)i; }
void channel_config_set_write_increment(dma_channel_config *c, bool i) { (void)c; (void)i; }
void channel_config_set_dreq(dma_channel_config *c, uint d) { (void)c; (void)d; }
void dma_channel_configure(uint ch, const dma_channel_config *c, volatile void *w,
                           const volatile void *r, uint n, bool trig) {
    (void)ch; (void)c; (void)w; (void)r;
    g_dma_last_count = n;
    if (trig) g_sm[0][0].tx = n < 8u ? n : 8u;        // the feed's initial fill (TX joined, 8)
}
dma_stop_t dma_stop_safely(unsigned ch) { (void)ch; dma_stop_t s = { true, false, false }; return s; }

uint       pwm_gpio_to_slice_num(uint p) { return (p >> 1) & 7u; }
uint       pwm_gpio_to_channel(uint p) { return p & 1u; }
pwm_config pwm_get_default_config(void) { pwm_config c = {0, 0, 0}; return c; }
void       pwm_config_set_wrap(pwm_config *c, uint16_t w) { c->top = w; }
void       pwm_config_set_clkdiv_int(pwm_config *c, uint d) { c->div = d; }
void       pwm_init(uint s, const pwm_config *c, bool start) { (void)s; (void)c; (void)start; }
void       pwm_set_chan_level(uint s, uint ch, uint16_t lvl) {
    uint32_t cc = g_pwm.slice[s].cc;
    g_pwm.slice[s].cc = ch ? ((cc & 0xFFFFu) | ((uint32_t)lvl << 16)) : ((cc & 0xFFFF0000u) | lvl);
}
void       pwm_set_enabled(uint s, bool e) { (void)s; (void)e; }

const pio_program_t strobe_burst_program  = { strobe_burst_program_instructions,
                                              (uint8_t)count_of(strobe_burst_program_instructions), -1 };
const pio_program_t strobe_gate_program   = { strobe_gate_program_instructions,
                                              (uint8_t)count_of(strobe_gate_program_instructions), -1 };
const pio_program_t cam_handshake_program = { cam_handshake_program_instructions,
                                              (uint8_t)count_of(cam_handshake_program_instructions), -1 };

// ===========================================================================

#include "telemetry.h"
#include "config_store.h"
#include "hardware/structs/watchdog.h"

// The rest of the firmware as stubs. power_fsm.c, service.c, telemetry.c and
// strobe.c are the REAL ones.
static fault_t g_fault;
static uint32_t g_fault_gen;
static unsigned g_watchdog_arms, g_watchdog_kicks;
watchdog_hw_t mock_watchdog_hw;

void watchdog_enable(uint32_t ms, bool pause) {
    (void)pause; if (ms != 1000u) exit(4);
    mock_watchdog_hw.ctrl |= WATCHDOG_CTRL_ENABLE_BITS;
    g_watchdog_arms++;
}
void watchdog_update(void) { g_watchdog_kicks++; }
absolute_time_t make_timeout_time_ms(uint32_t ms) { return g_now_us + (uint64_t)ms * 1000u; }
int64_t absolute_time_diff_us(absolute_time_t a, absolute_time_t b) { return (int64_t)(b - a); }
// g_rise_in_sleep: the Pi's 3V3 rises 6 ms into the next sleep of 6 ms or more
// (image 1.11, V110-F1; the 1.10 review's probe did the same). One-shot.
static bool g_rise_in_sleep;
void sleep_ms(uint32_t ms) {
    if (g_rise_in_sleep && ms >= 6u) {
        g_now_us += 6000u;
        g_pin[PIN_PI_3V3_SENSE].in = true;
        g_rise_in_sleep = false;
        ms -= 6u;
    }
    g_now_us += (uint64_t)ms * 1000u;
}
int getchar_timeout_us(uint32_t us) { (void)us; return -1; }
fault_t fault_current(void) { return g_fault; }
void fault_raise(fault_t f) { if (!g_fault) { g_fault = f; g_fault_gen++; } }
void fault_clear(void) { g_fault = FAULT_NONE; }
uint32_t fault_generation(void) { return g_fault_gen; }
bool beam_enabled(void) { return false; }
bool beam_enable(bool on) { (void)on; return true; }
bool detect_armed(void) { return false; }
bool detect_arm(bool on) { (void)on; return true; }
bool detect_hpf_set(hpf_mode_t m) { (void)m; return true; }
void detect_hpf_safe_off(void) { }
void detect_service(void) { }
bool detect_last_pass(detect_pass_t *out) { (void)out; return false; }
void panel_onboard_update(void) { }
void panel_update(void) { }
void safe_state_pi_shutdown_line(pi_line_t mode) { (void)mode; }
void safe_state_pi_uart_lines(bool on) { (void)on; }
adc_5vin_status_t adc_5vin_read(float *v) { *v = 5.20f; return ADC_5VIN_FRESH; }
static adc_mode_t g_adc_mode;
adc_mode_t adc_engine_mode(void) { return g_adc_mode; }
void adc_engine_set_mode(adc_mode_t m) { g_adc_mode = m; }
bool adc_ring_running(void) { return true; }
size_t adc_ring_written(void) { return 200u; }
bool adc_ring_snapshot(adc_ring_snap_t *s) {
    memset(s, 0, sizeof(*s)); s->t_us = g_now_us; s->w = 200u; return true;
}
size_t adc_ring_freeze_copy(unsigned c, uint64_t since, uint16_t *dst, size_t cap,
                             bool *over, uint64_t *stop, size_t *w) {
    (void)c; (void)since; (void)dst; (void)cap;
    if (over) *over = true;
    if (stop) *stop = g_now_us;
    if (w) *w = 200u;
    return 0u;
}
uint32_t adc_ring_freeze_flags(void) { return 0u; }
uint32_t adc_cycles_per_sample(void) { return 300u; }

static void step(uint32_t ms) {
    g_now_us += (uint64_t)ms * 1000u;
    (void)pitrac_service();
}
// A Pi whose 3V3 is up but whose userspace is not yet: the FSM holds PI_BOOTING.
static bool g_slow_pi;
// The board at power-on, the FSM in STANDBY: no step taken yet.
static void power_on_reset(bool pi) {
    memset(g_pin, 0, sizeof(g_pin));
    for (unsigned p = 0; p < NPINS; p++) g_pin[p].fn = GPIO_FUNC_SIO;
    g_pin[PIN_PWR_TOGGLE].in = true;
    g_pin[PIN_PI_3V3_SENSE].in = pi;
    g_pin[PIN_RPI5_ON].in = pi && !g_slow_pi;
    g_pin[PIN_LATCH_CONTROL].dir_out = true;
    g_pin[PIN_PULSE_LIMIT_DIS].dir_out = true;
    g_pin[PIN_STROBE_PULSE].dir_out = true;
    g_pin[PIN_CAM_TRIGGER].dir_out = true;
    g_adc_mode = ADC_MODE_IDLE;
    strobe_init(); camera_init(); shot_init();
    telemetry_test_power_on(); telemetry_init(NULL, TELE_BOOT_POWER_ON);
    power_fsm_init(); pitrac_watchdog_enable(false); pitrac_abort_clear();
}
static void boot(bool pi) {
    power_on_reset(pi);
    (void)power_request_on(); step(0); step(pi ? RAIL_SETTLE_MS : PI_DETECT_WINDOW_MS);
    if (pi) step(0);
    g_now_us += 100000u;
}
static void show(const char *label) {
    printf("%s: FSM=%s raw3V3=%d RPI5_ON=%d cachedPi=%d live=%d wdog=%d gate=%u downUnrequested=%d\n",
        label, power_state_name(power_fsm_state()), gpio_get(PIN_PI_3V3_SENSE), gpio_get(PIN_RPI5_ON),
        power_pi_present(), strobe_live_armed(), pitrac_watchdog_enabled(),
        (unsigned)strobe_gate_level(), power_pi_down_unrequested(NULL));
}


static strobe_refusal_t exit_refusal(void) {
    strobe_info_t i;
    strobe_info(&i);
    return i.live_exit == STROBE_LIVE_EXIT_CONDITION ? i.live_exit_refusal : STROBE_OK;
}

// V18-F1, A: the Pi's 3V3 goes away under a Pi-present session, the board's
// supply and latch still up. The FSM keeps its classification (by design); the
// session must end at the next service pass, gate at 0, and must not come back.
static bool sense_lost_ends_session(void) {
    boot(true);
    CHECK(power_fsm_state() == PS_RUNNING);
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(!pitrac_watchdog_enabled());
    CHECK(strobe_gate_set(1) == STROBE_OK);
    g_pin[PIN_PI_3V3_SENSE].in = false;
    g_pin[PIN_RPI5_ON].in = false;
    step(0);
    show("3V3 lost, next service pass");
    CHECK(power_pi_present());                    // the FSM's flag is unchanged ...
    CHECK(!strobe_live_armed());                  // ... but the session ended at once
    CHECK(exit_refusal() == STROBE_REFUSE_PI_UNSETTLED);
    CHECK(strobe_gate_level() == 0u);
    CHECK(!pitrac_watchdog_enabled());

    // Past the FSM's down debounce: still RUNNING, still no way back into live
    // mode, and no pulse with a setpoint.
    step(PI_DOWN_DEBOUNCE_MS + 10u);
    show("3V3 lost, after the down debounce");
    CHECK(power_fsm_state() == PS_RUNNING);
    CHECK(power_pi_down_unrequested(NULL));
    CHECK(strobe_live_arm() == STROBE_REFUSE_PI_UNSETTLED);
    CHECK(!strobe_live_armed());
    CHECK(!pitrac_watchdog_enabled());
    CHECK(strobe_gate_set(2) == STROBE_OK);       // dry: the gate may rise ...
    g_complete_burst = true;
    const strobe_burst_t pulse = {20u, 0u, 1u};
    strobe_result_t r = strobe_run(&pulse, STROBE_SW_MAX_US);
    CHECK(r.refusal == STROBE_REFUSE_GATE_NONZERO);   // ... but nothing fires with it
    CHECK(!r.live);
    CHECK(g_firings == 0u);
    return true;
}

// V18-F1, B: a late Pi's 3V3 rises in BENCH_RUNNING. Inside the FSM's debounce,
// live mode must not arm, so nothing arms the watchdog. After it, PI_BOOTING has
// the rails not ready.
static bool late_sense_refuses_arm(void) {
    boot(false);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    g_pin[PIN_PI_3V3_SENSE].in = true;
    step(0);
    show("Late 3V3, inside the FSM's debounce");
    CHECK(!power_pi_present());
    CHECK(strobe_live_arm() == STROBE_REFUSE_PI_UNSETTLED);
    CHECK(!strobe_live_armed());
    CHECK(!pitrac_watchdog_enabled());
    CHECK(g_watchdog_arms == 0u);
    step(PI_PRESENT_DEBOUNCE_MS);
    show("Late 3V3, debounce complete");
    CHECK(power_pi_present());
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    CHECK(strobe_live_arm() == STROBE_REFUSE_RAIL);
    CHECK(g_watchdog_arms == 0u);
    return true;
}

// V18-F1, B': the same late Pi under a session armed without one. It ends at the
// next pass, inside the FSM's debounce, and hands back the watchdog it armed:
// never a powered Pi under live mode's watchdog.
static bool late_sense_ends_session(void) {
    boot(false);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(pitrac_watchdog_enabled());
    CHECK(g_watchdog_arms == 1u);
    CHECK(strobe_gate_set(1) == STROBE_OK);
    g_pin[PIN_PI_3V3_SENSE].in = true;
    step(0);
    show("Late 3V3 under a no-Pi session, next service pass");
    CHECK(!power_pi_present());                   // still inside the FSM's debounce
    CHECK(!strobe_live_armed());
    CHECK(exit_refusal() == STROBE_REFUSE_PI_UNSETTLED);
    CHECK(!pitrac_watchdog_enabled());
    CHECK(strobe_gate_level() == 0u);
    return true;
}

// An ordinary Pi halt: RPI5_ON drops and the header 3V3 stays up (measured on
// this board's Pi, BENCH_P8_PI.md 8.2). The powered-Pi session and its watchdog
// exemption stay.
static bool halt_keeps_session(void) {
    boot(true);
    CHECK(power_fsm_state() == PS_RUNNING);
    CHECK(strobe_live_arm() == STROBE_OK);
    g_pin[PIN_RPI5_ON].in = false;
    step(0);
    step(PI_DOWN_DEBOUNCE_MS + 10u);
    show("Halted Pi, 3V3 still up");
    CHECK(power_pi_down_unrequested(NULL));
    CHECK(strobe_live_armed());
    CHECK(!pitrac_watchdog_enabled());
    strobe_live_disarm();
    return true;
}

// The settled cases through the real FSM: without a Pi live mode arms the
// watchdog and needs it; with one it does neither.
static bool settled_sessions(void) {
    boot(false);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(pitrac_watchdog_enabled());
    CHECK(strobe_live_watchdog_required());
    step(10u);
    CHECK(strobe_live_armed());
    strobe_live_disarm();
    CHECK(!pitrac_watchdog_enabled());

    boot(true);
    CHECK(power_fsm_state() == PS_RUNNING);
    unsigned arms = g_watchdog_arms;
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(!pitrac_watchdog_enabled());
    CHECK(!strobe_live_watchdog_required());
    step(10u);
    show("Pi session, settled");
    CHECK(strobe_live_armed());
    CHECK(g_watchdog_arms == arms);
    strobe_live_disarm();
    return true;
}

// PI_BOOTING (a Pi's 3V3 up, its userspace not): the rails are not ready, so
// live mode cannot arm. Once RUNNING, it arms without the watchdog.
static bool pi_booting_refuses_arm(void) {
    g_slow_pi = true;
    boot(true);
    show("Slow Pi, booting");
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    CHECK(strobe_live_arm() == STROBE_REFUSE_RAIL);
    CHECK(!pitrac_watchdog_enabled());
    g_pin[PIN_RPI5_ON].in = true;
    step(0);
    CHECK(power_fsm_state() == PS_RUNNING);
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(!pitrac_watchdog_enabled());
    strobe_live_disarm();
    return true;
}

// The real orderly shutdown ends live mode and zeroes the gate at its start,
// before the Pi's holdoff.
static bool shutdown_ends_live(void) {
    boot(true);
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(strobe_gate_set(1) == STROBE_OK);
    power_request_shutdown();
    step(0);
    show("Orderly shutdown begins");
    CHECK(power_fsm_state() == PS_SHUTTING_DOWN);
    CHECK(!strobe_live_armed());
    CHECK(!pitrac_watchdog_enabled());
    CHECK(strobe_gate_level() == 0u);
    CHECK(!gpio_get(PIN_STROBE_PULSE));
    return true;
}

// The operator's own watchdog stays theirs, with a Pi and without one.
static bool operator_watchdog(void) {
    boot(true);
    pitrac_watchdog_enable(true);
    CHECK(strobe_live_arm() == STROBE_OK);
    strobe_live_disarm();
    CHECK(pitrac_watchdog_enabled());
    pitrac_watchdog_enable(false);

    boot(false);
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(pitrac_watchdog_enabled());             // live mode's ...
    strobe_live_keep_watchdog();                  // ... until `wdog on` makes it the operator's
    strobe_live_disarm();
    CHECK(pitrac_watchdog_enabled());
    return true;
}

// ===========================================================================
// Image 1.10 (V19-F1): `reset`, `bootsel` and `cfg save`'s guard, the firmware's
// own text, against the real FSM.
// ===========================================================================
static unsigned g_reboots, g_bootsels;
// They do not return on the board. Here they count, and the copied command
// returns at its closing brace.
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    (void)pc; (void)sp; (void)delay_ms; g_reboots++;
}
void reset_usb_boot(uint32_t gpio_mask, uint32_t disable_mask) {
    (void)gpio_mask; (void)disable_mask; g_bootsels++;
}
static bool s_overlap;          // config_store.c's: the image overlaps the config sectors

// What the copied commands print, kept for the checks and passed on to stdout.
static char g_out[2048];
// g_rise_in_print: the Pi's 3V3 rises while the command's own attempt message
// ('resetting...' / 'rebooting to BOOTSEL...') is printed -- before its sleep
// (image 1.12, V111-N2). One-shot.
static bool g_rise_in_print;
static int capture_printf(const char *fmt, ...) {
    size_t n = strlen(g_out);
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(g_out + n, sizeof g_out - n, fmt, ap);
    va_end(ap);
    fputs(g_out + n, stdout);
    if (g_rise_in_print && (strstr(g_out + n, "resetting...") ||
                            strstr(g_out + n, "rebooting to BOOTSEL..."))) {
        g_pin[PIN_PI_3V3_SENSE].in = true;
        g_rise_in_print = false;
    }
    return r;
}
// Image 1.14 (V113-F4): cli.c's `off`, `fault clear` and the Pi-only power rule
// are copied out too. s_from_pi is cli.c's own flag, set here as
// cli_run_remote() sets it around a Pi command.
#include "link_cli.h"
static bool s_from_pi;
const char *fault_name(fault_t f) { return f == FAULT_NONE ? "none" : "a fault"; }

#define printf capture_printf
#include "fsm_guards_extract.c"
#undef printf

// The text is non-NULL and contains `want` (a NULL fails rather than crashing).
static bool has(const char *text, const char *want) {
    return text != NULL && strstr(text, want) != NULL;
}

static char k_reset[] = "reset", k_bootsel[] = "bootsel", k_force[] = "force";
// Plain `reset` and plain `bootsel`: did either reach its reboot?
static bool plain_reset_admitted(void) {
    g_out[0] = '\0';
    unsigned r = g_reboots;
    char *argv[] = { k_reset };
    cmd_reset(1, argv);
    return g_reboots != r;
}
static bool plain_bootsel_admitted(void) {
    g_out[0] = '\0';
    unsigned b = g_bootsels;
    char *argv[] = { k_bootsel };
    cmd_bootsel(1, argv);
    return g_bootsels != b;
}
static bool forced_reset_admitted(void) {
    g_out[0] = '\0';
    unsigned r = g_reboots;
    char *argv[] = { k_reset, k_force };
    cmd_reset(2, argv);
    return g_reboots != r;
}
static bool forced_bootsel_admitted(void) {
    g_out[0] = '\0';
    unsigned b = g_bootsels;
    char *argv[] = { k_bootsel, k_force };
    cmd_bootsel(2, argv);
    return g_bootsels != b;
}

// The reviewer's scenario: BENCH_RUNNING, the latch closed, a late Pi's 3V3 up,
// inside the FSM's 100 ms debounce. Plain `reset` and `bootsel` are refused,
// with the watchdog left as it was; so is `cfg save`. Before the 3V3 rises
// both are admitted (no Pi); after the debounce they stay refused.
static bool reset_late_pi(void) {
    boot(false);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(gpio_get_out_level(PIN_LATCH_CONTROL));
    CHECK(plain_reset_admitted());                 // no Pi: a reset is only a reset
    CHECK(plain_bootsel_admitted());
    CHECK(cfg_save_blocked_reason() == NULL);

    pitrac_watchdog_enable(true);                  // the operator's: a refusal keeps it
    g_pin[PIN_PI_3V3_SENSE].in = true;
    step(0);
    show("Late 3V3, inside the FSM's debounce: plain reset and bootsel");
    CHECK(power_pi_3v3_sensed());
    CHECK(!power_pi_present());
    CHECK(!plain_reset_admitted());
    CHECK(has(g_out, "REFUSED: a Pi's 3V3 is sensed (GPIO24), not yet classified"));
    CHECK(has(g_out, "Wait until 'stat' shows the Pi"));
    CHECK(!has(g_out, "Shut the Pi down first"));
    CHECK(!plain_bootsel_admitted());
    CHECK(has(g_out, "'bootsel force'"));
    CHECK(pitrac_watchdog_enabled());
    CHECK(has(cfg_save_blocked_reason(), "3V3 is sensed"));
    CHECK(has(cfg_save_unblock_hint(), "PI_BOOTING"));

    step(PI_PRESENT_DEBOUNCE_MS - 1u);             // the last pass of the debounce
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(!plain_reset_admitted());
    CHECK(!plain_bootsel_admitted());
    CHECK(cfg_save_blocked_reason() != NULL);

    step(1u);
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    CHECK(!plain_reset_admitted());
    CHECK(has(g_out, "REFUSED: a Pi is present and powered"));
    CHECK(has(g_out, "Shut the Pi down first"));
    CHECK(!plain_bootsel_admitted());
    CHECK(cfg_save_blocked_reason() != NULL);
    CHECK(pitrac_watchdog_enabled());

    CHECK(forced_reset_admitted());                // `force` is still the way through
    CHECK(!pitrac_watchdog_enabled());             // ... and disarms the watchdog first
    return true;
}

// POWERING_ON: the latch closed, the Pi's 3V3 already up, the FSM waiting
// RAIL_SETTLE_MS before it looks. Plain `reset` and `bootsel` are refused there
// too; `cfg save` is refused by the state. From 1.11 they are refused in
// POWERING_ON with nothing sensed as well: the FSM has not decided yet, and this
// is when a Pi raises its 3V3. Admitted once BENCH_RUNNING says there is none.
static bool reset_powering_on(void) {
    power_on_reset(true);
    (void)power_request_on();
    step(0);
    step(RAIL_SETTLE_MS / 2u);
    show("POWERING_ON, the Pi's 3V3 up before the settle");
    CHECK(power_fsm_state() == PS_POWERING_ON);
    CHECK(gpio_get_out_level(PIN_LATCH_CONTROL));
    CHECK(!power_pi_present());
    CHECK(!plain_reset_admitted());
    CHECK(!plain_bootsel_admitted());
    CHECK(cfg_save_blocked_reason() != NULL);

    power_on_reset(false);                         // the same moment with no Pi sensed
    (void)power_request_on();
    step(0);
    pitrac_watchdog_enable(true);
    step(RAIL_SETTLE_MS / 2u);
    CHECK(power_fsm_state() == PS_POWERING_ON);
    CHECK(!power_pi_maybe_present());
    CHECK(!plain_reset_admitted());                // 1.11: undecided is not "no Pi"
    CHECK(has(g_out, "REFUSED: the board is still looking for a Pi"));
    CHECK(has(g_out, "BENCH_RUNNING"));
    CHECK(!plain_bootsel_admitted());
    CHECK(pitrac_watchdog_enabled());              // a refusal keeps it
    step(PI_DETECT_WINDOW_MS);                     // the FSM decides: no Pi
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(plain_reset_admitted());
    CHECK(!pitrac_watchdog_enabled());             // the reboot path disarms it
    CHECK(plain_bootsel_admitted());
    return true;
}

// V110-F1: the Pi's 3V3 rises 6 ms into the commands' 50 ms wait, after their
// entry check passed -- in BENCH_RUNNING (a late Pi). The second check, after
// the wait, refuses: no reboot, and the watchdog as it was, armed or not.
// Nothing rising: the reboot is reached and the watchdog disarmed just before.
// `force` still goes through a rise.
static bool reset_rise_in_wait(void) {
    boot(false);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(!power_pi_maybe_present());

    pitrac_watchdog_enable(true);                  // the operator's
    g_rise_in_sleep = true;
    CHECK(!plain_reset_admitted());
    show("Plain reset, the 3V3 risen during its 50 ms wait");
    CHECK(!g_rise_in_sleep);                       // the rise happened inside the wait
    CHECK(power_pi_3v3_sensed() && !power_pi_present());
    CHECK(has(g_out, "resetting..."));             // past the entry check ...
    CHECK(has(g_out, "REFUSED: a Pi's 3V3 is sensed (GPIO24), not yet classified"));
    CHECK(has(g_out, "Nothing was done: the Pi's 3V3 rose after the first check"));
    CHECK(has(g_out, "message or the 50 ms wait), before 'reset'"));
    CHECK(pitrac_watchdog_enabled());              // ... and the watchdog kept

    boot(false);                                   // bootsel, the watchdog not armed
    CHECK(!pitrac_watchdog_enabled());
    g_rise_in_sleep = true;
    CHECK(!plain_bootsel_admitted());
    CHECK(!g_rise_in_sleep);
    CHECK(has(g_out, "rebooting to BOOTSEL..."));
    CHECK(has(g_out, "before 'bootsel'"));
    CHECK(!pitrac_watchdog_enabled());

    boot(false);                                   // bootsel, the watchdog armed
    pitrac_watchdog_enable(true);
    g_rise_in_sleep = true;
    CHECK(!plain_bootsel_admitted());
    CHECK(pitrac_watchdog_enabled());

    boot(false);                                   // nothing rises: through, disarmed
    pitrac_watchdog_enable(true);
    CHECK(plain_reset_admitted());
    CHECK(!pitrac_watchdog_enabled());
    pitrac_watchdog_enable(true);
    CHECK(plain_bootsel_admitted());
    CHECK(!pitrac_watchdog_enabled());

    boot(false);                                   // `force` through a rise
    pitrac_watchdog_enable(true);
    g_rise_in_sleep = true;
    CHECK(forced_reset_admitted());
    CHECK(!pitrac_watchdog_enabled());
    boot(false);
    g_rise_in_sleep = true;
    CHECK(forced_bootsel_admitted());
    return true;
}

// V111-N2: the 3V3 rises while the attempt message is printed, after the entry
// check and before the sleep. Refused at the second check like a rise in the
// wait, with the watchdog kept; the message names the whole interval.
static bool reset_rise_in_message(void) {
    boot(false);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(!power_pi_maybe_present());
    pitrac_watchdog_enable(true);
    g_rise_in_print = true;
    CHECK(!plain_reset_admitted());
    show("Plain reset, the 3V3 risen while its message printed");
    CHECK(!g_rise_in_print);                       // the rise happened in the message
    CHECK(has(g_out, "resetting..."));
    CHECK(has(g_out, "Nothing was done: the Pi's 3V3 rose after the first check"));
    CHECK(pitrac_watchdog_enabled());

    boot(false);
    g_rise_in_print = true;
    CHECK(!plain_bootsel_admitted());
    CHECK(!g_rise_in_print);
    CHECK(has(g_out, "before 'bootsel'"));
    return true;
}

// The settled states are unchanged from 1.7: refused with a Pi RUNNING, and still
// refused after its 3V3 is lost there (the FSM keeps its classification);
// admitted in STANDBY, where the latch is already open, even with a 3V3 sensed.
// A glitch shorter than the debounce leaves nothing refused once it is over.
static bool reset_settled(void) {
    boot(true);
    CHECK(power_fsm_state() == PS_RUNNING);
    CHECK(!plain_reset_admitted());
    CHECK(!plain_bootsel_admitted());
    g_pin[PIN_PI_3V3_SENSE].in = false;
    g_pin[PIN_RPI5_ON].in = false;
    step(0);
    step(PI_DOWN_DEBOUNCE_MS + 10u);
    CHECK(power_fsm_state() == PS_RUNNING);
    CHECK(!plain_reset_admitted());
    CHECK(!plain_bootsel_admitted());
    CHECK(cfg_save_blocked_reason() != NULL);

    power_on_reset(false);
    CHECK(power_fsm_state() == PS_STANDBY);
    CHECK(!gpio_get_out_level(PIN_LATCH_CONTROL));
    g_pin[PIN_PI_3V3_SENSE].in = true;             // a Pi powered some other way
    CHECK(plain_reset_admitted());                 // the latch is open: nothing to cut
    CHECK(cfg_save_blocked_reason() != NULL);      // ... but no flash write
    CHECK(has(cfg_save_unblock_hint(), "own USB-C"));

    boot(false);
    g_pin[PIN_PI_3V3_SENSE].in = true;
    step(0);
    CHECK(!plain_reset_admitted());
    g_pin[PIN_PI_3V3_SENSE].in = false;            // gone inside the debounce: a glitch
    step(1u);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(plain_reset_admitted());
    CHECK(cfg_save_blocked_reason() == NULL);
    return true;
}

// ===========================================================================
// Image 1.14 (V113-F4): what `off` and `fault clear` from the Pi do, through
// cli.c's own rule and commands, against the real FSM.
// ===========================================================================

// A line as cli_run_remote() runs it: split, the Pi-only rule, then (only the
// two commands under test) dispatched with s_from_pi set. True if it ran.
static bool run_pi(const char *line) {
    static char copy[96];
    char *argv[8];
    int argc = 0;
    g_out[0] = '\0';
    snprintf(copy, sizeof(copy), "%s", line);
    for (char *p = copy; *p && argc < 8;) {
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    if (!pi_rule_admits(&argc, argv)) return false;
    s_from_pi = true;
    if (!strcmp(argv[0], "off")) cmd_off(argc, argv);
    else if (!strcmp(argv[0], "fault")) cmd_fault(argc, argv);
    else exit(5);
    s_from_pi = false;
    return true;
}

// The same command from USB: no rule, s_from_pi clear.
static void run_usb(const char *line) {
    static char copy[96];
    char *argv[8];
    int argc = 0;
    g_out[0] = '\0';
    snprintf(copy, sizeof(copy), "%s", line);
    for (char *p = copy; *p && argc < 8;) {
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    if (!strcmp(argv[0], "off")) cmd_off(argc, argv);
    else cmd_fault(argc, argv);
}

// The reviewer's FAULT: a Pi slower than the boot timeout, so the FSM latched
// FAULT_PI_BOOT_TIMEOUT with the rail left up on purpose, and then its
// userspace came up -- RPI5_ON high, the UART connected, the Pi talking.
static void fault_with_late_pi(void) {
    g_slow_pi = true;
    power_on_reset(true);
    g_slow_pi = false;
    (void)power_request_on();
    step(0);
    step(RAIL_SETTLE_MS);
    step(PI_BOOT_TIMEOUT_MS + 1u);
    g_pin[PIN_RPI5_ON].in = true;
    step(1);
}

// In that FAULT, a plain `off` and a plain `fault clear` from the Pi are refused
// and change nothing; `fault clear confirm` cuts it, counted, as the owner
// chose. From USB both still end FAULT, as before.
static bool pi_power_in_fault(void) {
    fault_with_late_pi();
    show("FAULT after the boot timeout, the Pi's userspace up since");
    CHECK(power_fsm_state() == PS_FAULT && gpio_get_out_level(PIN_LATCH_CONTROL));
    CHECK(gpio_get(PIN_RPI5_ON) && power_pi_present());
    const uint32_t cuts = telemetry_get(TELE_PI_POWER_CUT);

    CHECK(!run_pi("off"));
    CHECK(has(g_out, "REFUSED from the Pi: 'off' in FAULT would cut this Pi's power"));
    CHECK(has(g_out, "'fault clear confirm'"));
    CHECK(!has(g_out, "'off' is the orderly shutdown"));
    CHECK(!run_pi("off confirm"));
    CHECK(!run_pi("fault clear"));
    CHECK(has(g_out, "acknowledging FAULT drops the rail under this Pi"));
    CHECK(!has(g_out, "'off' is the orderly shutdown"));     // V113-F4's fallback text
    CHECK(!run_pi("fault clear now"));
    step(1);
    step(1);
    CHECK(power_fsm_state() == PS_FAULT && gpio_get_out_level(PIN_LATCH_CONTROL));
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == cuts);
    CHECK(fault_current() == FAULT_PI_BOOT_TIMEOUT);

    CHECK(run_pi("fault clear confirm"));                       // the owner's form
    CHECK(has(g_out, "acknowledging FAULT: the rail drops"));
    step(1);
    step(1);
    CHECK(power_fsm_state() == PS_STANDBY && !gpio_get_out_level(PIN_LATCH_CONTROL));
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == cuts + 1u);      // a cut, and counted

    fault_with_late_pi();                                       // USB, unchanged
    const uint32_t cuts2 = telemetry_get(TELE_PI_POWER_CUT);
    run_usb("off");
    CHECK(has(g_out, "requested shutdown"));
    step(1);
    step(1);
    CHECK(!gpio_get_out_level(PIN_LATCH_CONTROL));
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == cuts2 + 1u);
    fault_with_late_pi();
    run_usb("fault clear");
    step(1);
    step(1);
    CHECK(!gpio_get_out_level(PIN_LATCH_CONTROL));
    return true;
}

// Image 1.15: `strobe cal` with a stop already asked as its loop starts ends
// ABORTED, with the stop marked taken -- what tells the Pi it stopped -- the gate
// at 0 and nothing fired. 1.14 had no host test of it (its mutation M24 went
// unchecked); the 1.14 review checked it with its own probe, and this is that
// check kept.
static bool always_stop(void) { return true; }

static bool cal_stop_marked_taken(void) {
    boot(true);
    CHECK(power_fsm_state() == PS_RUNNING);
    CHECK(strobe_live_arm() == STROBE_OK);
    g_complete_burst = true;
    g_firings = 0u;
    pitrac_abort_clear();
    pitrac_abort_source(always_stop);                 // the Pi's CLI_ABORT, as pi_link.c gives it
    (void)pitrac_yield_ms(0u);
    pitrac_abort_source(NULL);
    CHECK(pitrac_abort_pending() && !pitrac_abort_was_taken());
    const strobe_cal_t c = strobe_cal(9.0f, NULL);
    CHECK(c.res == STROBE_CAL_RES_ABORTED);
    CHECK(pitrac_abort_was_taken());
    CHECK(strobe_gate_level() == 0u);
    CHECK(g_firings == 0u);
    return true;
}

// Before the Pi is RUNNING -- late detection in BENCH_RUNNING, PI_BOOTING --
// `off` from the Pi is refused and nothing is left queued; in RUNNING it is the
// orderly shutdown, as before. A `fault clear` from the Pi outside FAULT clears
// the code and queues no acknowledgement.
static bool pi_off_orderly_only(void) {
    boot(false);
    g_pin[PIN_PI_3V3_SENSE].in = true;                          // a late Pi, in the debounce
    g_pin[PIN_RPI5_ON].in = true;
    step(0);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING);
    CHECK(!run_pi("off"));
    CHECK(has(g_out, "'off' in BENCH_RUNNING would cut this Pi's power"));
    CHECK(has(g_out, "Retry once 'stat' shows RUNNING"));
    step(1);
    CHECK(power_fsm_state() == PS_BENCH_RUNNING && gpio_get_out_level(PIN_LATCH_CONTROL));
    step(PI_PRESENT_DEBOUNCE_MS);
    CHECK(power_fsm_state() == PS_PI_BOOTING);
    CHECK(!run_pi("off"));
    step(1);
    CHECK(power_fsm_state() == PS_RUNNING);                     // nothing was left queued
    step(10);
    CHECK(power_fsm_state() == PS_RUNNING);

    const uint32_t cuts = telemetry_get(TELE_PI_POWER_CUT);
    fault_raise(FAULT_CAM_TIMEOUT);                             // a code, in RUNNING
    CHECK(run_pi("fault clear"));                               // no confirm needed here
    CHECK(fault_current() == FAULT_NONE && has(g_out, "cleared"));
    step(1);
    CHECK(power_fsm_state() == PS_RUNNING);

    CHECK(run_pi("off"));
    CHECK(has(g_out, "requested shutdown (orderly"));
    step(1);
    CHECK(power_fsm_state() == PS_SHUTTING_DOWN && gpio_get_out_level(PIN_LATCH_CONTROL));
    CHECK(!run_pi("off"));                                      // already under way
    CHECK(has(g_out, "A shutdown is already under way"));
    g_pin[PIN_RPI5_ON].in = false;                              // the Pi halts
    step(PI_SHUTDOWN_MIN_HOLDOFF_MS);
    step(1);
    CHECK(power_fsm_state() == PS_STANDBY);
    CHECK(telemetry_get(TELE_PI_POWER_CUT) == cuts);            // orderly: no cut
    return true;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(n) { #n, n }
        TEST(sense_lost_ends_session), TEST(late_sense_refuses_arm),
        TEST(late_sense_ends_session), TEST(halt_keeps_session),
        TEST(settled_sessions), TEST(pi_booting_refuses_arm),
        TEST(shutdown_ends_live), TEST(operator_watchdog),
        TEST(reset_late_pi), TEST(reset_powering_on), TEST(reset_settled),
        TEST(reset_rise_in_wait), TEST(reset_rise_in_message),
        TEST(pi_power_in_fault), TEST(pi_off_orderly_only), TEST(cal_stop_marked_taken),
#undef TEST
    };
    if (argc == 2) {
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            if (strcmp(argv[1], cases[i].name) != 0) continue;
            test_name = cases[i].name;
            if (!cases[i].run()) return 1;
            printf("PASS %s\n", test_name);
            return 0;
        }
    }
    fprintf(stderr, "Expected one registered test name\n");
    return 2;
}
