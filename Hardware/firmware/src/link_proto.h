// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// link_proto.h -- what the Pi may ask over the serial link, and the answers.
//
// AN ALLOWLIST, NOT THE CLI. The USB CLI can drive pins, fire the strobe and
// reset the board; nothing here can. Each request below is handled by one
// function with a fixed payload, and anything else gets LINK_E_UNKNOWN. Phase 8's
// ARM, CONFIG and event messages (DEVELOPER_GUIDE.md section 9.3) will be added
// the same way.
//
// Request/response, one at a time: the Pi sends a request and waits for its
// response (type | 0x80, the same seq, payload[0] = a status) before the next.
// A frame whose type has bit 7 set is never answered, so a looped-back line
// cannot start a conversation with itself. Multi-byte fields are little-endian.
//
//  type  request      request payload            response payload after the status byte
//  0x01  PING         -                          proto u8, fw version u32
//  0x02  INFO         -                          fw version u32, build stamp char[20],
//                                                boot type u8, boot partition s8,
//                                                layout flags u8 (1 partitioned, 2 trial,
//                                                4 bought), run start u32, run size u32,
//                                                other start u32, other size u32,
//                                                staging capacity u32
//  0x03  STATUS       -                          power state u8, fault u8, +5V_IN mV u16
//                                                (0xFFFF unread), flags u8 (1 Pi present,
//                                                2 latch closed, 4 rails ready), uptime
//                                                ms u32, time in state ms u32
//  0x04  HIST         -                          the 16 halt-telemetry words, u32 each
//  0x10  UPD_BEGIN    size u32, sha256[32]       max data per UPD_DATA u32
//  0x11  UPD_DATA     offset u32, data[1..251]   next expected offset u32
//  0x12  UPD_END      -                          staged image version u32
//  0x13  UPD_APPLY    -                          -   (the board then shuts the Pi down)
//  0x14  UPD_ABORT    -                          -   (as `upd abort`: fw_update.h)
//  0x15  UPD_STATUS   -                          state u8, last error u8, size u32,
//                                                received u32, staged version u32,
//                                                result u8, from version u32,
//                                                to version u32, detail u8 (its
//                                                meaning per result: fwu_status_t)
//
// The status byte is a fwu_err_t (fw_update.h; 0 = ok), or LINK_E_*.
//
// Compiled into the host tests (tests/link_test.c) against mocks.

#ifndef PITRAC_LINK_PROTO_H
#define PITRAC_LINK_PROTO_H

#include <stddef.h>
#include <stdint.h>

#define LINK_PROTO_VERSION  1u
#define LINK_BAUD           921600u

enum {
    LINK_PING       = 0x01,
    LINK_INFO       = 0x02,
    LINK_STATUS     = 0x03,
    LINK_HIST       = 0x04,
    LINK_UPD_BEGIN  = 0x10,
    LINK_UPD_DATA   = 0x11,
    LINK_UPD_END    = 0x12,
    LINK_UPD_APPLY  = 0x13,
    LINK_UPD_ABORT  = 0x14,
    LINK_UPD_STATUS = 0x15,
};

#define LINK_RESPONSE        0x80u
#define LINK_DATA_MAX        251u      // LINK_MAX_PAYLOAD less UPD_DATA's offset
#define LINK_STAMP_BYTES     20u       // "Oct 19 2026 11:44:38"

// Link-level statuses, clear of every fwu_err_t.
#define LINK_E_LENGTH        0xFDu     // the payload is the wrong length for the request
#define LINK_E_UNKNOWN       0xFEu     // not a request this firmware accepts

// One request. Writes the response payload (status first) into rsp[cap], cap
// at least LINK_MAX_PAYLOAD, and returns its length; 0 means no response (a
// type with bit 7 set).
size_t link_proto_handle(uint8_t type, const uint8_t *req, size_t req_len,
                         uint8_t *rsp, size_t cap);

#endif // PITRAC_LINK_PROTO_H
