// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// pio_sim.c -- see pio_sim.h. Instruction encodings and semantics from the
// RP2350 datasheet, chapter 11 (PIO). The encodings are cross-checked against
// pioasm's own output in tests/cam_pio_test.c ("encodings").

#include "pio_sim.h"

#include <string.h>

static uint32_t mask_bits(unsigned n) { return n >= 32u ? 0xFFFFFFFFu : ((1u << n) - 1u); }

static uint32_t bitrev(uint32_t v) {
    uint32_t r = 0;
    for (int i = 0; i < 32; i++) { r = (r << 1) | (v & 1u); v >>= 1; }
    return r;
}

static void fail(pio_sim_t *s, const char *why) {
    if (!s->error) s->error_text = why;
    s->error = true;
}

void pio_sim_init(pio_sim_t *s, bool *irq) {
    memset(s, 0, sizeof(*s));
    s->wrap_bottom = 0;
    s->wrap_top = 31;
    s->set_count = 1;
    s->out_count = 32;
    s->in_count = 32;
    s->out_shift_right = true;
    s->in_shift_right = true;
    s->pull_thresh = 32;
    s->push_thresh = 32;
    s->clkdiv = 1;
    s->irq = irq;
}

void pio_sim_load(pio_sim_t *s, const uint16_t *prog, unsigned len, unsigned offset) {
    for (unsigned i = 0; i < len; i++) {
        uint16_t w = prog[i];
        if ((w >> 13) == 0u)   // JMP: relocate the 5-bit target, as the SDK does
            w = (uint16_t)((w & ~0x1Fu) | ((w + offset) & 0x1Fu));
        s->mem[(offset + i) & 31u] = w;
    }
}

void pio_sim_set_pc(pio_sim_t *s, unsigned pc) { s->pc = pc & 31u; }

static bool fifo_push(pio_sim_fifo_t *f, uint32_t v) {
    if (f->count >= PIO_SIM_FIFO_DEPTH) return false;
    f->data[(f->head + f->count) % PIO_SIM_FIFO_DEPTH] = v;
    f->count++;
    return true;
}

static bool fifo_pop(pio_sim_fifo_t *f, uint32_t *v) {
    if (f->count == 0u) return false;
    *v = f->data[f->head];
    f->head = (f->head + 1u) % PIO_SIM_FIFO_DEPTH;
    f->count--;
    return true;
}

bool pio_sim_tx_push(pio_sim_t *s, uint32_t v) { return fifo_push(&s->tx, v); }
bool pio_sim_rx_pop(pio_sim_t *s, uint32_t *v) { return fifo_pop(&s->rx, v); }

uint32_t pio_sim_synced_inputs(const pio_sim_t *s) { return s->sync[PIO_SIM_SYNC_CYCLES]; }

static uint32_t in_pins(const pio_sim_t *s) {
    uint32_t v = pio_sim_synced_inputs(s);
    unsigned b = s->in_base & 31u;
    if (b) v = (v >> b) | (v << (32u - b));
    return v & mask_bits(s->in_count);
}

static void write_pins(pio_sim_t *s, unsigned base, unsigned count, uint32_t v, bool dirs) {
    for (unsigned i = 0; i < count; i++) {
        uint32_t bit = 1u << ((base + i) & 31u);
        uint32_t *t = dirs ? &s->out_dir : &s->out_level;
        if ((v >> i) & 1u) *t |= bit; else *t &= ~bit;
    }
}

// The flag an IRQ index names, and the slot it is recorded under.
static bool *irq_target(pio_sim_t *s, unsigned idx, unsigned *slot) {
    unsigned mode = (idx >> 3) & 3u, num = idx & 7u;
    switch (mode) {
        case 0: *slot = num; return &s->irq[num];
        case 1: *slot = 8u + num;  return s->irq_prev ? &s->irq_prev[num] : NULL;
        case 2: num = (num & 4u) | ((num + s->sm) & 3u); *slot = num; return &s->irq[num];
        default: *slot = 16u + num; return s->irq_next ? &s->irq_next[num] : NULL;
    }
}

