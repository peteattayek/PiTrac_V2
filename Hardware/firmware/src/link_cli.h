// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// link_cli.h -- the Pi's CLI over the serial link (protocol 4): the parts that
// are pure logic. pi_link.c does the UART and runs the command; cli.c parses and
// dispatches it exactly as it does a line typed on USB.
//
// THE OWNER'S DECISIONS (2026-11-24, PROGRESS.md section 8): the Pi may run
// anything the USB CLI can, through the same dispatcher and therefore the same
// guards -- the live-mode allowlist, the strobe staircase, cap and budget, the
// beam/live interlock, the reset/bootsel Pi guard. A live strobe session is
// shared: either side may arm, fire and end it. From the Pi only, what would cut
// the Pi's own power needs a trailing `confirm`, and a plain `off` runs only as
// the orderly shutdown (link_cli_power_rule()).
//
// AT MOST ONCE (image 1.14, review findings V113-F1/F2). Every other link request
// is safe to repeat; a command line is not -- a repeated `strobe pulse 20` fires
// twice. Image 1.13 remembered the last command by its 8-bit seq and text, and
// its review showed that this can run one command twice (a second client's
// command in between) or skip a new one (the seq wrapped). So a command now
// carries a TICKET the board issued for it (CLI_BEGIN): this board session's id
// and a number the board counts up. The board's record (the book) holds the
// last LINK_CLI_BOOK tickets: issued, run (with the line's length and CRC-16, and
// how it ended), or cancelled. A ticket runs only from "issued", so at most once
// in a session, and no number is issued twice in one: the count stops short of
// wrapping, and pi_link.c starts a new session first (image 1.15, review note
// V114-N3; 1.14 wrapped to 0 and reused low numbers). The length and CRC-16 tell
// a resend from a different line under a used ticket -- a client's mistake --
// as far as they can: two lines of one length can share a CRC-16. A ticket from
// another session -- the board restarted, which
// also ends any live strobe session -- or one older than the book runs nothing,
// and the answer says so. Whether such a ticket ran BEFORE is then unknown, and
// the Pi's tool says that rather than guessing. Every answer names the session as
// well as the number (protocol 4, image 1.16, review finding V115-F4), so it can
// be told from an answer about the same number in another session.
//
// No SDK calls: compiled into the native host tests (tests/link_test.c).

#ifndef PITRAC_LINK_CLI_H
#define PITRAC_LINK_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_frame.h"

// The longest command line, as the USB CLI's buffer (cli.c CLI_MAX_LINE - 1).
#define LINK_CLI_LINE_MAX     95u
// Output text per CLI_OUT frame, after its 12-byte header (session, ticket, offset).
#define LINK_CLI_OUT_CHUNK    240u
#define LINK_CLI_OUT_HDR      12u
// While a command runs, a CLI_OUT frame (empty if there is no new text) at
// least this often, from the yield path, so the Pi can tell "still running"
// from "lost". A command that neither prints nor yields for longer sends none.
#define LINK_CLI_KEEPALIVE_MS 1000u
// The ticket that heads CLI and CLI_ABORT: session u32, ticket u32.
#define LINK_CLI_ID_BYTES     8u
// The CLI family's answer: status, flags, session u32, ticket u32, output bytes u32,
// CRC u16. The session is protocol 4's (image 1.16, review finding V115-F4): a ticket
// NUMBER restarts at 1 in every session, so an answer that named the number alone
// could be taken for another session's -- a late "cancelled" for this boot's
// ticket 1 was read as the answer about the previous boot's ticket 1, which had run.
#define LINK_CLI_RESULT_BYTES 16u
// CLI_BEGIN's answer: status, session u32, ticket u32.
#define LINK_CLI_BEGIN_BYTES  9u
// How many tickets the board remembers. A resend of any of the last 16 commands
// is answered from the record; an older ticket is refused (LINK_E_STALE).
#define LINK_CLI_BOOK         16u

// A command line from the Pi: 1..LINK_CLI_LINE_MAX bytes of printable ASCII
// (32-126). No tabs, no control characters, no NUL.
bool link_cli_valid_line(const uint8_t *p, size_t n);

// How a command ended, as its answer carries it: for exactly the (session, ticket)
// the request named, whatever the status -- a refusal echoes the request's pair.
typedef struct {
    uint8_t  status;       // 0, or a LINK_E_* (link_proto.h)
    uint8_t  flags;        // LINK_CLI_F_*
    uint32_t session;
    uint32_t ticket;
    uint32_t out_bytes;    // every byte of output sent for it
    uint16_t out_crc;      // link_crc16 of those bytes, from LINK_CRC_INIT
} link_cli_result_t;

// The 16-byte wire form. Returns LINK_CLI_RESULT_BYTES.
size_t link_cli_put_result(uint8_t *rsp, const link_cli_result_t *r);

typedef enum {
    LINK_CLI_T_ISSUED = 1,      // may run, once
    LINK_CLI_T_RAN,             // ran (or is running): never again
    LINK_CLI_T_CANCELLED,       // cancelled before it ran: never
} link_cli_tstate_t;

typedef struct {
    uint32_t          ticket;   // 0: empty
    uint8_t           state;    // link_cli_tstate_t
    uint8_t           len;      // the line it ran, to tell a resend from a misuse
    uint16_t          crc;
    link_cli_result_t r;        // how it ended (RAN, CANCELLED)
} link_cli_slot_t;

