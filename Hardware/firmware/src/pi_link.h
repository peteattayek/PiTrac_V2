// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// pi_link.h -- the serial link to the Pi on UART1 (J8.8/J8.10), 921600 8N1.
//
// The framing is link_frame.h, the requests link_proto.h. This file is the
// hardware: the UART, a DMA ring for what arrives, and the bytes going out.
//
// PINS. The power FSM decides when GPIO36/37 are on UART1 (safe_state.h): only
// while a Pi is up and talking. While they are released this module ignores the
// line, and every reconnect starts clean.
//
// WHEN IT RUNS. pi_link_service() runs from the superloop next to cli_service(),
// not from pitrac_service(): a request can start an update or a shutdown, which
// must not happen inside a blocking CLI command. During a long CLI command the
// link does not answer and the Pi retries; the DMA ring keeps the bytes (2 KB).
//
// THE PI'S CLI (protocol 4, image 1.16; link_proto.h, link_cli.h). A CLI request
// runs right here, at the top of the superloop exactly where a USB line runs, so
// it can never start inside another command, and only under a ticket the board
// issued and has not used (the book, link_cli.h). Before it runs, the bytes that
// arrived after it are searched for its own CLI_ABORT: a request and its cancel
// queued together behind a USB command end cancelled. While it runs, its printf
// output also goes to the Pi (a stdio driver enabled only then), the link is read
// only for that ticket's CLI_ABORT, and a keepalive goes out each second -- both
// from the yield path (pitrac_abort_source()). USB input waits, but any key typed
// there asks it to stop. Its answer says whether a stop was asked, and whether
// the command acted on it (pitrac_abort_was_taken()).
//
// Nothing here uses an interrupt (the firmware installs none): received bytes are
// collected by DMA and read from the superloop.

#ifndef PITRAC_PI_LINK_H
#define PITRAC_PI_LINK_H

#include <stdbool.h>
#include <stdint.h>

// After safe_state_init() and before power_fsm_init(): the UART exists before
// anything can connect its pins.
void pi_link_init(void);

// Superloop: read what arrived, answer one request at a time, send.
void pi_link_service(void);

typedef struct {
    bool     ready;          // UART and RX DMA set up
    bool     connected;      // GPIO36/37 on UART1: a Pi is up
    uint32_t baud;           // what the UART divider gives
    uint32_t connects;       // times the lines were connected
    uint32_t rx_bytes, tx_bytes;
    uint32_t frames, crc_errors, gap_drops, junk;
    uint32_t requests, unknown;   // answered; of those, unknown types
    uint8_t  last_type;      // the last request answered
    uint32_t last_ms;        // ...when
    // The Pi's CLI (protocol 4), this boot.
    uint32_t cli_session;    // this board session's id (link_cli.h)
    uint32_t cli_tickets;    // tickets issued
    uint32_t cli_runs;       // command lines run (refusals by the CLI's own guards included)
    uint32_t cli_rule;       // refused by the Pi-only power rule (nothing ran)
    uint32_t cli_repeats;    // resends and CLI_ABORTs answered from the record, nothing run
    uint32_t cli_cancelled;  // tickets cancelled before they ran
    uint32_t cli_refused;    // CLI or CLI_ABORT refused: malformed, another session, stale
    uint32_t cli_stop_asked; // commands a stop was asked of (a CLI_ABORT, or a key on USB)
    uint32_t cli_stopped;    // ...of those, commands that acted on it and ended early
    uint32_t cli_dropped;    // frames that arrived while a command ran, other than its abort
} pi_link_stat_t;

void pi_link_status(pi_link_stat_t *out);

#endif // PITRAC_PI_LINK_H
