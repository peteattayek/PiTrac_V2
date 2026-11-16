// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// pio_sim.h -- a small, cycle-counting model of ONE RP2350 PIO state machine, for
// the host tests. It runs the instruction words pioasm generates from the real
// .pio files, so a test checks the program that ships, not a transcription.
//
// Modelled: every instruction this firmware's programs use (JMP with all eight
// conditions, WAIT on GPIO/PIN/IRQ, IN, OUT, PUSH/PULL with block/noblock, MOV
// with invert/bit-reverse and IN_COUNT masking, IRQ set/clear/wait with the
// RP2350 PREV/NEXT/REL index modes, SET), delay cycles, wrap, an integer clock
// divider, 4-deep FIFOs, and the 2-flop input synchroniser (inputs are seen 2
// system clocks late). Not modelled: side-set, autopush/autopull (asserted off),
// FIFO joins, MOV to/from the RX FIFO, OUT/MOV EXEC, and STATUS beyond "TX level".
//
// Conventions: GPIOBASE is 0, so pin numbers are absolute GPIO numbers.

#ifndef PITRAC_PIO_SIM_H
#define PITRAC_PIO_SIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PIO_SIM_FIFO_DEPTH 4u
#define PIO_SIM_SYNC_CYCLES 2u

typedef struct pio_sim pio_sim_t;

// The GPIO input bank as the pads present it at system clock `cycle`.
typedef uint32_t (*pio_sim_pins_fn)(uint64_t cycle, void *ctx);

typedef struct {
    uint32_t data[PIO_SIM_FIFO_DEPTH];
    unsigned head, count;
} pio_sim_fifo_t;

struct pio_sim {
    // Instruction memory: 32 words, as loaded (jmp targets already relocated).
    uint16_t mem[32];

    // Configuration (as pio_sm_config would set it).
    unsigned wrap_bottom, wrap_top;
    unsigned set_base, set_count;
    unsigned out_base, out_count;
    unsigned in_base, in_count;          // in_count 1..32 (RP2350 IN_COUNT)
    unsigned jmp_pin;
    bool     out_shift_right, in_shift_right;
    unsigned pull_thresh, push_thresh;   // 1..32
    uint32_t clkdiv;                     // integer divider, >= 1
    uint32_t clk_phase;                  // executes when (cycle % clkdiv) == clk_phase
    unsigned sm;                         // index within the block, for REL irqs

    // State.
    bool     enabled;
    unsigned pc;
    uint32_t x, y, isr, osr;
    unsigned isr_count, osr_count;
    unsigned delay_left;
    pio_sim_fifo_t tx, rx;

    // IRQ flags: this block's, and the neighbours' (PREV / NEXT). Shared between
    // simulated state machines by pointing several sims at the same arrays.
    bool    *irq;        // [8]
    bool    *irq_prev;   // [8], may be NULL
    bool    *irq_next;   // [8], may be NULL

    // Pins.
    uint32_t out_level;   // what this SM drives (SET/OUT/MOV pins)
    uint32_t out_dir;     // pindirs
    uint32_t sync[PIO_SIM_SYNC_CYCLES + 1];   // input pipeline
    pio_sim_pins_fn pins;
    void    *pins_ctx;

    // Diagnostics.
    uint64_t cycle;       // system clocks simulated so far
    bool     error;       // an unmodelled instruction or a dropped push
    const char *error_text;
    unsigned rx_dropped;
    // Every IRQ this SM SET, by target: index 0-7 this block, 8-15 prev, 16-23 next.
    // The cycle of the most recent set, and how many.
    uint64_t irq_set_cycle[24];
    unsigned irq_set_count[24];
};

// A fresh SM with SDK-default config (wrap over the whole program, shifts right,
// thresholds 32, divider 1, IN_COUNT 32). `irq` must point at 8 bools.
void pio_sim_init(pio_sim_t *s, bool *irq);

// Copy `len` instructions to `offset`, relocating JMP targets the way the SDK's
// pio_add_program_at_offset() does.
void pio_sim_load(pio_sim_t *s, const uint16_t *prog, unsigned len, unsigned offset);

// Like pio_sm_exec(jmp target) on a disabled SM: set the PC.
void pio_sim_set_pc(pio_sim_t *s, unsigned pc);

bool pio_sim_tx_push(pio_sim_t *s, uint32_t v);
bool pio_sim_rx_pop(pio_sim_t *s, uint32_t *v);

// Advance one system clock.
void pio_sim_step(pio_sim_t *s);

// The level the GPIO input bank presents to the PIO right now, after the
// synchroniser (what `jmp pin` / `mov pins` / `wait` read this cycle).
uint32_t pio_sim_synced_inputs(const pio_sim_t *s);

#endif // PITRAC_PIO_SIM_H
