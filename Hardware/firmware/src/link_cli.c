// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// link_cli.c -- see link_cli.h. Compiled into the host tests.

#include "link_cli.h"
#include "link_proto.h"

#include <string.h>

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

bool link_cli_valid_line(const uint8_t *p, size_t n) {
    if (!p || n == 0u || n > LINK_CLI_LINE_MAX) return false;
    for (size_t i = 0; i < n; i++)
        if (p[i] < 32u || p[i] > 126u) return false;
    return true;
}

size_t link_cli_put_result(uint8_t *rsp, const link_cli_result_t *r) {
    rsp[0] = r->status;
    rsp[1] = r->flags;
    put32(&rsp[2], r->session);
    put32(&rsp[6], r->ticket);
    put32(&rsp[10], r->out_bytes);
    rsp[14] = (uint8_t)r->out_crc;
    rsp[15] = (uint8_t)(r->out_crc >> 8);
    return LINK_CLI_RESULT_BYTES;
}

// A result for a command that did not run now, naming the (session, ticket) the
// request named -- 0, 0 only when the request was too short to name one.
static size_t refuse(uint8_t *rsp, uint8_t status, uint32_t session, uint32_t ticket) {
    const link_cli_result_t r = { status, 0u, session, ticket, 0u, 0u };
    return link_cli_put_result(rsp, &r);
}

void link_cli_book_init(link_cli_book_t *b, uint32_t session) {
    memset(b, 0, sizeof(*b));
    b->session = session;
}

bool link_cli_book_full(const link_cli_book_t *b) { return b->issued == UINT32_MAX; }

uint32_t link_cli_issue(link_cli_book_t *b) {
    // Ticket 0 is never issued (it marks an empty slot), and the count never
    // wraps: a full book issues nothing until the session is retired (image 1.15,
    // review note V114-N3). 1.14 relied on "136 years at one a second", which
    // nothing enforced: the count went on to 0 and then reused low numbers.
    if (link_cli_book_full(b)) return 0u;
    const uint32_t t = ++b->issued;
    link_cli_slot_t *s = &b->slot[t % LINK_CLI_BOOK];
    memset(s, 0, sizeof(*s));
    s->ticket = t;
    s->state  = LINK_CLI_T_ISSUED;
    return t;
}

// The slot that holds `ticket` in this session, or NULL with *status saying why
// not: LINK_E_SESSION (another session, or never issued), LINK_E_STALE (older
// than the book).
static link_cli_slot_t *find(link_cli_book_t *b, uint32_t session, uint32_t ticket, uint8_t *status) {
    if (session != b->session || ticket == 0u || ticket > b->issued) {
        *status = LINK_E_SESSION;
        return NULL;
    }
    link_cli_slot_t *s = &b->slot[ticket % LINK_CLI_BOOK];
    if (s->ticket != ticket) {
        *status = LINK_E_STALE;
        return NULL;
    }
    return s;
}

// What the record says of a ticket that is no longer ISSUED.
static size_t recorded(uint8_t *rsp, const link_cli_slot_t *s) {
    link_cli_result_t r = s->r;
    r.flags = (uint8_t)(r.flags | LINK_CLI_F_REPEAT);
    return link_cli_put_result(rsp, &r);
}

