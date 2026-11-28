// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// pi_link.c -- see pi_link.h.

#include "pi_link.h"
#include "link_frame.h"
#include "link_proto.h"
#include "link_cli.h"
#include "safe_state.h"
#include "service.h"
#include "cli.h"
#include "board.h"

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "pico/rand.h"
#include "hardware/uart.h"
#include "hardware/dma.h"

#include <stdio.h>
#include <string.h>

#define LINK_UART      uart1
#define RX_RING_BITS   11u
#define RX_RING        (1u << RX_RING_BITS)      // 2048 bytes: ~22 ms at 921600 baud

// The DMA wraps its write address on this boundary, so the ring must be aligned
// to its own size.
static uint8_t s_rx[RX_RING] __attribute__((aligned(RX_RING)));
static int            s_dma = -1;
static bool           s_ready, s_was_connected;
static uint32_t       s_rd;                       // next ring index to read
static link_rx_snap_t s_snap;
static link_dec_t     s_dec;
static uint8_t        s_tx[LINK_MAX_FRAME];
static size_t         s_tx_len, s_tx_at;
static uint8_t        s_rsp[LINK_MAX_PAYLOAD];
static uint32_t       s_baud, s_connects, s_rx_bytes, s_tx_bytes, s_requests, s_unknown, s_last_ms;
static uint8_t        s_last_type;

// The Pi's CLI (protocol 4). Static, not on the stack: a command's own call
// chain runs on top of these, and printf -> out_chars -> a frame is deep already.
static link_cli_book_t s_book;                        // the tickets (link_cli.h)
static link_cli_out_t  s_cli_out;                     // output not yet sent
static char            s_cli_line[LINK_CLI_LINE_MAX + 1u];   // the request, split by the CLI
static uint8_t         s_cli_frame[LINK_MAX_FRAME];
static uint8_t         s_cli_chunk[LINK_CLI_OUT_HDR + LINK_CLI_OUT_CHUNK];
static link_dec_t      s_ahead;                       // the look-ahead's own decoder
static bool            s_cli_running;
static uint8_t         s_cli_seq;
static uint32_t        s_cli_ticket;
static uint32_t        s_cli_tx_ms;                   // when a CLI frame last went out
static uint32_t        s_cli_runs, s_cli_rule, s_cli_repeats, s_cli_cancelled, s_cli_refused;
static uint32_t        s_cli_stop_asked, s_cli_stopped, s_cli_dropped;

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// The next ring slot the DMA will write.
static uint32_t ring_w(void) {
    return ((uint32_t)(uintptr_t)dma_hw->ch[s_dma].write_addr - (uint32_t)(uintptr_t)s_rx)
           & (RX_RING - 1u);
}

// A new session id: random, never 0 ("none"), never the one it replaces.
static uint32_t new_session(uint32_t old) {
    uint32_t s;
    do { s = get_rand_32(); } while (s == 0u || s == old);
    return s;
}