// Execute the instruction at the PC. Returns false if it stalled.
static bool execute(pio_sim_t *s, bool *jumped) {
    uint16_t in = s->mem[s->pc];
    unsigned op = in >> 13, a1 = (in >> 5) & 7u, a2 = in & 0x1Fu;
    uint32_t synced = pio_sim_synced_inputs(s);

    switch (op) {
    case 0: {   // JMP
        bool take = false;
        switch (a1) {
            case 0: take = true; break;
            case 1: take = s->x == 0u; break;
            case 2: take = s->x != 0u; s->x--; break;
            case 3: take = s->y == 0u; break;
            case 4: take = s->y != 0u; s->y--; break;
            case 5: take = s->x != s->y; break;
            case 6: take = ((synced >> (s->jmp_pin & 31u)) & 1u) != 0u; break;
            default: take = s->osr_count < s->pull_thresh; break;
        }
        if (take) { s->pc = a2; *jumped = true; }
        return true;
    }
    case 1: {   // WAIT
        unsigned pol = (in >> 7) & 1u, src = (in >> 5) & 3u;
        bool level;
        if (src == 2u) {
            unsigned slot;
            bool *f = irq_target(s, a2, &slot);
            if (!f) { fail(s, "wait on an irq block that is not modelled"); return true; }
            if (pol) { if (!*f) return false; *f = false; return true; }
            return !*f;
        }
        unsigned pin = src == 0u ? a2 : src == 1u ? s->in_base + a2 : s->jmp_pin + a2;
        level = ((synced >> (pin & 31u)) & 1u) != 0u;
        return level == (pol != 0u);
    }
    case 2: {   // IN
        unsigned n = a2 ? a2 : 32u;
        uint32_t d;
        switch (a1) {
            case 0: d = in_pins(s); break;
            case 1: d = s->x; break;
            case 2: d = s->y; break;
            case 3: d = 0u; break;
            case 6: d = s->isr; break;
            case 7: d = s->osr; break;
            default: fail(s, "IN from a reserved source"); return true;
        }
        d &= mask_bits(n);
        if (n == 32u) s->isr = d;
        else if (s->in_shift_right) s->isr = (s->isr >> n) | (d << (32u - n));
        else s->isr = (s->isr << n) | d;
        s->isr_count = s->isr_count + n > 32u ? 32u : s->isr_count + n;
        return true;
    }
    case 3: {   // OUT
        unsigned n = a2 ? a2 : 32u;
        uint32_t d;
        if (s->out_shift_right) { d = s->osr & mask_bits(n); s->osr = n == 32u ? 0u : s->osr >> n; }
        else { d = n == 32u ? s->osr : s->osr >> (32u - n); s->osr = n == 32u ? 0u : s->osr << n; }
        s->osr_count = s->osr_count + n > 32u ? 32u : s->osr_count + n;
        switch (a1) {
            case 0: write_pins(s, s->out_base, n, d, false); break;
            case 1: s->x = d; break;
            case 2: s->y = d; break;
            case 3: break;
            case 4: write_pins(s, s->out_base, n, d, true); break;
            case 5: s->pc = d & 31u; *jumped = true; break;
            case 6: s->isr = d; s->isr_count = n; break;
            default: fail(s, "OUT EXEC is not modelled"); break;
        }
        return true;
    }
    case 4: {   // PUSH / PULL
        if (in & 0x10u) { fail(s, "MOV to/from the RX FIFO is not modelled"); return true; }
        bool pull = (in >> 7) & 1u, cond = (in >> 6) & 1u, block = (in >> 5) & 1u;
        if (!pull) {
            if (cond && s->isr_count < s->push_thresh) return true;
            if (s->rx.count >= PIO_SIM_FIFO_DEPTH) {
                if (block) return false;
                s->rx_dropped++;
                fail(s, "push noblock into a full RX FIFO: a word was lost");
            } else {
                fifo_push(&s->rx, s->isr);
            }
            s->isr = 0u;
            s->isr_count = 0u;
            return true;
        }
        if (cond && s->osr_count < s->pull_thresh) return true;
        uint32_t v;
        if (!fifo_pop(&s->tx, &v)) {
            if (block) return false;
            v = s->x;
        }
        s->osr = v;
        s->osr_count = 0u;
        return true;
    }
    case 5: {   // MOV
        unsigned opn = (in >> 3) & 3u, src = in & 7u;
        uint32_t d;
        switch (src) {
            case 0: d = in_pins(s); break;
            case 1: d = s->x; break;
            case 2: d = s->y; break;
            case 3: d = 0u; break;
            case 5: d = 0u; break;      // STATUS: not modelled beyond "all zeroes"
            case 6: d = s->isr; break;
            case 7: d = s->osr; break;
            default: fail(s, "MOV from a reserved source"); return true;
        }
        if (opn == 1u) d = ~d;
        else if (opn == 2u) d = bitrev(d);
        else if (opn == 3u) { fail(s, "MOV with a reserved operation"); return true; }
        switch (a1) {
            case 0: write_pins(s, s->out_base, s->out_count, d, false); break;
            case 1: s->x = d; break;
            case 2: s->y = d; break;
            case 3: write_pins(s, s->out_base, s->out_count, d, true); break;
            case 5: s->pc = d & 31u; *jumped = true; break;
            case 6: s->isr = d; s->isr_count = 0u; break;
            case 7: s->osr = d; s->osr_count = 0u; break;
            default: fail(s, "MOV EXEC is not modelled"); break;
        }
        return true;
    }
    case 6: {   // IRQ
        if (in & 0x80u) { fail(s, "IRQ with a reserved bit set"); return true; }
        bool clr = (in >> 6) & 1u, wt = (in >> 5) & 1u;
        unsigned slot;
        bool *f = irq_target(s, a2, &slot);
        if (!f) { fail(s, "irq to a block that is not modelled"); return true; }
        if (clr) { *f = false; return true; }
        if (wt) { fail(s, "irq set + wait is not modelled"); return true; }
        *f = true;
        s->irq_set_cycle[slot] = s->cycle;
        s->irq_set_count[slot]++;
        return true;
    }
    default: {  // SET
        switch (a1) {
            case 0: write_pins(s, s->set_base, s->set_count, a2, false); break;
            case 1: s->x = a2; break;
            case 2: s->y = a2; break;
            case 4: write_pins(s, s->set_base, s->set_count, a2, true); break;
            default: fail(s, "SET to a reserved destination"); break;
        }
        return true;
    }
    }
}

void pio_sim_step(pio_sim_t *s) {
    // The synchroniser: what the PIO reads now is what the pads showed
    // PIO_SIM_SYNC_CYCLES clocks ago.
    uint32_t raw = s->pins ? s->pins(s->cycle, s->pins_ctx) : 0u;
    for (unsigned i = PIO_SIM_SYNC_CYCLES; i > 0u; i--) s->sync[i] = s->sync[i - 1u];
    s->sync[0] = raw;

    if (s->enabled && (s->clkdiv <= 1u || s->cycle % s->clkdiv == s->clk_phase)) {
        if (s->delay_left) {
            s->delay_left--;
        } else {
            unsigned delay = (s->mem[s->pc] >> 8) & 0x1Fu;
            bool jumped = false;
            if (execute(s, &jumped)) {
                if (!jumped) s->pc = s->pc == s->wrap_top ? s->wrap_bottom : (s->pc + 1u) & 31u;
                s->delay_left = delay;
            }
        }
    }
    s->cycle++;
}
