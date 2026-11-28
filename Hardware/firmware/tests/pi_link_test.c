// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// pi_link_test.c -- the UNCHANGED src/pi_link.c, with the real decoder, settle
// rule and ticket book (link_frame.c, link_cli.c), against stand-ins for the
// clock, the UART and the DMA ring (link_standin/). Image 1.15, review finding
// V114-F2.
//
// The 1.14 review compiled pi_link_service(), cli_request() and cancel_waiting()
// the same way and showed the board running a command whose own CLI_ABORT was
// already in RAM: the superloop's pass released only the snapshot taken one pass
// earlier, which ended at the CLI, and a USB command between the two passes left
// the abort above it. link_test.c's look-ahead case had given the scanner both
// frames at once, so it could not see that.
//
// The ring here is the real one in pi_link.c (s_rx). The test advances the DMA's
// write index as bytes "arrive", and each byte lands in the ring LAND_US after
// that -- the lag LINK_RX_SETTLE_US covers -- so a look-ahead that reads up to the
// index without waiting reads bytes that are not there yet. The clock moves on
// 1 us at every read of it.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "link_standin.h"
#if defined(_MSC_VER)
// s_rx's alignment: the board's DMA ring wrap needs it; ring_w() masks either way.
#define __attribute__(x)
#endif
#include "pi_link.c"

static const char *test_name;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, test_name, #c); \
    return false; } } while (0)

// --- the clock, and the DMA's late landing --------------------------------------
#define LAND_US 10u
static uint64_t g_us = 1000u;
typedef struct { uint32_t idx; uint8_t b; uint64_t at; } pend_t;
static pend_t g_pend[4096];
static size_t g_npend;

static void land(void) {
    size_t k = 0u;
    for (size_t i = 0; i < g_npend; i++) {
        if (g_pend[i].at <= g_us) s_rx[g_pend[i].idx] = g_pend[i].b;
        else g_pend[k++] = g_pend[i];
    }
    g_npend = k;
}

uint64_t        time_us_64(void) { g_us += 1u; land(); return g_us; }
absolute_time_t get_absolute_time(void) { return time_us_64(); }
uint32_t        to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000u); }
static void     pause_us(uint64_t us) { g_us += us; land(); }

// --- the DMA ring's write index -----------------------------------------------------
static dma_hw_t g_dma;
dma_hw_t *const dma_hw = &g_dma;
static uint32_t g_w;                          // the next ring slot the DMA writes

static void arrive(const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        g_pend[g_npend++] = (pend_t){ g_w, b[i], g_us + LAND_US };
        g_w = (g_w + 1u) & (RX_RING - 1u);
    }
    g_dma.ch[0].write_addr = (uintptr_t)&s_rx[g_w];
}

int  dma_claim_unused_channel(bool required) { (void)required; return 0; }
dma_channel_config dma_channel_get_default_config(uint ch) { (void)ch; dma_channel_config c = { 0u }; return c; }
void channel_config_set_transfer_data_size(dma_channel_config *c, enum dma_channel_transfer_size s) { (void)c; (void)s; }
void channel_config_set_read_increment(dma_channel_config *c, bool incr) { (void)c; (void)incr; }
void channel_config_set_write_increment(dma_channel_config *c, bool incr) { (void)c; (void)incr; }
void channel_config_set_ring(dma_channel_config *c, bool write, uint bits) { (void)c; (void)write; (void)bits; }
void channel_config_set_dreq(dma_channel_config *c, uint dreq) { (void)c; (void)dreq; }
void dma_channel_set_config(uint ch, const dma_channel_config *c, bool trigger) { (void)ch; (void)c; (void)trigger; }
void dma_channel_set_read_addr(uint ch, const volatile void *a, bool trigger) { (void)ch; (void)a; (void)trigger; }
void dma_channel_set_write_addr(uint ch, volatile void *a, bool trigger) {
    (void)trigger;
    g_dma.ch[ch].write_addr = (uintptr_t)a;
    g_w = 0u;
}
void dma_channel_set_trans_count(uint ch, uint32_t n, bool trigger) { (void)ch; (void)n; (void)trigger; }
uint32_t dma_encode_endless_transfer_count(void) { return 0xF0000000u; }