void pi_link_init(void) {
    // A new id for every boot, so a ticket from before a reset is never taken
    // for one of this session's (link_cli.h).
    link_cli_book_init(&s_book, new_session(0u));

    // Configures the block only; the pins stay as safe_state_init() left them.
    s_baud = uart_init(LINK_UART, LINK_BAUD);
    uart_set_format(LINK_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(LINK_UART, false, false);
    uart_set_fifo_enabled(LINK_UART, true);
    link_dec_init(&s_dec);

    s_dma = dma_claim_unused_channel(false);
    if (s_dma < 0) return;                        // `link` reports it not ready
    dma_channel_config c = dma_channel_get_default_config((uint)s_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, RX_RING_BITS);   // wrap the WRITE address
    channel_config_set_dreq(&c, uart_get_dreq_num(LINK_UART, false));
    dma_channel_set_config((uint)s_dma, &c, false);
    dma_channel_set_read_addr((uint)s_dma, &uart_get_hw(LINK_UART)->dr, false);
    dma_channel_set_write_addr((uint)s_dma, s_rx, false);
    // ENDLESS: the channel never completes, so it never needs re-arming.
    dma_channel_set_trans_count((uint)s_dma, dma_encode_endless_transfer_count(), true);
    s_rd = 0u;
    s_ready = true;
}

static void tx_drain(void) {
    while (s_tx_at < s_tx_len && uart_is_writable(LINK_UART)) {
        uart_putc_raw(LINK_UART, (char)s_tx[s_tx_at++]);
        s_tx_bytes++;
    }
    if (s_tx_at >= s_tx_len) s_tx_len = s_tx_at = 0u;
}

// ---------------------------------------------------------------------------
// The Pi's CLI.

// One frame, written out now (blocking on the UART FIFO: 921600 baud, ~2.8 ms
// for a full frame). Used only while a command runs, when the one-exchange s_tx
// buffer above is idle: its output, its keepalives and its answer.
static void send_frame_now(uint8_t type, uint8_t seq, const uint8_t *p, size_t n) {
    size_t len = link_frame_encode(s_cli_frame, sizeof(s_cli_frame), type, seq, p, n);
    for (size_t i = 0; i < len; i++) uart_putc_raw(LINK_UART, (char)s_cli_frame[i]);
    s_tx_bytes += (uint32_t)len;
    s_cli_tx_ms = to_ms_since_boot(get_absolute_time());
}

// A CLI_OUT: the session and ticket (protocol 4: both, image 1.16, V115-F4), this
// text's offset in the whole output, the text.
static void cli_emit(uint32_t offset, const uint8_t *p, size_t n, void *ctx) {
    (void)ctx;
    put32(&s_cli_chunk[0], s_book.session);
    put32(&s_cli_chunk[4], s_cli_ticket);
    put32(&s_cli_chunk[8], offset);
    memcpy(&s_cli_chunk[LINK_CLI_OUT_HDR], p, n);
    send_frame_now((uint8_t)(LINK_CLI_OUT | LINK_RESPONSE), s_cli_seq, s_cli_chunk,
                   LINK_CLI_OUT_HDR + n);
}

// The stdio driver: enabled only while a Pi command runs, so printf reaches both
// the USB console and the Pi. A line's end sends what is held, so the Pi sees the
// output a line at a time rather than a chunk at a time.
static void link_out_chars(const char *buf, int len) {
    if (!s_cli_running || len <= 0) return;
    link_cli_out_put(&s_cli_out, buf, (size_t)len, cli_emit, NULL);
    if (memchr(buf, '\n', (size_t)len)) link_cli_out_flush(&s_cli_out, false, cli_emit, NULL);
}

static void link_out_flush(void) {
    if (s_cli_running) link_cli_out_flush(&s_cli_out, false, cli_emit, NULL);
}

static stdio_driver_t s_link_stdio = {
    .out_chars = link_out_chars,
    .out_flush = link_out_flush,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = false,          // the Pi gets '\n', as printf wrote it
#endif
};

// pitrac_abort_source() while a Pi command runs: called from the yield path
// only. Reads what has arrived -- this ticket's CLI_ABORT asks the command to
// stop, anything else is dropped (link_cli_scan()) -- and sends a keepalive if
// nothing has gone out for LINK_CLI_KEEPALIVE_MS.
static bool cli_poll(void) {
    bool stop = false;
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t upto;
    if (link_rx_settled(&s_snap, ring_w(), time_us_64(), &upto)) {
        if (!safe_state_pi_uart_connected()) {
            s_rd = upto;                              // released: not listening
        } else {
            while (s_rd != upto) {
                const uint8_t b = s_rx[s_rd];
                s_rd = (s_rd + 1u) & (RX_RING - 1u);
                s_rx_bytes++;
                if (link_cli_scan(&s_dec, &b, 1u, now, s_book.session, s_cli_ticket, &s_cli_dropped))
                    stop = true;
            }
        }
    }
    if ((uint32_t)(now - s_cli_tx_ms) >= LINK_CLI_KEEPALIVE_MS)
        link_cli_out_flush(&s_cli_out, true, cli_emit, NULL);   // held text, or an empty frame
    return stop;
}

// Everything received up to NOW, settled: the DMA's write index read now, then
// LINK_RX_SETTLE_US waited out, so every byte below it has landed in the ring
// (link_frame.h). Image 1.15, review finding V114-F2: the look-ahead used to stop
// at the `upto` this pass released, which is the write index of the PREVIOUS
// pass. A USB command between the two passes leaves everything that arrived
// meanwhile above it -- a CLI and its own CLI_ABORT both in RAM, and the
// look-ahead saw only the CLI, which then ran. ~20 us per CLI request.
static uint32_t ring_settled_now(void) {
    const uint32_t w  = ring_w();
    const uint64_t t0 = time_us_64();
    while (time_us_64() - t0 < LINK_RX_SETTLE_US) tight_loop_contents();
    return w;
}

// Is this request's own CLI_ABORT already in the ring, after it (s_rd..upto)?
// The ring wraps, so it is read in up to two pieces through one decoder.
static bool cancel_waiting(uint32_t session, uint32_t ticket, uint32_t upto, uint32_t now) {
    link_dec_init(&s_ahead);
    uint32_t i = s_rd;
    while (i != upto) {
        const uint32_t end = (upto > i) ? upto : RX_RING;
        if (link_cli_abort_ahead(&s_ahead, &s_rx[i], end - i, now, session, ticket)) return true;
        i = end & (RX_RING - 1u);
    }
    return false;
}

// Run one CLI request that the book has released (link_cli_idle()): its output
// streamed, then its answer. `line` points into s_dec, which cli_poll() reuses
// while the command runs: copied out first.
static void run_cli(uint8_t seq, uint32_t ticket, const uint8_t *line, size_t n) {
    memcpy(s_cli_line, line, n);
    s_cli_line[n] = '\0';

    // On the USB console first, before the Pi's output is switched on: there only.
    printf("\n[pi] %s\n", s_cli_line);

    s_cli_seq = seq;
    s_cli_ticket = ticket;
    s_cli_tx_ms = to_ms_since_boot(get_absolute_time());
    link_cli_out_reset(&s_cli_out);
    pitrac_abort_clear();          // a key typed before this command does not stop it
    s_cli_running = true;
    stdio_set_driver_enabled(&s_link_stdio, true);
    pitrac_abort_source(cli_poll);

    const cli_remote_t r = cli_run_remote(s_cli_line);

    pitrac_abort_source(NULL);
    stdio_flush();
    link_cli_out_flush(&s_cli_out, false, cli_emit, NULL);
    stdio_set_driver_enabled(&s_link_stdio, false);
    s_cli_running = false;

    // A stop asked for, and a stop done, are different answers (V113-F3): only a
    // command that ended early because of it says so (pitrac_abort_taken()).
    const bool asked   = pitrac_abort_pending();
    const bool stopped = asked && pitrac_abort_was_taken();
    link_cli_result_t res;
    res.status    = (r == CLI_REMOTE_PI_RULE) ? LINK_E_PI_RULE : 0u;
    res.flags     = (uint8_t)((asked ? LINK_CLI_F_STOP_ASKED : 0u) | (stopped ? LINK_CLI_F_STOPPED : 0u));
    res.session   = s_book.session;
    res.ticket    = ticket;
    res.out_bytes = s_cli_out.sent;
    res.out_crc   = s_cli_out.crc;
    if (r == CLI_REMOTE_PI_RULE) s_cli_rule++; else s_cli_runs++;
    if (asked) s_cli_stop_asked++;
    if (stopped) s_cli_stopped++;
    link_cli_finish(&s_book, &res);

    uint8_t rsp[LINK_CLI_RESULT_BYTES];
    send_frame_now((uint8_t)(LINK_CLI | LINK_RESPONSE), seq, rsp, link_cli_put_result(rsp, &res));
    cli_prompt_redraw();
}

// The CLI family with no command running. True if a command ran (the ring was
// read past this pass's snapshot).
static bool cli_request(uint8_t type, uint8_t seq, const uint8_t *p, size_t n, uint32_t now) {
    bool run = false;
    uint32_t ticket = 0u;

    // V113-F2: a request whose own cancel arrived behind it -- both queued while a
    // USB command held the board -- is cancelled first, and answered as such.
    // Searched up to what has arrived by now, not up to this pass's snapshot
    // (V114-F2, ring_settled_now()).
    if (type == LINK_CLI && n >= LINK_CLI_ID_BYTES &&
        cancel_waiting(get32(p), get32(&p[4]), ring_settled_now(), now)) {
        if (link_cli_idle(&s_book, LINK_CLI_ABORT, p, LINK_CLI_ID_BYTES, s_rsp, &run, &ticket) &&
            s_rsp[0] == 0u && s_rsp[1] == LINK_CLI_F_CANCELLED)
            s_cli_cancelled++;
    }

    // A session's numbers never wrap (V114-N3): when the last is issued, the next
    // CLI_BEGIN starts a new session. Every ticket of the old one is then refused
    // as another session's (LINK_E_SESSION), which the Pi reads as "whether it
    // ran is unknown" -- 2^32 - 1 tickets on, so in practice never.
    if (type == LINK_CLI_BEGIN && link_cli_book_full(&s_book))
        link_cli_book_init(&s_book, new_session(s_book.session));

    const size_t len = link_cli_idle(&s_book, type, p, n, s_rsp, &run, &ticket);
    if (run) {
        run_cli(seq, ticket, &p[LINK_CLI_ID_BYTES], n - LINK_CLI_ID_BYTES);
        return true;
    }
    if (type != LINK_CLI_BEGIN) {
        const uint8_t st = s_rsp[0], fl = s_rsp[1];
        if (st != 0u && st != LINK_E_PI_RULE) s_cli_refused++;
        else if (fl & LINK_CLI_F_REPEAT)      s_cli_repeats++;
        else if (fl & LINK_CLI_F_CANCELLED)   s_cli_cancelled++;
    }
    s_tx_len = link_frame_encode(s_tx, sizeof(s_tx), (uint8_t)(type | LINK_RESPONSE), seq, s_rsp, len);
    s_tx_at = 0u;
    tx_drain();
    return false;
}

void pi_link_service(void) {
    if (!s_ready) return;

    // A connect or a release starts clean: no half frame, no half answer, and
    // nothing that arrived while the lines were released (a break, at best).
    const bool conn = safe_state_pi_uart_connected();
    if (conn != s_was_connected) {
        s_was_connected = conn;
        if (conn) s_connects++;
        link_dec_reset(&s_dec);
        s_tx_len = s_tx_at = 0u;
        s_rd = ring_w();
        s_snap.valid = false;
    }

    tx_drain();
    if (s_tx_len) return;                         // one exchange at a time

    uint32_t upto;
    if (!link_rx_settled(&s_snap, ring_w(), time_us_64(), &upto)) return;
    if (!conn) { s_rd = upto; return; }           // released: not listening

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    while (s_rd != upto) {
        const uint8_t b = s_rx[s_rd];
        s_rd = (s_rd + 1u) & (RX_RING - 1u);
        s_rx_bytes++;
        if (link_dec_push(&s_dec, b, now) != LINK_DEC_FRAME) continue;

        if (s_dec.type == LINK_CLI || s_dec.type == LINK_CLI_ABORT || s_dec.type == LINK_CLI_BEGIN) {
            s_requests++;
            s_last_type = s_dec.type;
            s_last_ms = now;
            // A command reads the ring on past `upto` (cli_poll()), and an answer
            // waits in s_tx: either way, stop here and take a fresh snapshot on
            // the next pass.
            (void)cli_request(s_dec.type, s_dec.seq, s_dec.payload, s_dec.len, now);
            break;
        }

        const size_t n = link_proto_handle(s_dec.type, s_dec.payload, s_dec.len, s_rsp, sizeof(s_rsp));
        if (n == 0u) continue;
        s_requests++;
        if (s_rsp[0] == LINK_E_UNKNOWN) s_unknown++;
        s_last_type = s_dec.type;
        s_last_ms = now;
        s_tx_len = link_frame_encode(s_tx, sizeof(s_tx), (uint8_t)(s_dec.type | LINK_RESPONSE),
                                     s_dec.seq, s_rsp, n);
        s_tx_at = 0u;
        tx_drain();
        break;                                    // the rest waits for this answer
    }
}

void pi_link_status(pi_link_stat_t *out) {
    memset(out, 0, sizeof(*out));
    out->ready      = s_ready;
    out->connected  = s_ready && safe_state_pi_uart_connected();
    out->baud       = s_baud;
    out->connects   = s_connects;
    out->rx_bytes   = s_rx_bytes;
    out->tx_bytes   = s_tx_bytes;
    out->frames     = s_dec.frames;
    out->crc_errors = s_dec.crc_errors;
    out->gap_drops  = s_dec.gap_drops;
    out->junk       = s_dec.junk;
    out->requests   = s_requests;
    out->unknown    = s_unknown;
    out->last_type  = s_last_type;
    out->last_ms    = s_last_ms;
    out->cli_session    = s_book.session;
    out->cli_tickets    = s_book.issued;
    out->cli_runs       = s_cli_runs;
    out->cli_rule       = s_cli_rule;
    out->cli_repeats    = s_cli_repeats;
    out->cli_cancelled  = s_cli_cancelled;
    out->cli_refused    = s_cli_refused;
    out->cli_stop_asked = s_cli_stop_asked;
    out->cli_stopped    = s_cli_stopped;
    out->cli_dropped    = s_cli_dropped;
}
