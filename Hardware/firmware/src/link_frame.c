// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// link_frame.c -- see link_frame.h. No SDK calls: compiled into the host tests.

#include "link_frame.h"

#include <string.h>

enum { ST_HUNT = 0, ST_LEN, ST_TYPE, ST_SEQ, ST_PAYLOAD, ST_CRC0, ST_CRC1 };

uint16_t link_crc16(const uint8_t *p, size_t n, uint16_t crc) {
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (int k = 0; k < 8; k++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

size_t link_frame_encode(uint8_t *out, size_t cap, uint8_t type, uint8_t seq,
                         const uint8_t *payload, size_t len) {
    if (len > LINK_MAX_PAYLOAD || !out) return 0u;
    const size_t total = LINK_HDR_BYTES + len + LINK_CRC_BYTES;
    if (cap < total) return 0u;
    out[0] = LINK_SYNC;
    out[1] = (uint8_t)len;
    out[2] = type;
    out[3] = seq;
    if (len) memcpy(&out[LINK_HDR_BYTES], payload, len);
    const uint16_t crc = link_crc16(&out[1], 3u + len, LINK_CRC_INIT);
    out[LINK_HDR_BYTES + len]      = (uint8_t)(crc & 0xFFu);
    out[LINK_HDR_BYTES + len + 1u] = (uint8_t)(crc >> 8);
    return total;
}

void link_dec_init(link_dec_t *d) {
    memset(d, 0, sizeof(*d));
    d->state = ST_HUNT;
}

void link_dec_reset(link_dec_t *d) {
    d->state = ST_HUNT;
    d->got = 0u;
}

static void crc_byte(link_dec_t *d, uint8_t b) {
    d->crc = link_crc16(&b, 1u, d->crc);
}

link_dec_result_t link_dec_push(link_dec_t *d, uint8_t b, uint32_t now_ms) {
    // A partial frame that stalled is abandoned, and this byte starts afresh:
    // otherwise a frame cut short would swallow the start of the next one.
    if (d->state != ST_HUNT && now_ms - d->t_last_ms > LINK_BYTE_GAP_MS) {
        d->gap_drops++;
        d->state = ST_HUNT;
    }
    d->t_last_ms = now_ms;

    switch (d->state) {
    case ST_HUNT:
        if (b == LINK_SYNC) { d->state = ST_LEN; d->crc = LINK_CRC_INIT; }
        else d->junk++;
        return LINK_DEC_NONE;
    case ST_LEN:
        d->len = b; d->got = 0u; crc_byte(d, b); d->state = ST_TYPE;
        return LINK_DEC_NONE;
    case ST_TYPE:
        d->type = b; crc_byte(d, b); d->state = ST_SEQ;
        return LINK_DEC_NONE;
    case ST_SEQ:
        d->seq = b; crc_byte(d, b);
        d->state = d->len ? ST_PAYLOAD : ST_CRC0;
        return LINK_DEC_NONE;
    case ST_PAYLOAD:
        d->payload[d->got++] = b; crc_byte(d, b);
        if (d->got >= d->len) d->state = ST_CRC0;
        return LINK_DEC_NONE;
    case ST_CRC0:
        d->crc_rx = b; d->state = ST_CRC1;
        return LINK_DEC_NONE;
    case ST_CRC1:
        d->crc_rx = (uint16_t)(d->crc_rx | ((uint16_t)b << 8));
        d->state = ST_HUNT;
        if (d->crc_rx != d->crc) { d->crc_errors++; return LINK_DEC_BAD_CRC; }
        d->frames++;
        return LINK_DEC_FRAME;
    default:
        d->state = ST_HUNT;
        return LINK_DEC_NONE;
    }
}

bool link_rx_settled(link_rx_snap_t *s, uint32_t w_now, uint64_t now_us, uint32_t *upto) {
    if (!s->valid) {
        s->idx = w_now; s->t_us = now_us; s->valid = true;
        return false;
    }
    if (now_us - s->t_us < LINK_RX_SETTLE_US) return false;
    *upto = s->idx;
    s->idx = w_now;
    s->t_us = now_us;
    return true;
}