// --- the UART: what the board sends ---------------------------------------------------
struct uart_inst { int unused; };
static struct uart_inst g_uart1;
uart_inst_t *const standin_uart1 = &g_uart1;
static uart_hw_t g_uart_hw;
static uint8_t g_tx[65536];
static size_t  g_ntx;
uint uart_init(uart_inst_t *u, uint baud) { (void)u; return baud; }
void uart_set_format(uart_inst_t *u, uint d, uint s, int p) { (void)u; (void)d; (void)s; (void)p; }
void uart_set_hw_flow(uart_inst_t *u, bool cts, bool rts) { (void)u; (void)cts; (void)rts; }
void uart_set_fifo_enabled(uart_inst_t *u, bool on) { (void)u; (void)on; }
uint uart_get_dreq_num(uart_inst_t *u, bool tx) { (void)u; (void)tx; return 0u; }
uart_hw_t *uart_get_hw(uart_inst_t *u) { (void)u; return &g_uart_hw; }
bool uart_is_writable(uart_inst_t *u) { (void)u; return true; }
void uart_putc_raw(uart_inst_t *u, char c) { (void)u; if (g_ntx < sizeof g_tx) g_tx[g_ntx++] = (uint8_t)c; }

// --- the rest of the board ------------------------------------------------------------
void stdio_set_driver_enabled(stdio_driver_t *d, bool on) { (void)d; (void)on; }
void stdio_flush(void) { }

static const uint32_t *g_rand;
static size_t g_nrand;
uint32_t get_rand_32(void) { return g_nrand ? (g_nrand--, *g_rand++) : 0x5EED0001u; }

static unsigned g_dispatched;
static char     g_ran[128];
cli_remote_t cli_run_remote(char *line) {
    g_dispatched++;
    snprintf(g_ran, sizeof g_ran, "%s", line);
    link_out_chars("ran\n", 4);                // what printf would hand the link's driver
    return CLI_REMOTE_RAN;
}
void cli_prompt_redraw(void) { }
bool safe_state_pi_uart_connected(void) { return true; }
size_t link_proto_handle(uint8_t type, const uint8_t *req, size_t req_len, uint8_t *rsp, size_t cap) {
    (void)type; (void)req; (void)req_len; (void)rsp; (void)cap;
    return 0u;
}

static bool g_pending, g_taken;
void pitrac_abort_clear(void) { g_pending = g_taken = false; }
void pitrac_abort_source(bool (*poll)(void)) { (void)poll; }
bool pitrac_abort_pending(void) { return g_pending; }
bool pitrac_abort_was_taken(void) { return g_taken; }

// --- the Pi's side ------------------------------------------------------------------
static uint8_t g_seq = 0x40u;

static uint8_t send(uint8_t type, const uint8_t *p, size_t n) {
    uint8_t f[LINK_MAX_FRAME];
    const uint8_t seq = g_seq++;
    arrive(f, link_frame_encode(f, sizeof f, type, seq, p, n));
    return seq;
}

static void put32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t send_cli(uint32_t session, uint32_t ticket, const char *line) {
    uint8_t p[LINK_CLI_ID_BYTES + LINK_CLI_LINE_MAX];
    put32le(p, session);
    put32le(&p[4], ticket);
    const size_t n = strlen(line);
    memcpy(&p[LINK_CLI_ID_BYTES], line, n);
    return send(LINK_CLI, p, LINK_CLI_ID_BYTES + n);
}

static uint8_t send_abort(uint32_t session, uint32_t ticket) {
    uint8_t p[LINK_CLI_ID_BYTES];
    put32le(p, session);
    put32le(&p[4], ticket);
    return send(LINK_CLI_ABORT, p, sizeof p);
}

// The board's answers, decoded from what it sent.
typedef struct { uint8_t type, seq; size_t len; uint8_t p[LINK_MAX_PAYLOAD]; } frame_t;
static frame_t    g_ans[64];
static size_t     g_nans, g_tx_read;
static link_dec_t g_rxdec;

