// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// engine_test.c -- the UNCHANGED strobe.c, camera.c and shot.c, and the `gpio`
// command copied verbatim from cli.c, run against a small model of the registers
// they use (sdk_standin/sdk_standin.h). Added 2026-11-10 for the independent
// review's findings:
//   H7-F2  a camera-gated burst stopped by a power abort, a fault or a timeout was
//          reported "never opened -- nothing fired" after GPIO25 had pulsed;
//   H7-F3  `gpio 10 1` drove the camera trigger with the +5 V rail down.
//
// The model does not run the PIO programs (tests/cam_pio_test.c does that, cycle by
// cycle). The test puts the state machines where the hardware would have them --
// a PC in the burst's high loop with GPIO25 driven, IRQ0 raised, a word in the
// camera's RX FIFO -- and checks what the firmware concludes.

#include <assert.h>
#include <errno.h>
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
// The rest of the firmware, as stubs
// ===========================================================================
static bool    g_rails = true;
static fault_t g_fault = FAULT_NONE;
static bool    g_wdog;

bool    power_rails_ready(void) { return g_rails; }
static bool    g_pi, g_sensed;    // the FSM's classification; the 3V3 sensed now
bool    power_pi_present(void) { return g_pi; }
bool    power_pi_3v3_sensed(void) { return g_sensed; }
fault_t fault_current(void) { return g_fault; }
void    fault_raise(fault_t f) { if (g_fault == FAULT_NONE) g_fault = f; }
void    pitrac_watchdog_enable(bool on) { g_wdog = on; }
bool    pitrac_watchdog_enabled(void) { return g_wdog; }
yield_t pitrac_service(void) { return YIELD_OK; }
yield_t pitrac_yield_ms(uint32_t ms) { g_now_us += (uint64_t)ms * 1000u; return YIELD_OK; }
bool    pitrac_abort_pending(void) { return false; }
bool    beam_enabled(void) { return false; }
bool    detect_armed(void) { return false; }
bool    detect_arm(bool on) { (void)on; return true; }
bool    detect_hpf_set(hpf_mode_t m) { (void)m; return true; }
bool    detect_last_pass(detect_pass_t *out) { (void)out; return false; }
adc_mode_t adc_engine_mode(void) { return ADC_MODE_IDLE; }
void    adc_engine_set_mode(adc_mode_t m) { (void)m; }
bool    adc_ring_running(void) { return true; }
size_t  adc_ring_written(void) { return 0u; }
bool    adc_ring_snapshot(adc_ring_snap_t *s) { (void)s; return false; }
size_t  adc_ring_freeze_copy(unsigned c, uint64_t since, uint16_t *dst, size_t cap,
                             bool *over, uint64_t *stop, size_t *w) {
    (void)c; (void)since; (void)dst; (void)cap;
    if (over) *over = true;
    if (stop) *stop = 0u;
    if (w) *w = 0u;
    return 0u;
}
uint32_t adc_ring_freeze_flags(void) { return 0u; }
uint32_t adc_cycles_per_sample(void) { return 300u; }

// The `gpio` command, copied verbatim from cli.c by extract_cli_gpio.py.
#include "cli_gpio_extract.c"

// ===========================================================================
// Fixtures
// ===========================================================================
#define GATE   19u      // strobe_gate.pio, just below the burst
#define BURST  20u      // strobe_burst.pio's first word (`pull`)
#define HI     24u      // the burst's high loop (`jmp x--` with GPIO25 high)
#define LO     28u      // its low loop (`jmp y--` with GPIO25 low)

static void boot(void) {
    for (unsigned p = 0; p < NPINS; p++) { g_pin[p].fn = GPIO_FUNC_SIO; g_pin[p].dir_out = false; }
    // As safe_state_init() leaves them.
    g_pin[PIN_PULSE_LIMIT_DIS].dir_out = true;  g_pin[PIN_PULSE_LIMIT_DIS].out = false;
    g_pin[PIN_STROBE_PULSE].dir_out = true;     g_pin[PIN_STROBE_PULSE].out = false;
    g_pin[PIN_CAM_TRIGGER].dir_out = true;      g_pin[PIN_CAM_TRIGGER].out = false;
    strobe_init();
    camera_init();
    shot_init();
    g_now_us += 100000u;                        // past any gate-decay window
}

