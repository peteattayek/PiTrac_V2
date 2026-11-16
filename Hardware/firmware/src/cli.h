// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// cli.h -- line-based bench CLI over USB-CDC.
//
// This is the spine of the whole bring-up. `capture` in particular turns the
// board into a logging DSO, which is what makes the analog phases tractable
// without wiring a scope probe to everything.

#ifndef PITRAC_CLI_H
#define PITRAC_CLI_H

void cli_init(void);
void cli_service(void);   // non-blocking; call from the core 0 superloop

// The image's identity, "Mmm dd yyyy hh:mm:ss": cli.c's __DATE__ and __TIME__,
// the one stamp in the binary (HANDOFF.md section 4). `id` prints it; the Pi
// link's INFO sends it.
const char *cli_build_stamp(void);

#endif // PITRAC_CLI_H