typedef struct {
    uint32_t        session;    // this board session's id, random at boot
    uint32_t        issued;     // the last ticket issued; 0 before the first
    link_cli_slot_t slot[LINK_CLI_BOOK];
} link_cli_book_t;

void     link_cli_book_init(link_cli_book_t *b, uint32_t session);
// A new ticket, ISSUED. It displaces the one LINK_CLI_BOOK before it. 0 (none)
// once the book is full: the session must be retired first.
uint32_t link_cli_issue(link_cli_book_t *b);
// Every number of this session issued (2^32 - 1 of them): start a new session
// (link_cli_book_init() with a new id) before the next CLI_BEGIN.
bool     link_cli_book_full(const link_cli_book_t *b);

// The CLI family with NO command running: CLI_BEGIN, CLI_ABORT and CLI. Writes
// the answer (status first) into rsp, at least LINK_CLI_RESULT_BYTES, and
// returns its length -- except for a CLI to run now: then *run is true, *ticket
// is its ticket, the line is p[LINK_CLI_ID_BYTES..n), and nothing is written.
// That ticket is already marked RAN, so it cannot run again whatever arrives;
// link_cli_finish() records how it ended. Any other type returns 0.
size_t link_cli_idle(link_cli_book_t *b, uint8_t type, const uint8_t *p, size_t n,
                     uint8_t *rsp, bool *run, uint32_t *ticket);

// How a command that link_cli_idle() released ended. Its resend, or a CLI_ABORT
// naming it, is answered with this, flagged LINK_CLI_F_REPEAT.
void link_cli_finish(link_cli_book_t *b, const link_cli_result_t *r);

// THE PI-ONLY POWER RULE (the owner, 2026-11-24; image 1.14 for V113-F4). From
// the Pi, these run only as written:
//   `forceoff confirm`                          -> `forceoff`
//   `reset force confirm`, `bootsel force confirm` -> `<cmd> force`
//   `adc5vcal <volts> confirm`                  -> `adc5vcal <volts>`: a wrong scale
//                                                  makes the supply monitor drop the rail
//   `fault clear confirm`, while a fault holds the rail up (acknowledging it drops
//                                                  the rail) -> `fault clear`
//   `off`, only while the board is RUNNING the Pi: the orderly shutdown. Anywhere
//                                                  else `off` drops the rail at once
//                                                  (FAULT, or a Pi not yet classified)
// The trailing `confirm` is removed before dispatch. Anything else is untouched:
// plain `reset` and `bootsel` still meet the CLI's own Pi guard.
typedef struct {
    bool off_orderly;       // an `off` now would be the orderly shutdown (RUNNING)
    bool fault_ack_cuts;    // a `fault clear` now would drop the rail (FAULT, latch closed)
} link_cli_power_t;

typedef enum {
    LINK_CLI_ALLOW = 0,          // dispatch argv[0..*argc) as it now stands
    LINK_CLI_NEEDS_CONFIRM,      // refuse: nothing runs; the form with `confirm` would
    LINK_CLI_NOT_ORDERLY,        // refuse `off`: no orderly shutdown is possible now
} link_cli_rule_t;

link_cli_rule_t link_cli_power_rule(int *argc, char **argv, const link_cli_power_t *now);

// Output: text collected and sent in chunks of at most LINK_CLI_OUT_CHUNK, each
// with its offset in the whole; the total and a CRC of all of it go in the answer.
typedef void (*link_cli_emit_fn)(uint32_t offset, const uint8_t *p, size_t n, void *ctx);

typedef struct {
    uint8_t  buf[LINK_CLI_OUT_CHUNK];
    size_t   n;            // held, not yet sent
    uint32_t sent;         // bytes sent so far: the next chunk's offset
    uint16_t crc;          // link_crc16 of every byte sent, from LINK_CRC_INIT
} link_cli_out_t;

void link_cli_out_reset(link_cli_out_t *o);
// Append; every time the buffer fills, emit it.
void link_cli_out_put(link_cli_out_t *o, const char *s, size_t n, link_cli_emit_fn emit, void *ctx);
// Emit what is held (nothing if empty, unless `always`: then an empty chunk,
// the keepalive).
void link_cli_out_flush(link_cli_out_t *o, bool always, link_cli_emit_fn emit, void *ctx);

// While a command runs, the bytes arriving from the Pi go through the link's own
// decoder. A CLI_ABORT for exactly this (session, ticket), and 8 bytes long,
// asks it to stop; any other frame is dropped -- a late abort for an earlier
// ticket, a resend of this one, a new request. Returns true if the matching
// CLI_ABORT arrived; *dropped counts the other frames.
bool link_cli_scan(link_dec_t *d, const uint8_t *bytes, size_t n, uint32_t now_ms,
                   uint32_t session, uint32_t ticket, uint32_t *dropped);

// Before a CLI request runs: is a CLI_ABORT for it already among the bytes that
// arrived after it? Read with a decoder of the caller's own (`scratch`, kept
// across calls for a buffer that wraps), so the link's is untouched and the
// bytes are still read in order afterwards. For V113-F2: a request and its
// cancel that queued up together while a USB command held the board must end
// cancelled, not run.
bool link_cli_abort_ahead(link_dec_t *scratch, const uint8_t *bytes, size_t n, uint32_t now_ms,
                          uint32_t session, uint32_t ticket);

#endif // PITRAC_LINK_CLI_H