size_t link_cli_idle(link_cli_book_t *b, uint8_t type, const uint8_t *p, size_t n,
                     uint8_t *rsp, bool *run, uint32_t *ticket) {
    *run = false;
    *ticket = 0u;
    uint8_t why = 0u;

    switch (type) {
    case LINK_CLI_BEGIN: {
        if (n != 0u) return refuse(rsp, LINK_E_LENGTH, 0u, 0u);
        const uint32_t t = link_cli_issue(b);
        if (t == 0u) return refuse(rsp, LINK_E_SESSION, 0u, 0u);   // full: pi_link.c renews first
        rsp[0] = 0u;
        put32(&rsp[1], b->session);
        put32(&rsp[5], t);
        return LINK_CLI_BEGIN_BYTES;
    }

    case LINK_CLI_ABORT: {
        if (n != LINK_CLI_ID_BYTES) return refuse(rsp, LINK_E_LENGTH, 0u, 0u);
        const uint32_t sess = get32(p), t = get32(&p[4]);
        link_cli_slot_t *s = find(b, sess, t, &why);
        if (!s) return refuse(rsp, why, sess, t);
        if (s->state == LINK_CLI_T_ISSUED) {
            // Not run, and now never will: cancelled.
            s->state = LINK_CLI_T_CANCELLED;
            s->r = (link_cli_result_t){ 0u, LINK_CLI_F_CANCELLED, b->session, t, 0u, 0u };
            return link_cli_put_result(rsp, &s->r);
        }
        return recorded(rsp, s);        // done, or cancelled already
    }

    case LINK_CLI: {
        if (n < LINK_CLI_ID_BYTES + 1u) return refuse(rsp, LINK_E_LENGTH, 0u, 0u);
        const uint8_t *line = &p[LINK_CLI_ID_BYTES];
        const size_t   len  = n - LINK_CLI_ID_BYTES;
        const uint32_t sess = get32(p), t = get32(&p[4]);
        if (!link_cli_valid_line(line, len)) return refuse(rsp, LINK_E_LENGTH, sess, t);
        link_cli_slot_t *s = find(b, sess, t, &why);
        if (!s) return refuse(rsp, why, sess, t);
        const uint16_t crc = link_crc16(line, len, LINK_CRC_INIT);
        switch (s->state) {
        case LINK_CLI_T_ISSUED:
            // Marked before it runs: from here this ticket never runs again.
            s->state = LINK_CLI_T_RAN;
            s->len   = (uint8_t)len;
            s->crc   = crc;
            s->r     = (link_cli_result_t){ 0u, 0u, b->session, t, 0u, 0u };
            *run     = true;
            *ticket  = t;
            return 0u;
        case LINK_CLI_T_RAN:
            // A resend: the same line, answered from the record. A line of
            // another length or CRC-16 under a ticket already used is a client's
            // mistake: nothing runs. (Equal length and CRC is taken as the same
            // line; two lines can share both. The ticket, not this, is what keeps
            // a command from running twice.)
            if (s->len != len || s->crc != crc) return refuse(rsp, LINK_E_MISMATCH, sess, t);
            return recorded(rsp, s);
        default:
            return recorded(rsp, s);    // cancelled: it never runs
        }
    }

    default:
        return 0u;
    }
}

void link_cli_finish(link_cli_book_t *b, const link_cli_result_t *r) {
    link_cli_slot_t *s = &b->slot[r->ticket % LINK_CLI_BOOK];
    if (s->ticket != r->ticket || s->state != LINK_CLI_T_RAN) return;
    s->r = *r;
}

