// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// link_proto.h -- what the Pi may ask over the serial link, and the answers.
//
// THE PI CAN RUN THE CLI. Until 1.12 this was an allowlist -- nothing here
// could drive a pin, fire the strobe or reset the board. The owner decided on
// 2026-11-24 (PROGRESS.md section 8) that the link may do anything the USB CLI
// can: CLI (0x20) carries a command line, which runs through the CLI's own
// dispatcher and therefore under every guard it has (the live-mode allowlist,
// the strobe staircase, cap and budget, the beam/live interlock, the
// reset/bootsel Pi guard). A live strobe session is shared between USB and the
// Pi. From the Pi only, the commands that would cut the Pi's own power need a
// trailing `confirm`, and a plain `off` runs only as the orderly shutdown
// (link_cli.h). The fixed requests below stay as they were; a protocol-1 tool
// can still use all of them.
//
// PROTOCOL 4 (image 1.16) is protocol 3 with the SESSION in every CLI answer --
// the result and each CLI_OUT -- beside the ticket number (review finding
// V115-F4): numbers restart at 1 in every session, and a late answer about this
// boot's ticket 1 was taken for the answer about the previous boot's. Protocol 3
// (images 1.14 and 1.15) failed two reviews and never left the PC. Image 1.13's
// protocol 2 failed its independent
// review (V113-F1..F5, PROGRESS.md section 6) and never left the PC: a resend
// matched on an 8-bit seq could run a command twice or skip a new one, a
// cancelled command could still start, "stopped" could mean "finished", and a
// lost output frame went unnoticed. Protocols 3 and 4 replace it:
//   - TICKETS. Every command first takes a ticket from the board (CLI_BEGIN):
//     this board session's 32-bit id and a 32-bit number the board counts up.
//     The count never wraps within a session: before it would, the board starts
//     a new session (image 1.15, review note V114-N3). So a ticket, session and
//     number together, is issued once and runs at most once. Its resend is
//     answered from the board's record (LINK_CLI_BOOK tickets deep) and never
//     runs again; a ticket from
//     another session (the board has restarted) or older than the record is
//     refused, and runs nothing. Two clients, a wrapped seq or a reset cannot
//     make one ticket run twice or a new one look old (link_cli.h).
//   - CANCEL. CLI_ABORT names a ticket. Not yet run: it is cancelled and will
//     never run -- including when the request is already waiting in the
//     receive buffer behind it. Running: it is asked to stop. Done: the answer
//     is its recorded result.
//   - STOPPED means stopped. The answer says a stop was ASKED (F_STOP_ASKED)
//     and, separately, whether the command ACTED on it (F_STOPPED).
//   - COMPLETE OUTPUT. Each CLI_OUT carries its byte offset; the answer carries
//     the total and a CRC-16 of all of it, so the Pi can tell it lost a frame.
//
// Request/response, one at a time: the Pi sends a request and waits for its
// response (type | 0x80, the same seq, payload[0] = a status) before the next.
// A frame whose type has bit 7 set is never answered, so a looped-back line
// cannot start a conversation with itself. Multi-byte fields are little-endian.
//
// CLI is the one request answered by more than one frame: CLI_OUT frames
// (0xA1, the request's seq) carry the command's output text as it is printed,
// at most LINK_CLI_OUT_CHUNK bytes each, and an empty one at least every
// LINK_CLI_KEEPALIVE_MS while the command runs and yields; then the response
// (0xA0) ends it. Everything printed shows on the USB console too, under a
// `[pi] <command>` line. While the command runs, the board reads only a
// CLI_ABORT for that ticket from the Pi; every other frame is dropped. That
// CLI_ABORT, or any key typed at the USB console, asks the command to stop, as
// a key does on USB.
//
// The CLI family's answers (CLI, CLI_ABORT) all have one 16-byte shape, "the
// result": status u8, flags u8 (LINK_CLI_F_*), session u32, ticket u32, output
// bytes u32, output CRC-16 u16 (link_crc16 from LINK_CRC_INIT over every output
// byte). The session and ticket are the request's, refusals included, so the Pi
// takes an answer only for the (session, ticket) it asked about. The output fields
// are 0 for a command that did not run.
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
//  0x20  CLI          session u32, ticket u32,   the result. Status 0: it ran (or, with
//                     a command line: 1..95      F_REPEAT, had run), and its output
//                     bytes of printable ASCII   (CLI_OUT, before this) says what
//                                                happened, refusals included; with
//                                                F_CANCELLED it never ran. Otherwise
//                                                a LINK_E_* below: nothing ran now
//  0x21  CLI_OUT      (never a request: 0xA1     session u32, ticket u32, offset u32
//                     frames go board -> Pi)     (of this text's first byte in the
//                                                output), text 0..240 bytes; an empty
//                                                one is a keepalive. No status byte
//  0x22  CLI_ABORT    session u32, ticket u32    the result. Not run yet: F_CANCELLED,
//                                                and it never will run. Already done:
//                                                its recorded result (F_REPEAT). While
//                                                it runs this is not answered: it asks
//                                                the command to stop, and the CLI's
//                                                own answer says what happened
//  0x23  CLI_BEGIN    -                          session u32, ticket u32: a new ticket
//
// The status byte is a fwu_err_t (fw_update.h; 0 = ok), or LINK_E_*.
//
// Compiled into the host tests (tests/link_test.c) against mocks.

