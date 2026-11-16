// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// link_proto.c -- see link_proto.h. Compiled into the host tests.

#include "link_proto.h"
#include "link_frame.h"
#include "fw_update.h"
#include "power_fsm.h"
#include "safe_state.h"
#include "adc_engine.h"
#include "telemetry.h"
#include "board.h"
#include "cli.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <string.h>

static size_t put8(uint8_t *p, size_t at, uint8_t v)   { p[at] = v; return at + 1u; }
static size_t put16(uint8_t *p, size_t at, uint16_t v) {
    p[at] = (uint8_t)v; p[at + 1u] = (uint8_t)(v >> 8);
    return at + 2u;
}
static size_t put32(uint8_t *p, size_t at, uint32_t v) {
    for (unsigned i = 0; i < 4u; i++) p[at + i] = (uint8_t)(v >> (8u * i));
    return at + 4u;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static size_t ping(uint8_t *r) {
    size_t n = put8(r, 0u, FWU_OK);
    n = put8(r, n, LINK_PROTO_VERSION);
    return put32(r, n, PITRAC_FW_VERSION);
}

static size_t info(uint8_t *r) {
    const fwu_layout_t *L = fwu_port_layout();
    fwu_status_t st;
    fwu_status(&st);
    size_t n = put8(r, 0u, FWU_OK);
    n = put32(r, n, PITRAC_FW_VERSION);
    // The image's identity, as `id` prints it: space-padded to 20 bytes.
    const char *stamp = cli_build_stamp();
    size_t sl = strlen(stamp);
    for (size_t i = 0; i < LINK_STAMP_BYTES; i++) r[n + i] = i < sl ? (uint8_t)stamp[i] : (uint8_t)' ';
    n += LINK_STAMP_BYTES;
    n = put8(r, n, L->boot_type);
    n = put8(r, n, (uint8_t)L->boot_partition);
    n = put8(r, n, (uint8_t)((L->partitioned ? 1u : 0u) | (st.trial ? 2u : 0u) | (st.bought ? 4u : 0u)));
    n = put32(r, n, L->run_start);
    n = put32(r, n, L->run_size);
    n = put32(r, n, L->partitioned ? L->other_start : 0u);
    n = put32(r, n, L->partitioned ? L->other_size : 0u);
    return put32(r, n, FWU_STAGE_BYTES);
}

static size_t status(uint8_t *r) {
    float v = 0.0f;
    uint16_t mv = 0xFFFFu;
    if (adc_5vin_read(&v) == ADC_5VIN_FRESH && v > 0.0f && v < 65.0f)
        mv = (uint16_t)(v * 1000.0f + 0.5f);
    const uint8_t flags = (uint8_t)((power_pi_present() ? 1u : 0u)
                        | (gpio_get_out_level(PIN_LATCH_CONTROL) ? 2u : 0u)
                        | (power_rails_ready() ? 4u : 0u));
    size_t n = put8(r, 0u, FWU_OK);
    n = put8(r, n, (uint8_t)power_fsm_state());
    n = put8(r, n, (uint8_t)fault_current());
    n = put16(r, n, mv);
    n = put8(r, n, flags);
    n = put32(r, n, to_ms_since_boot(get_absolute_time()));
    return put32(r, n, power_state_elapsed_ms());
}

static size_t hist(uint8_t *r) {
    size_t n = put8(r, 0u, FWU_OK);
    for (unsigned i = 0; i < TELE_WORDS; i++) n = put32(r, n, telemetry_word(i));
    return n;
}

static size_t upd_status(uint8_t *r) {
    fwu_status_t st;
    fwu_status(&st);
    size_t n = put8(r, 0u, FWU_OK);
    n = put8(r, n, (uint8_t)st.state);
    n = put8(r, n, (uint8_t)st.last_error);
    n = put32(r, n, st.size);
    n = put32(r, n, st.received);
    n = put32(r, n, st.staged_version);
    n = put8(r, n, (uint8_t)st.result);
    n = put32(r, n, st.result_from);
    n = put32(r, n, st.result_to);
    return put8(r, n, (uint8_t)st.result_detail);
}

size_t link_proto_handle(uint8_t type, const uint8_t *req, size_t req_len,
                         uint8_t *rsp, size_t cap) {
    if (type & LINK_RESPONSE) return 0u;            // a response, or our own echo
    if (!rsp || cap < LINK_MAX_PAYLOAD) return 0u;

    // Each request has one payload shape; a `break` below is the wrong one.
    switch (type) {
    case LINK_PING:       if (req_len) break; return ping(rsp);
    case LINK_INFO:       if (req_len) break; return info(rsp);
    case LINK_STATUS:     if (req_len) break; return status(rsp);
    case LINK_HIST:       if (req_len) break; return hist(rsp);
    case LINK_UPD_STATUS: if (req_len) break; return upd_status(rsp);

    case LINK_UPD_BEGIN: {
        if (req_len != 4u + 32u) break;
        fwu_err_t e = fwu_begin(get32(req), &req[4]);
        size_t n = put8(rsp, 0u, (uint8_t)e);
        return put32(rsp, n, LINK_DATA_MAX);
    }
    case LINK_UPD_DATA: {
        if (req_len < 5u || req_len > 4u + LINK_DATA_MAX) break;
        uint32_t next = 0u;
        fwu_err_t e = fwu_data(get32(req), &req[4], (uint32_t)(req_len - 4u), &next);
        size_t n = put8(rsp, 0u, (uint8_t)e);
        return put32(rsp, n, next);
    }
    case LINK_UPD_END: {
        if (req_len) break;
        uint32_t version = 0u;
        fwu_err_t e = fwu_end(&version);
        size_t n = put8(rsp, 0u, (uint8_t)e);
        return put32(rsp, n, version);
    }
    case LINK_UPD_APPLY:  if (req_len) break; return put8(rsp, 0u, (uint8_t)fwu_apply());
    case LINK_UPD_ABORT:  if (req_len) break; return put8(rsp, 0u, (uint8_t)fwu_abort());

    default:
        return put8(rsp, 0u, LINK_E_UNKNOWN);
    }
    return put8(rsp, 0u, LINK_E_LENGTH);
}