static const frame_t *answer(uint8_t type, uint8_t seq) {
    while (g_tx_read < g_ntx) {
        if (link_dec_push(&g_rxdec, g_tx[g_tx_read++], 0u) != LINK_DEC_FRAME || g_nans >= 64u) continue;
        frame_t *f = &g_ans[g_nans++];
        f->type = g_rxdec.type;
        f->seq  = g_rxdec.seq;
        f->len  = g_rxdec.len;
        memcpy(f->p, g_rxdec.payload, g_rxdec.len);
    }
    for (size_t i = g_nans; i-- > 0u; )
        if (g_ans[i].type == (uint8_t)(type | LINK_RESPONSE) && g_ans[i].seq == seq) return &g_ans[i];
    return NULL;
}

// One superloop pass, some time after the last.
static void pass(void) { pause_us(50u); pi_link_service(); }

static void boot(void) {
    link_dec_init(&g_rxdec);
    pi_link_init();
    pass();
    pass();
}

static uint32_t g_session;

// A ticket, as the Pi's tool gets one. 0 on failure.
static uint32_t begin(void) {
    const uint8_t seq = send(LINK_CLI_BEGIN, NULL, 0u);
    for (int i = 0; i < 4; i++) pass();
    const frame_t *a = answer(LINK_CLI_BEGIN, seq);
    if (!a || a->len != LINK_CLI_BEGIN_BYTES || a->p[0] != 0u) return 0u;
    g_session = get32le(&a->p[1]);
    return get32le(&a->p[5]);
}

// The result frame's fields: protocol 4 names the session as well as the ticket
// (image 1.16, review finding V115-F4).
static bool result_is(const frame_t *a, uint8_t status, uint8_t flags, uint32_t session,
                      uint32_t ticket) {
    return a && a->len == LINK_CLI_RESULT_BYTES && a->p[0] == status && a->p[1] == flags &&
           get32le(&a->p[2]) == session && get32le(&a->p[6]) == ticket;
}

// ---------------------------------------------------------------------------

// The review's case. The CLI arrives, and the superloop's next pass snapshots
// it (releasing only what came before). A USB command then holds the loop for
// about a second, and the CLI's own CLI_ABORT arrives and lands meanwhile. The
// pass after it releases the CLI. 1.14 dispatched it (count 1, cancelled 0), and
// answered the abort from the record of a command that had run.
static bool cancel_in_ram_before_dispatch(void) {
    boot();
    const uint32_t t = begin();
    CHECK(t == 1u);
    const uint8_t cseq = send_cli(g_session, t, "stat");
    pass();                                               // the snapshot now ends at the CLI
    CHECK(answer(LINK_CLI, cseq) == NULL && g_dispatched == 0u);
    const uint8_t aseq = send_abort(g_session, t);
    pause_us(999000u);                                    // the USB command
    pi_link_service();
    CHECK(g_dispatched == 0u);
    // Cancelled by the abort found behind it, then answered from that record:
    // CANCELLED, with REPEAT (as in 1.14 when its look-ahead did find the abort).
    CHECK(result_is(answer(LINK_CLI, cseq), 0u, LINK_CLI_F_CANCELLED | LINK_CLI_F_REPEAT, g_session, t));
    for (int i = 0; i < 4; i++) pass();
    CHECK(result_is(answer(LINK_CLI_ABORT, aseq), 0u, LINK_CLI_F_CANCELLED | LINK_CLI_F_REPEAT, g_session, t));
    CHECK(g_dispatched == 0u && s_cli_cancelled == 1u && s_cli_runs == 0u);
    return true;
}

// The settle rule is kept: the abort's bytes are counted by the DMA's index but
// land 10 us later, just as the pass that releases the CLI runs. A look-ahead
// that read up to the index at once would find zeros there and run the command.
static bool cancel_still_landing(void) {
    boot();
    const uint32_t t = begin();
    const uint8_t cseq = send_cli(g_session, t, "strobe pulse 20");
    pass();
    pause_us(50u);
    (void)send_abort(g_session, t);                       // index advanced; bytes not yet in RAM
    pi_link_service();
    CHECK(g_dispatched == 0u);
    CHECK(result_is(answer(LINK_CLI, cseq), 0u, LINK_CLI_F_CANCELLED | LINK_CLI_F_REPEAT, g_session, t));
    return true;
}

