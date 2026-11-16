// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// pi_link.c -- see pi_link.h.

#include "pi_link.h"
#include "link_frame.h"
#include "link_proto.h"
#include "safe_state.h"
#include "board.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/dma.h"

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

// The next ring slot the DMA will write.
static uint32_t ring_w(void) {
    return ((uint32_t)(uintptr_t)dma_hw->ch[s_dma].write_addr - (uint32_t)(uintptr_t)s_rx)
           & (RX_RING - 1u);
}

void pi_link_init(void) {
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
}
