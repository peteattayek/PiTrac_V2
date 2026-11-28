// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// cli.h -- line-based bench CLI over USB-CDC.
//
// This is the spine of the whole bring-up. `capture` in particular turns the
// board into a logging DSO, which is what makes the analog phases tractable
// without wiring a scope probe to everything.

#ifndef PITRAC_CLI_H
#define PITRAC_CLI_H

#include <stdbool.h>

void cli_init(void);
void cli_service(void);   // non-blocking; call from the core 0 superloop

// A command line from the Pi (protocol 4, pi_link.c). Split and dispatched
// exactly as a line typed on USB -- the same dispatcher, so the same guards --
// after the Pi-only power rule (link_cli_power_rule(): what would cut the Pi's
// own power needs `confirm`, and `off` runs only as the orderly shutdown).
// `line` is modified (split in place). Prints as a USB command does; the caller
// routes the output. Called only from the superloop, never from inside a
// command. Returns CLI_REMOTE_RAN, or CLI_REMOTE_PI_RULE if that rule refused it
// (nothing ran).
typedef enum { CLI_REMOTE_RAN = 0, CLI_REMOTE_PI_RULE } cli_remote_t;
cli_remote_t cli_run_remote(char *line);

// After a Pi command printed on the USB console: a fresh prompt, with whatever
// the USB operator had typed so far, so the two do not interleave unseen.
void cli_prompt_redraw(void);

// The image's identity, "Mmm dd yyyy hh:mm:ss": cli.c's __DATE__ and __TIME__,
// the one stamp in the binary (HANDOFF.md section 4). `id` prints it; the Pi
// link's INFO sends it.
const char *cli_build_stamp(void);

#endif // PITRAC_CLI_H