// The same with the abort split across the ring's end: the look-ahead reads it
// in two pieces through one decoder.
static bool cancel_across_the_wrap(void) {
    boot();
    const uint32_t t = begin();
    // Walk the ring on to 25 bytes short of its end with junk the decoder drops.
    static uint8_t junk[RX_RING];
    const uint32_t to_go = (RX_RING - 25u - g_w) & (RX_RING - 1u);
    arrive(junk, to_go);
    for (int i = 0; i < 4; i++) pass();
    CHECK(s_rd == RX_RING - 25u);
    const uint8_t cseq = send_cli(g_session, t, "stat");   // 18 bytes: ends 7 short of the end
    pass();
    (void)send_abort(g_session, t);                         // 14 bytes: 7 before the wrap, 7 after
    CHECK(g_w == 7u);
    pause_us(999000u);
    pi_link_service();
    CHECK(g_dispatched == 0u);
    CHECK(result_is(answer(LINK_CLI, cseq), 0u, LINK_CLI_F_CANCELLED | LINK_CLI_F_REPEAT, g_session, t));
    return true;
}

// Without an abort the command runs, once, and its answer says so.
static bool runs_without_abort(void) {
    boot();
    const uint32_t t = begin();
    const uint8_t cseq = send_cli(g_session, t, "stat");
    for (int i = 0; i < 4; i++) pass();
    CHECK(g_dispatched == 1u && !strcmp(g_ran, "stat"));
    CHECK(result_is(answer(LINK_CLI, cseq), 0u, 0u, g_session, t));
    // Its output frame names the session and the ticket, then the offset (V115-F4),
    // and the answer counts and checks the text.
    const frame_t *o = answer(LINK_CLI_OUT, cseq);
    CHECK(o && o->len == LINK_CLI_OUT_HDR + 4u && get32le(&o->p[0]) == g_session);
    CHECK(get32le(&o->p[4]) == t && get32le(&o->p[8]) == 0u && !memcmp(&o->p[12], "ran\n", 4));
    const frame_t *r = answer(LINK_CLI, cseq);
    CHECK(get32le(&r->p[10]) == 4u &&
          (uint16_t)(r->p[14] | (r->p[15] << 8)) == link_crc16((const uint8_t *)"ran\n", 4u, LINK_CRC_INIT));
    const uint8_t again = send_cli(g_session, t, "stat");   // the resend: from the record
    for (int i = 0; i < 4; i++) pass();
    CHECK(g_dispatched == 1u);
    CHECK(result_is(answer(LINK_CLI, again), 0u, LINK_CLI_F_REPEAT, g_session, t));
    return true;
}

// V114-N3: when a session's last number is issued, the next CLI_BEGIN starts a
// new session -- a new random id, never 0 and never the old one -- and the old
// session's tickets are refused rather than taken for the new ones.
static bool session_renewed_before_wrap(void) {
    static const uint32_t script[] = { 0x5EED0001u, 0u, 0x5EED0001u, 0x0BADCAFEu };
    g_rand = script;
    g_nrand = sizeof script / sizeof script[0];
    boot();
    CHECK(s_book.session == 0x5EED0001u);
    s_book.issued = UINT32_MAX - 1u;
    const uint32_t last = begin();
    CHECK(last == UINT32_MAX && g_session == 0x5EED0001u);
    const uint32_t next = begin();
    CHECK(g_session == 0x0BADCAFEu && next == 1u);
    uint8_t seq = send_cli(0x5EED0001u, last, "stat");     // the old session's last ticket
    for (int i = 0; i < 4; i++) pass();
    CHECK(g_dispatched == 0u && result_is(answer(LINK_CLI, seq), LINK_E_SESSION, 0u, 0x5EED0001u, last));
    seq = send_cli(0x0BADCAFEu, next, "id");
    for (int i = 0; i < 4; i++) pass();
    CHECK(g_dispatched == 1u && !strcmp(g_ran, "id"));
    CHECK(result_is(answer(LINK_CLI, seq), 0u, 0u, 0x0BADCAFEu, next));
    return true;
}

int main(int argc, char **argv) {
    static const struct { const char *name; bool (*run)(void); } cases[] = {
#define TEST(n) { #n, n }
        TEST(cancel_in_ram_before_dispatch), TEST(cancel_still_landing),
        TEST(cancel_across_the_wrap), TEST(runs_without_abort),
        TEST(session_renewed_before_wrap),
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