#ifndef PITRAC_LINK_PROTO_H
#define PITRAC_LINK_PROTO_H

#include <stddef.h>
#include <stdint.h>

// 1: the fixed requests (images 1.1-1.12). 2: image 1.13's CLI, rejected, never
// shipped. 3: the CLI with tickets (1.14 and 1.15, rejected, never shipped). 4: the
// same with the session in every answer (1.16). A protocol-1 board answers any CLI
// request with LINK_E_UNKNOWN alone.
#define LINK_PROTO_VERSION  4u
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
    LINK_CLI        = 0x20,
    LINK_CLI_OUT    = 0x21,
    LINK_CLI_ABORT  = 0x22,
    LINK_CLI_BEGIN  = 0x23,
};

#define LINK_RESPONSE        0x80u
#define LINK_DATA_MAX        251u      // LINK_MAX_PAYLOAD less UPD_DATA's offset
#define LINK_STAMP_BYTES     20u       // "Oct 19 2026 11:44:38"

// The flags byte of the CLI family's result.
#define LINK_CLI_F_REPEAT     0x01u    // answered from the board's record: it had already
                                       // run (or been cancelled), and did not run again; its
                                       // output is not sent again
#define LINK_CLI_F_STOP_ASKED 0x02u    // a stop (a CLI_ABORT for this ticket, or a key on
                                       // USB) arrived while it ran
#define LINK_CLI_F_STOPPED    0x04u    // ...and the command acted on it: it ended early.
                                       // Without this, it ran to its end
#define LINK_CLI_F_CANCELLED  0x08u    // cancelled before it ran: it did not run, and this
                                       // ticket never will

// Link-level statuses, clear of every fwu_err_t.
#define LINK_E_MISMATCH      0xF9u     // CLI: this ticket already carried a line of another
                                       // length or CRC-16 (a client's mistake, caught as far
                                       // as those two tell lines apart); nothing ran now
#define LINK_E_STALE         0xFAu     // CLI, CLI_ABORT: the ticket is older than the board's
                                       // record; nothing ran now, and whether it ran before
                                       // is not known
#define LINK_E_SESSION       0xFBu     // CLI, CLI_ABORT: not a ticket of this board session
                                       // (the board has restarted since, or it was never
                                       // issued); nothing ran now
#define LINK_E_PI_RULE       0xFCu     // CLI: the Pi-only power rule refused it (its output
                                       // says why: add `confirm`, or `off` outside RUNNING);
                                       // nothing ran
#define LINK_E_LENGTH        0xFDu     // the payload is the wrong length for the request
#define LINK_E_UNKNOWN       0xFEu     // not a request this firmware accepts

// One request. Writes the response payload (status first) into rsp[cap], cap
// at least LINK_MAX_PAYLOAD, and returns its length; 0 means no response (a
// type with bit 7 set).
size_t link_proto_handle(uint8_t type, const uint8_t *req, size_t req_len,
                         uint8_t *rsp, size_t cap);

#endif // PITRAC_LINK_PROTO_H