static const strobe_burst_t k_burst = { 14u, 596u, 3u };

// A camera-gated dry burst, armed and holding at its gate.
static bool arm(void) {
    strobe_result_t r;
    CHECK(strobe_gated_arm(&k_burst, 10000u, &r));
    CHECK(g_sm[0][0].enabled && g_sm[0][0].pc == GATE);
    CHECK(g_pin[PIN_STROBE_PULSE].fn == GPIO_FUNC_PIO0);
    return true;
}

// Where the hardware would be: FIRE taken by the gate, the burst in its first
// pulse (GPIO25 driven high), between pulses, or complete (IRQ0).
static void at_first_pulse(void) { g_sm[0][0].pc = HI; g_pio_drive[0][PIN_STROBE_PULSE] = true; }
static void between_pulses(void) { g_sm[0][0].pc = LO; g_pio_drive[0][PIN_STROBE_PULSE] = false; }
static void completed(void)      { g_sm[0][0].pc = BURST; g_pio_drive[0][PIN_STROBE_PULSE] = false; g_irq[0][0] = true; }

// The gate was read with the engine stopped and GPIO25 already SIO low.
static bool witnessed_stopped(void) {
    CHECK(!g_sm[0][0].pc_read_enabled);
    CHECK(g_pad25_sio_low_at_pc_read);
    return true;
}

// ===========================================================================
// H7-F2: the gate, as the stopped engine shows it
// ===========================================================================

// Stopped before FIRE: the gate never opened. Safe-off, then cancel, twice.
static bool gate_abort_before_fire(void) {
    boot();
    if (!arm()) return false;
    strobe_safe_off();
    if (!witnessed_stopped()) return false;
    strobe_result_t r;
    CHECK(strobe_gated_cancel(&r) == STROBE_GATE_CLOSED);
    CHECK(r.outcome == STROBE_RUN_ABORT_POWER);
    CHECK(strobe_gated_cancel(&r) == STROBE_GATE_CLOSED);       // repeated: the same
    CHECK(!gpio_get(PIN_STROBE_PULSE) && g_pin[PIN_STROBE_PULSE].fn == GPIO_FUNC_SIO);
    return true;
}

// The review's reproduction: the power FSM's safe-off lands during the first
// pulse. Image 1.4 then reported "never opened".
static bool gate_abort_first_pulse(void) {
    boot();
    if (!arm()) return false;
    at_first_pulse();
    CHECK(gpio_get(PIN_STROBE_PULSE));                          // GPIO25 is high
    strobe_safe_off();
    if (!witnessed_stopped()) return false;
    CHECK(!gpio_get(PIN_STROBE_PULSE));                         // and dropped
    strobe_result_t r;
    CHECK(strobe_gated_cancel(&r) == STROBE_GATE_OPENED);
    CHECK(r.outcome == STROBE_RUN_ABORT_POWER);
    CHECK(strobe_gated_cancel(&r) == STROBE_GATE_OPENED);       // repeated: the same
    return true;
}

static bool gate_abort_between_pulses(void) {
    boot();
    if (!arm()) return false;
    between_pulses();
    strobe_safe_off();
    if (!witnessed_stopped()) return false;
    strobe_result_t r;
    CHECK(strobe_gated_cancel(&r) == STROBE_GATE_OPENED);
    return true;
}

static bool gate_after_completion(void) {
    boot();
    if (!arm()) return false;
    completed();
    strobe_result_t r;
    CHECK(strobe_gated_poll(&r) == STROBE_GATED_FINISHED);
    CHECK(r.outcome == STROBE_RUN_OK && r.gate == STROBE_GATE_OPENED);
    if (!witnessed_stopped()) return false;
    CHECK(strobe_gated_cancel(&r) == STROBE_GATE_OPENED);
    CHECK(r.outcome == STROBE_RUN_OK);                          // a late cancel changes nothing
    return true;
}

