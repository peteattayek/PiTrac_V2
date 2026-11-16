// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// link_frame.h -- the byte framing of the Pi serial link (UART1, J8.8/J8.10).
//
// One frame, all multi-byte fields little-endian:
//
//     0xA5 | len | type | seq | payload[len] | crc_lo | crc_hi
//
//   len      payload bytes, 0-255
//   type     the request (Pi -> board), or the request | 0x80 (the response)
//   seq      chosen by the Pi; the response echoes it. A retry reuses it.
//   crc      CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no
//            final xor) over len, type, seq and the payload -- not the 0xA5.
//
// The proposal in DEVELOPER_GUIDE.md section 9.3 had no seq byte; it was added
// so a response can be matched to its request across a retry.
//
// The decoder is a byte-at-a-time state machine. It hunts for 0xA5, and drops a
// frame whose CRC fails or whose bytes stop arriving for LINK_BYTE_GAP_MS; the
// Pi's retry recovers either. A lost or corrupted byte therefore costs a retry,
// never a wrong command: every path to a command passes the CRC.
//
// No SDK calls: compiled into the native host tests (tests/link_test.c).

#ifndef PITRAC_LINK_FRAME_H
#define PITRAC_LINK_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LINK_SYNC          0xA5u
#define LINK_MAX_PAYLOAD   255u
#define LINK_HDR_BYTES     4u       // sync, len, type, seq
#define LINK_CRC_BYTES     2u
#define LINK_MAX_FRAME     (LINK_HDR_BYTES + LINK_MAX_PAYLOAD + LINK_CRC_BYTES)   // 261
#define LINK_CRC_INIT      0xFFFFu

// A partial frame whose next byte comes this long after the previous one is
// dropped. At 921600 baud a whole 261-byte frame takes 2.8 ms. The time is the
// one the bytes are PROCESSED at, so a superloop held up by a long CLI command
// can split a frame across this gap: that costs the Pi one retry.
#define LINK_BYTE_GAP_MS   50u

// CRC-16/CCITT-FALSE, continued from `crc` (LINK_CRC_INIT to start).
// "123456789" -> 0x29B1.
uint16_t link_crc16(const uint8_t *p, size_t n, uint16_t crc);

// Writes one frame into out[cap]. Returns its length, or 0 if len > 255 or the
// frame does not fit (nothing useful is written then).
size_t link_frame_encode(uint8_t *out, size_t cap, uint8_t type, uint8_t seq,
                         const uint8_t *payload, size_t len);

typedef enum {
    LINK_DEC_NONE = 0,     // byte consumed, no frame yet
    LINK_DEC_FRAME,        // a whole frame passed its CRC: read it from the decoder now
    LINK_DEC_BAD_CRC,      // a whole frame failed its CRC and was dropped
} link_dec_result_t;

typedef struct {
    uint8_t  state;        // internal
    uint8_t  len, type, seq;
    uint16_t got;          // payload bytes received so far
    uint16_t crc;          // running CRC over len, type, seq, payload
    uint16_t crc_rx;       // the frame's own CRC, as received
    uint32_t t_last_ms;    // when the previous byte of a partial frame was processed
    uint8_t  payload[LINK_MAX_PAYLOAD];
    // Counters since link_dec_init().
    uint32_t frames;       // frames that passed the CRC
    uint32_t crc_errors;   // frames dropped on their CRC
    uint32_t gap_drops;    // partial frames dropped on LINK_BYTE_GAP_MS
    uint32_t junk;         // bytes discarded while hunting for 0xA5
} link_dec_t;

void link_dec_init(link_dec_t *d);
// Back to hunting for 0xA5, dropping any partial frame; the counters stay.
void link_dec_reset(link_dec_t *d);

// Feed one byte, processed at now_ms. On LINK_DEC_FRAME, d->type, d->seq,
// d->len and d->payload hold the frame until the next call.
link_dec_result_t link_dec_push(link_dec_t *d, uint8_t b, uint32_t now_ms);

// ---------------------------------------------------------------------------
// Reading the RX DMA ring.
//
// The DMA's WRITE_ADDR counts transfers ISSUED, and a write can land in SRAM a
// little after its address was issued (PROGRESS.md section 6, review round 4 of
// 2026-10-12, for the ADC ring). So a byte is read only once the write index
// that covers it is at least LINK_RX_SETTLE_US old: about 3000 cycles at
// 150 MHz, two orders of magnitude above that lag. A byte read early anyway
// fails the frame's CRC: one retry, not a wrong command.
//
// The ring keeps the newest RING bytes. If the consumer falls more than a whole
// ring behind, the oldest bytes are overwritten and skipped: never re-read, so a
// stale frame cannot be replayed.
// ---------------------------------------------------------------------------
#define LINK_RX_SETTLE_US  20u

typedef struct {
    uint32_t idx;          // a write index...
    uint64_t t_us;         // ...and when it was read
    bool     valid;
} link_rx_snap_t;

// Once per service pass, with the write index read now. Returns true with
// *upto = an index whose bytes have all landed (read up to it), and re-arms the
// snapshot with w_now; false while the held snapshot is younger than
// LINK_RX_SETTLE_US (or there was none). The indices are the caller's ring
// positions; nothing here interprets them.
bool link_rx_settled(link_rx_snap_t *s, uint32_t w_now, uint64_t now_us, uint32_t *upto);

#endif // PITRAC_LINK_FRAME_H