link_cli_rule_t link_cli_power_rule(int *argc, char **argv, const link_cli_power_t *now) {
    if (!argc || !argv || !now || *argc < 1) return LINK_CLI_ALLOW;
    const char *c = argv[0];
    const bool last_confirm = !strcmp(argv[*argc - 1], "confirm");

    if (!strcmp(c, "forceoff")) {
        // Exactly `forceoff confirm`.
        if (*argc == 2 && last_confirm) { *argc = 1; return LINK_CLI_ALLOW; }
        return LINK_CLI_NEEDS_CONFIRM;
    }
    if (!strcmp(c, "reset") || !strcmp(c, "bootsel")) {
        // Only the `force` forms skip the CLI's Pi guard; they need exactly
        // `<cmd> force confirm`. Any other form goes to that guard as typed.
        const bool force = *argc >= 2 && !strcmp(argv[1], "force");
        if (!force) return LINK_CLI_ALLOW;
        if (*argc == 3 && last_confirm) { *argc = 2; return LINK_CLI_ALLOW; }
        return LINK_CLI_NEEDS_CONFIRM;
    }
    if (!strcmp(c, "adc5vcal")) {
        // Alone it only prints its usage. With a value it sets the scale the
        // supply monitor judges +5V_IN by: exactly `adc5vcal <volts> confirm`.
        if (*argc == 1) return LINK_CLI_ALLOW;
        if (*argc == 3 && last_confirm) { *argc = 2; return LINK_CLI_ALLOW; }
        return LINK_CLI_NEEDS_CONFIRM;
    }
    if (!strcmp(c, "fault")) {
        // cmd_fault() clears on any `fault clear ...`. Exactly `fault clear
        // confirm` when that drops the rail; otherwise `confirm` is just removed.
        const bool clear = *argc >= 2 && !strcmp(argv[1], "clear");
        if (!clear) return LINK_CLI_ALLOW;
        if (*argc == 3 && last_confirm) { *argc = 2; return LINK_CLI_ALLOW; }
        return now->fault_ack_cuts ? LINK_CLI_NEEDS_CONFIRM : LINK_CLI_ALLOW;
    }
    if (!strcmp(c, "off")) {
        // cmd_off() ignores its arguments, so every `off ...` is judged alike.
        return now->off_orderly ? LINK_CLI_ALLOW : LINK_CLI_NOT_ORDERLY;
    }
    return LINK_CLI_ALLOW;
}

void link_cli_out_reset(link_cli_out_t *o) {
    if (!o) return;
    o->n    = 0u;
    o->sent = 0u;
    o->crc  = LINK_CRC_INIT;
}

static void emit_held(link_cli_out_t *o, link_cli_emit_fn emit, void *ctx) {
    if (emit) emit(o->sent, o->buf, o->n, ctx);
    o->crc   = link_crc16(o->buf, o->n, o->crc);
    o->sent += (uint32_t)o->n;
    o->n     = 0u;
}

void link_cli_out_put(link_cli_out_t *o, const char *s, size_t n, link_cli_emit_fn emit, void *ctx) {
    if (!o || !s) return;
    for (size_t i = 0; i < n; i++) {
        o->buf[o->n++] = (uint8_t)s[i];
        if (o->n == LINK_CLI_OUT_CHUNK) emit_held(o, emit, ctx);
    }
}

void link_cli_out_flush(link_cli_out_t *o, bool always, link_cli_emit_fn emit, void *ctx) {
    if (!o) return;
    if (o->n == 0u && !always) return;
    emit_held(o, emit, ctx);
}

// The frame just decoded is a CLI_ABORT for exactly (session, ticket).
static bool abort_for(const link_dec_t *d, uint32_t session, uint32_t ticket) {
    return d->type == LINK_CLI_ABORT && d->len == LINK_CLI_ID_BYTES &&
           get32(d->payload) == session && get32(&d->payload[4]) == ticket;
}

bool link_cli_scan(link_dec_t *d, const uint8_t *bytes, size_t n, uint32_t now_ms,
                   uint32_t session, uint32_t ticket, uint32_t *dropped) {
    bool stop = false;
    if (!d || !bytes) return false;
    for (size_t i = 0; i < n; i++) {
        if (link_dec_push(d, bytes[i], now_ms) != LINK_DEC_FRAME) continue;
        if (abort_for(d, session, ticket)) stop = true;
        else if (dropped) (*dropped)++;
    }
    return stop;
}

bool link_cli_abort_ahead(link_dec_t *scratch, const uint8_t *bytes, size_t n, uint32_t now_ms,
                          uint32_t session, uint32_t ticket) {
    if (!scratch || !bytes) return false;
    for (size_t i = 0; i < n; i++)
        if (link_dec_push(scratch, bytes[i], now_ms) == LINK_DEC_FRAME &&
            abort_for(scratch, session, ticket))
            return true;
    return false;
}