// A poll ends it first (rail down, the deadline, a fault), mid-pulse; the cancel
// that follows must report what the poll's stop saw, not infer from the outcome.
static bool gate_cancel_after_poll(void) {
    for (int how = 0; how < 3; how++) {
        boot();
        g_rails = true;
        g_fault = FAULT_NONE;
        if (!arm()) return false;
        at_first_pulse();
        if (how == 0) g_rails = false;
        if (how == 1) g_now_us += 10u * 1000u * 1000u;          // past the CPU deadline
        if (how == 2) g_fault = FAULT_INTERNAL;
        strobe_result_t r;
        CHECK(strobe_gated_poll(&r) == STROBE_GATED_FINISHED);
        if (!witnessed_stopped()) return false;
        CHECK(r.gate == STROBE_GATE_OPENED);
        CHECK(strobe_gated_cancel(&r) == STROBE_GATE_OPENED);
        CHECK(strobe_gated_cancel(&r) == STROBE_GATE_OPENED);
        CHECK(how != 0 || r.outcome == STROBE_RUN_ABORT_POWER);
        CHECK(how != 1 || r.outcome == STROBE_RUN_TIMEOUT);
        CHECK(how != 2 || r.outcome == STROBE_RUN_ABORT_FAULT);
    }
    // The control: a fault before FIRE, gate still shut.
    boot();
    g_rails = true;
    g_fault = FAULT_NONE;
    if (!arm()) return false;
    g_fault = FAULT_INTERNAL;
    strobe_result_t r;
    CHECK(strobe_gated_poll(&r) == STROBE_GATED_FINISHED);
    CHECK(r.gate == STROBE_GATE_CLOSED && r.outcome == STROBE_RUN_GATE_HELD);
    CHECK(strobe_gated_cancel(&r) == STROBE_GATE_CLOSED);
    return true;
}

// Cancelled while still running: held, cut, or done-but-not-polled.
static bool gate_cancel_active(void) {
    strobe_result_t r;
    boot();
    if (!arm()) return false;
    CHECK(strobe_gated_cancel(&r) == STROBE_GATE_CLOSED);
    CHECK(r.outcome == STROBE_RUN_GATE_HELD);
    if (!witnessed_stopped()) return false;

    boot();
    if (!arm()) return false;
    at_first_pulse();
    CHECK(strobe_gated_cancel(&r) == STROBE_GATE_OPENED);
    CHECK(r.outcome == STROBE_RUN_GATE_CUT);
    if (!witnessed_stopped()) return false;

    boot();
    if (!arm()) return false;
    completed();
    CHECK(strobe_gated_cancel(&r) == STROBE_GATE_OPENED);
    CHECK(r.outcome == STROBE_RUN_OK && r.irq_seen);
    return true;
}

// Any sign the burst ran counts as OPENED. IRQ0 up with the PC still on the gate
// should not happen (the burst raises IRQ0 only after running, and the arm clears
// it), so the read-back must not certify CLOSED from the PC alone if it ever does.
static bool gate_irq0_counts_as_opened(void) {
    boot();
    if (!arm()) return false;
    g_irq[0][0] = true;                                         // PC still on the gate
    strobe_safe_off();
    strobe_result_t r;
    CHECK(strobe_gated_cancel(&r) == STROBE_GATE_OPENED);
    return true;
}

static bool gate_unknown_when_never_armed(void) {
    boot();
    strobe_result_t r;
    CHECK(strobe_gated_cancel(&r) == STROBE_GATE_UNKNOWN);
    // A refused arm (the rail down) takes no read-back either.
    g_rails = false;
    CHECK(!strobe_gated_arm(&k_burst, 10000u, &r));
    CHECK(r.outcome == STROBE_RUN_REFUSED && r.gate == STROBE_GATE_UNKNOWN);
    CHECK(strobe_gated_cancel(&r) == STROBE_GATE_UNKNOWN);
    return true;
}

// ===========================================================================
// H7-F2 in shot.c: CAM_TIMEOUT is certified only by a gate seen CLOSED
// ===========================================================================

static shot_test_req_t shot_req(void) {
    shot_test_req_t q;
    memset(&q, 0, sizeof q);
    q.first_delay_us = 200u;
    q.timeout_us     = 1000u;
    q.strobe         = true;
    q.burst          = k_burst;
    return q;
}

// The camera engine reports a word (what cam_handshake.pio would push).
static void cam_word(uint32_t w) {
    sm_model_t *s = &g_sm[1][0];
    s->rx[(s->rx_head + s->rx_n) % 8u] = w;
    s->rx_n++;
}

static void run_shot_steps(void) { for (int i = 0; i < 6 && shot_busy(); i++) shot_step(); }

static bool shot_timeout_certified(void) {
    boot();
    shot_test_req_t q = shot_req();
    shot_result_t r;
    CHECK(shot_test_start(&q, &r));
    CHECK(g_sm[1][0].enabled && g_sm[0][0].pc == GATE);
    cam_word(CAM_WORD_TIMEOUT);
    run_shot_steps();
    CHECK(!shot_busy());
    CHECK(shot_last(&r));
    CHECK(r.end == SHOT_END_CAM_TIMEOUT);
    CHECK(r.gate == STROBE_GATE_CLOSED);
    CHECK(r.strobe.outcome == STROBE_RUN_GATE_HELD);
    CHECK(g_fault == FAULT_CAM_TIMEOUT);
    if (!witnessed_stopped()) return false;
    return true;
}

// The camera says TIMEOUT, but the strobe engine had run past its gate: not a
// clean timeout, and never "nothing fired".
static bool shot_timeout_contradicted(void) {
    boot();
    shot_test_req_t q = shot_req();
    shot_result_t r;
    CHECK(shot_test_start(&q, &r));
    at_first_pulse();
    cam_word(CAM_WORD_TIMEOUT);
    run_shot_steps();
    CHECK(shot_last(&r));
    CHECK(r.end == SHOT_END_ENGINE);
    CHECK(r.gate == STROBE_GATE_OPENED);
    CHECK(g_fault == FAULT_INTERNAL);
    CHECK(!gpio_get(PIN_STROBE_PULSE) && !gpio_get(PIN_CAM_TRIGGER));
    return true;
}

// The review's reachable scenario: a physical shutdown press is serviced during the
// burst. The power FSM runs strobe_safe_off() then camera_safe_off(); the next
// shot_step() logs the abort -- and must say the gate OPENED.
static bool shot_power_abort_mid_burst(void) {
    boot();
    shot_test_req_t q = shot_req();
    shot_result_t r;
    CHECK(shot_test_start(&q, &r));
    cam_word(5997u - 3u);                                       // both strobes seen, early
    shot_step();
    CHECK(shot_state() == SHOT_FIRING);
    at_first_pulse();
    strobe_safe_off();                                          // power_fsm.c, in this order
    camera_safe_off();
    run_shot_steps();
    CHECK(shot_last(&r));
    CHECK(r.end == SHOT_END_ABORTED);
    CHECK(r.gate == STROBE_GATE_OPENED);
    CHECK(r.strobe.outcome == STROBE_RUN_ABORT_POWER);
    CHECK(!gpio_get(PIN_STROBE_PULSE) && !gpio_get(PIN_CAM_TRIGGER));
    return true;
}

// And before FIRE, the same abort is proven CLOSED.
static bool shot_power_abort_before_fire(void) {
    boot();
    shot_test_req_t q = shot_req();
    shot_result_t r;
    CHECK(shot_test_start(&q, &r));
    strobe_safe_off();
    camera_safe_off();
    run_shot_steps();
    CHECK(shot_last(&r));
    CHECK(r.end == SHOT_END_ABORTED);
    CHECK(r.gate == STROBE_GATE_CLOSED);
    return true;
}

static bool shot_fired(void) {
    boot();
    shot_test_req_t q = shot_req();
    shot_result_t r;
    CHECK(shot_test_start(&q, &r));
    cam_word(5997u - 3u);
    shot_step();
    completed();
    shot_step();
    cam_word(CAM_WORD_END);
    run_shot_steps();
    CHECK(shot_last(&r));
    CHECK(r.end == SHOT_END_FIRED);
    CHECK(r.pulses_fired == 3u);
    CHECK(r.strobe.outcome == STROBE_RUN_OK && r.gate == STROBE_GATE_OPENED);
    CHECK(g_fault == FAULT_NONE);
    return true;
}

// ===========================================================================
// H7-F3: the camera trigger by hand
// ===========================================================================

static bool gpio_cmd(const char *pin, const char *level) {
    char a0[] = "gpio", a1[8], a2[8];
    snprintf(a1, sizeof a1, "%s", pin);
    snprintf(a2, sizeof a2, "%s", level);
    char *argv[] = { a0, a1, a2 };
    cmd_gpio(3, argv);
    return true;
}

static bool trigger_high(void) {
    return g_pin[PIN_CAM_TRIGGER].fn == GPIO_FUNC_SIO && g_pin[PIN_CAM_TRIGGER].dir_out &&
           g_pin[PIN_CAM_TRIGGER].out;
}

// The review's reproduction: STANDBY (rail down), `gpio 10 1`.
static bool manual_trigger_rail_down(void) {
    boot();
    g_rails = false;
    CHECK(camera_manual_trigger(true) == CAM_START_RAIL);
    CHECK(!trigger_high());
    gpio_cmd("10", "1");
    CHECK(!trigger_high() && !gpio_get(PIN_CAM_TRIGGER));
    return true;
}

static bool manual_trigger_fault(void) {
    boot();
    g_fault = FAULT_CAM_TIMEOUT;
    CHECK(camera_manual_trigger(true) == CAM_START_FAULT);
    gpio_cmd("10", "1");
    CHECK(!trigger_high());
    return true;
}

static bool manual_trigger_busy(void) {
    boot();
    shot_test_req_t q = shot_req();
    q.strobe = false;
    q.hold_us = 100u;
    shot_result_t r;
    CHECK(shot_test_start(&q, &r));                             // a handshake running
    CHECK(camera_manual_trigger(true) == CAM_START_BUSY);
    gpio_cmd("10", "1");
    CHECK(g_pin[PIN_CAM_TRIGGER].fn == GPIO_FUNC_PIO1);         // still the engine's
    return true;
}

// The 7a static check, rail up: high, the strobes follow (the loopback), low; and a
// manual high is dropped by the next rail-down.
static bool manual_trigger_rail_up(void) {
    boot();
    gpio_cmd("10", "1");
    CHECK(trigger_high() && gpio_get(PIN_CAM_TRIGGER));
    gpio_cmd("10", "0");
    CHECK(!gpio_get(PIN_CAM_TRIGGER));
    gpio_cmd("10", "1");
    CHECK(trigger_high());
    camera_safe_off();                                          // what every rail-down runs
    CHECK(!gpio_get(PIN_CAM_TRIGGER));
    // Low is always admitted, rail down or not.
    g_rails = false;
    CHECK(camera_manual_trigger(false) == CAM_START_OK);
    CHECK(!gpio_get(PIN_CAM_TRIGGER));
    // Another writable pin is unaffected: still a plain write.
    gpio_cmd("18", "1");
    CHECK(g_pin[PIN_LED_RED].out);
    return true;
}

// ===========================================================================
// Live mode and a Pi (image 1.8, owner 2026-11-12): the real strobe_live_arm()
// and strobe_live_service() in strobe.c, not just the policy.
// ===========================================================================

static void set_pi(bool fsm_present, bool sensed) { g_pi = fsm_present; g_sensed = sensed; }

static strobe_refusal_t exit_refusal(void) {
    strobe_info_t i;
    strobe_info(&i);
    return i.live_exit == STROBE_LIVE_EXIT_CONDITION ? i.live_exit_refusal : STROBE_OK;
}

// No Pi: live mode arms the watchdog, needs it, and gives it back on exit.
static bool live_no_pi_arms_watchdog(void) {
    boot();
    set_pi(false, false); g_wdog = false;
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(strobe_live_armed());
    CHECK(g_wdog);
    CHECK(strobe_live_watchdog_required());
    strobe_live_service();
    CHECK(strobe_live_armed());
    strobe_live_disarm();
    CHECK(!strobe_live_armed());
    CHECK(!g_wdog);
    CHECK(!strobe_live_watchdog_required());
    return true;
}

// A Pi powered: live mode arms, leaves the watchdog off, and holds without it.
static bool live_with_pi_no_watchdog(void) {
    boot();
    set_pi(true, true); g_wdog = false;
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(strobe_live_armed());
    CHECK(!g_wdog);
    CHECK(!strobe_live_watchdog_required());
    strobe_live_service();
    CHECK(strobe_live_armed());
    CHECK(!g_wdog);
    strobe_live_disarm();
    CHECK(!strobe_live_armed());
    return true;
}

// A Pi that goes away (both observations agree) ends the session, gate at 0.
static bool live_pi_goes_away(void) {
    boot();
    set_pi(true, true); g_wdog = false;
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(strobe_gate_set(1u) == STROBE_OK);
    set_pi(false, false);
    strobe_live_service();
    CHECK(!strobe_live_armed());
    CHECK(exit_refusal() == STROBE_REFUSE_PI_CHANGED);
    CHECK(strobe_gate_level() == 0u);
    CHECK(!g_wdog);
    return true;
}

// V18-F1, A: the 3V3 sensed now is gone while the FSM still classifies a Pi.
// UNSETTLED ends the session at once, gate at 0, and live mode will not re-arm.
static bool live_pi_sense_lost(void) {
    boot();
    set_pi(true, true); g_wdog = false;
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(strobe_gate_set(1u) == STROBE_OK);
    set_pi(true, false);
    strobe_live_service();
    CHECK(!strobe_live_armed());
    CHECK(exit_refusal() == STROBE_REFUSE_PI_UNSETTLED);
    CHECK(strobe_gate_level() == 0u);
    CHECK(!g_wdog);
    CHECK(strobe_live_arm() == STROBE_REFUSE_PI_UNSETTLED);
    CHECK(!g_wdog);
    return true;
}

// A Pi that appears (both agree) in a session armed without one ends it, and the
// watchdog live mode armed is given back.
static bool live_pi_appears(void) {
    boot();
    set_pi(false, false); g_wdog = false;
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(g_wdog);
    set_pi(true, true);
    strobe_live_service();
    CHECK(!strobe_live_armed());
    CHECK(exit_refusal() == STROBE_REFUSE_PI_CHANGED);
    CHECK(!g_wdog);
    return true;
}

// V18-F1, B: a late Pi's 3V3 is sensed before the FSM's debounce promotes it.
// A no-Pi session ends at once and hands its watchdog back: never a powered Pi
// under live mode's watchdog.
static bool live_pi_sense_rises(void) {
    boot();
    set_pi(false, false); g_wdog = false;
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(g_wdog);
    CHECK(strobe_gate_set(1u) == STROBE_OK);
    set_pi(false, true);
    strobe_live_service();
    CHECK(!strobe_live_armed());
    CHECK(exit_refusal() == STROBE_REFUSE_PI_UNSETTLED);
    CHECK(strobe_gate_level() == 0u);
    CHECK(!g_wdog);
    return true;
}

// Never arm on a guess: either disagreement refuses, and nothing arms the watchdog.
static bool live_arm_refused_unsettled(void) {
    boot();
    g_wdog = false;
    set_pi(false, true);
    CHECK(strobe_live_arm() == STROBE_REFUSE_PI_UNSETTLED);
    CHECK(!strobe_live_armed());
    CHECK(!g_wdog);
    set_pi(true, false);
    CHECK(strobe_live_arm() == STROBE_REFUSE_PI_UNSETTLED);
    CHECK(!strobe_live_armed());
    CHECK(!g_wdog);
    return true;
}

// The operator's own `wdog on` before arming with a Pi stays theirs: live mode
// neither claims it nor turns it off.
static bool live_with_pi_keeps_operator_watchdog(void) {
    boot();
    set_pi(true, true); g_wdog = true;
    CHECK(strobe_live_arm() == STROBE_OK);
    CHECK(g_wdog);
    strobe_live_disarm();
    CHECK(g_wdog);
    return true;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(n) { #n, n }
        TEST(gate_abort_before_fire), TEST(gate_abort_first_pulse),
        TEST(gate_abort_between_pulses), TEST(gate_after_completion),
        TEST(gate_cancel_after_poll), TEST(gate_cancel_active),
        TEST(gate_irq0_counts_as_opened), TEST(gate_unknown_when_never_armed),
        TEST(shot_timeout_certified), TEST(shot_timeout_contradicted),
        TEST(shot_power_abort_mid_burst), TEST(shot_power_abort_before_fire),
        TEST(shot_fired),
        TEST(manual_trigger_rail_down), TEST(manual_trigger_fault),
        TEST(manual_trigger_busy), TEST(manual_trigger_rail_up),
        TEST(live_no_pi_arms_watchdog), TEST(live_with_pi_no_watchdog),
        TEST(live_pi_goes_away), TEST(live_pi_appears),
        TEST(live_pi_sense_lost), TEST(live_pi_sense_rises),
        TEST(live_arm_refused_unsettled),
        TEST(live_with_pi_keeps_operator_watchdog),
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
