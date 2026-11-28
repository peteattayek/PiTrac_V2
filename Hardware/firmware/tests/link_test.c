// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// Host tests for the Pi link's framing (link_frame.c), the pure part of the Pi's
// CLI over it (link_cli.c, protocol 3) and the SHA-256 the update checks images
// with (sha256.c). No mocks: all three are pure. The frame bytes below are also
// in pi/tests/test_pitrac_link.py, so the two ends are tested against the same
// vectors.
#include "link_frame.h"
#include "link_cli.h"
#include "link_proto.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *test_name;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, test_name, #condition); \
        return false; \
    } \
} while (0)

// Feed bytes; return the result of the last push, and how many frames came out.
static link_dec_result_t feed(link_dec_t *d, const uint8_t *p, size_t n, uint32_t t, unsigned *frames) {
    link_dec_result_t r = LINK_DEC_NONE;
    for (size_t i = 0; i < n; i++) {
        r = link_dec_push(d, p[i], t);
        if (r == LINK_DEC_FRAME && frames) (*frames)++;
    }
    return r;
}

// ---------------------------------------------------------------------------

static bool crc_check_value(void) {
    // CRC-16/CCITT-FALSE's catalogue check value.
    CHECK(link_crc16((const uint8_t *)"123456789", 9u, LINK_CRC_INIT) == 0x29B1u);
    // Continuing a CRC is the same as one pass.
    uint16_t c = link_crc16((const uint8_t *)"1234", 4u, LINK_CRC_INIT);
    CHECK(link_crc16((const uint8_t *)"56789", 5u, c) == 0x29B1u);
    return true;
}

static bool encode_vectors(void) {
    // The same bytes as pi/tests/test_pitrac_link.py.
    static const uint8_t ping[] = { 0xA5, 0x00, 0x01, 0x07, 0x4A, 0x8F };
    static const uint8_t pong[] = { 0xA5, 0x06, 0x81, 0x07, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x38, 0x0E };
    uint8_t out[LINK_MAX_FRAME];
    CHECK(link_frame_encode(out, sizeof(out), 0x01, 0x07, NULL, 0u) == sizeof(ping));
    CHECK(memcmp(out, ping, sizeof(ping)) == 0);
    static const uint8_t pl[] = { 0x00, 0x01, 0x01, 0x00, 0x01, 0x00 };
    CHECK(link_frame_encode(out, sizeof(out), 0x81, 0x07, pl, sizeof(pl)) == sizeof(pong));
    CHECK(memcmp(out, pong, sizeof(pong)) == 0);
    return true;
}

static bool encode_limits(void) {
    uint8_t big[300] = { 0 };
    uint8_t out[LINK_MAX_FRAME + 8];
    CHECK(link_frame_encode(out, sizeof(out), 0x11, 0, big, 255u) == LINK_MAX_FRAME);
    CHECK(link_frame_encode(out, sizeof(out), 0x11, 0, big, 256u) == 0u);      // len is a byte
    CHECK(link_frame_encode(out, 5u, 0x01, 0, NULL, 0u) == 0u);                // no room for the CRC
    CHECK(link_frame_encode(out, 6u, 0x01, 0, NULL, 0u) == 6u);
    return true;
}

static bool decode_round_trip(void) {
    static const size_t lens[] = { 0u, 1u, 2u, 200u, 255u };
    for (size_t k = 0; k < sizeof(lens) / sizeof(lens[0]); k++) {
        uint8_t pl[255], fr[LINK_MAX_FRAME];
        for (size_t i = 0; i < lens[k]; i++) pl[i] = (uint8_t)(i * 37u + k);
        size_t n = link_frame_encode(fr, sizeof(fr), 0x11, (uint8_t)(k + 100u), pl, lens[k]);
        CHECK(n == lens[k] + 6u);
        link_dec_t d;
        link_dec_init(&d);
        unsigned frames = 0;
        CHECK(feed(&d, fr, n, 1000u, &frames) == LINK_DEC_FRAME);
        CHECK(frames == 1u);
        CHECK(d.type == 0x11 && d.seq == (uint8_t)(k + 100u) && d.len == lens[k]);
        CHECK(memcmp(d.payload, pl, lens[k]) == 0);
        CHECK(d.frames == 1u && d.crc_errors == 0u);
    }
    return true;
}

// A single-bit error anywhere after the sync byte must not decode. In the type,
// seq, payload or CRC bytes a CRC-16 is guaranteed to catch it; a flipped length
// byte reframes the stream instead, and this vector shows that none of those
// decode either.
static bool every_bit_flip_rejected(void) {
    uint8_t pl[40], fr[LINK_MAX_FRAME];
    for (size_t i = 0; i < sizeof(pl); i++) pl[i] = (uint8_t)(0xA5u ^ i);
    const size_t n = link_frame_encode(fr, sizeof(fr), 0x10, 9, pl, sizeof(pl));
    for (size_t byte = 1; byte < n; byte++) {
        for (unsigned bit = 0; bit < 8u; bit++) {
            uint8_t bad[LINK_MAX_FRAME];
            memcpy(bad, fr, n);
            bad[byte] ^= (uint8_t)(1u << bit);
            link_dec_t d;
            link_dec_init(&d);
            unsigned frames = 0;
            // A flipped length makes the decoder wait for more: push zeros after
            // it; they can only complete a frame through the CRC as well.
            (void)feed(&d, bad, n, 0u, &frames);
            static const uint8_t pad[300] = { 0 };
            (void)feed(&d, pad, sizeof(pad), 0u, &frames);
            CHECK(frames == 0u);
        }
    }
    return true;
}

static bool resync_after_junk(void) {
    uint8_t stream[600];
    size_t at = 0;
    static const uint8_t junk[] = { 0x00, 0xFF, 0x13, 0xA5, 0x03, 0x02, 0x01, 0x55 };   // one bogus sync
    memcpy(&stream[at], junk, sizeof(junk)); at += sizeof(junk);
    // The bogus frame above wants 3 payload bytes and a CRC: these complete it,
    // it fails, and the decoder hunts again.
    static const uint8_t fill[] = { 0x10, 0x20, 0x30, 0x40, 0x50 };
    memcpy(&stream[at], fill, sizeof(fill)); at += sizeof(fill);
    const uint8_t pl[] = { 1, 2, 3 };
    at += link_frame_encode(&stream[at], sizeof(stream) - at, 0x02, 0x44, pl, sizeof(pl));
    link_dec_t d;
    link_dec_init(&d);
    unsigned frames = 0;
    (void)feed(&d, stream, at, 5u, &frames);
    CHECK(frames == 1u);
    CHECK(d.type == 0x02 && d.seq == 0x44 && d.len == 3u);
    CHECK(d.crc_errors == 1u);
    CHECK(d.junk >= 3u);
    return true;
}

static bool corrupt_then_next(void) {
    uint8_t a[LINK_MAX_FRAME], b[LINK_MAX_FRAME];
    const uint8_t pl[] = { 9, 8, 7, 6 };
    size_t na = link_frame_encode(a, sizeof(a), 0x03, 1, pl, sizeof(pl));
    size_t nb = link_frame_encode(b, sizeof(b), 0x04, 2, pl, sizeof(pl));
    a[6] ^= 0x40u;                                   // a payload byte
    link_dec_t d;
    link_dec_init(&d);
    unsigned frames = 0;
    CHECK(feed(&d, a, na, 0u, &frames) == LINK_DEC_BAD_CRC);
    CHECK(feed(&d, b, nb, 0u, &frames) == LINK_DEC_FRAME);
    CHECK(frames == 1u && d.type == 0x04 && d.seq == 2u);
    return true;
}

// A frame cut short must not swallow the start of the next one once the line
// has been quiet for LINK_BYTE_GAP_MS.
static bool gap_drops_partial(void) {
    uint8_t a[LINK_MAX_FRAME], b[LINK_MAX_FRAME];
    const uint8_t pl[10] = { 0 };
    size_t na = link_frame_encode(a, sizeof(a), 0x11, 1, pl, sizeof(pl));
    size_t nb = link_frame_encode(b, sizeof(b), 0x01, 2, NULL, 0u);
    link_dec_t d;
    link_dec_init(&d);
    unsigned frames = 0;
    (void)feed(&d, a, na - 3u, 100u, &frames);         // cut short
    // Exactly at the gap: still the same frame (its CRC then fails)...
    link_dec_t e = d;
    (void)feed(&e, b, nb, 100u + LINK_BYTE_GAP_MS, &frames);
    CHECK(frames == 0u);
    // ...past it: dropped, and the new frame decodes.
    CHECK(feed(&d, b, nb, 101u + LINK_BYTE_GAP_MS, &frames) == LINK_DEC_FRAME);
    CHECK(frames == 1u && d.gap_drops == 1u && d.type == 0x01);
    return true;
}

static bool reset_keeps_counters(void) {
    uint8_t a[LINK_MAX_FRAME];
    size_t na = link_frame_encode(a, sizeof(a), 0x01, 1, NULL, 0u);
    link_dec_t d;
    link_dec_init(&d);
    unsigned frames = 0;
    (void)feed(&d, a, na, 0u, &frames);
    (void)feed(&d, a, 3u, 0u, &frames);                // a partial frame
    link_dec_reset(&d);
    CHECK(feed(&d, a, na, 0u, &frames) == LINK_DEC_FRAME);
    CHECK(d.frames == 2u && frames == 2u);
    return true;
}

// The RX ring is read only up to a write index at least LINK_RX_SETTLE_US old.
static bool rx_settle(void) {
    link_rx_snap_t s;
    memset(&s, 0, sizeof(s));
    uint32_t upto = 0xDEADu;
    CHECK(!link_rx_settled(&s, 10u, 1000u, &upto));                   // first: snapshot only
    CHECK(upto == 0xDEADu);
    CHECK(!link_rx_settled(&s, 15u, 1000u + LINK_RX_SETTLE_US - 1u, &upto));   // too young
    CHECK(link_rx_settled(&s, 20u, 1000u + LINK_RX_SETTLE_US, &upto));
    CHECK(upto == 10u);                                               // the OLD index
    // The snapshot is now 20, taken at 1020: not before 1040.
    CHECK(!link_rx_settled(&s, 30u, 1030u, &upto));
    CHECK(upto == 10u);
    CHECK(link_rx_settled(&s, 31u, 1040u + LINK_RX_SETTLE_US, &upto));
    CHECK(upto == 20u);
    return true;
}

// ---------------------------------------------------------------------------
// SHA-256: FIPS 180-4 example vectors, and chunking that must not matter.

static bool hex_is(const uint8_t *d, const char *hex) {
    char s[65];
    for (unsigned i = 0; i < 32u; i++) snprintf(&s[2u * i], 3, "%02x", d[i]);
    return strcmp(s, hex) == 0;
}

static bool sha_vectors(void) {
    static const struct { const char *msg; const char *hex; } v[] = {
        { "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
        { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" },
        { "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
          "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1" },
    };
    for (size_t k = 0; k < sizeof(v) / sizeof(v[0]); k++) {
        sha256_t s;
        uint8_t d[32];
        sha256_init(&s);
        sha256_update(&s, v[k].msg, strlen(v[k].msg));
        sha256_final(&s, d);
        CHECK(hex_is(d, v[k].hex));
    }
    return true;
}

static bool sha_million_a_chunked(void) {
    static uint8_t a[1000];
    memset(a, 'a', sizeof(a));
    // 1,000,000 'a' in chunks of awkward sizes: 1, 63, 64, 65, 251 ...
    static const size_t chunks[] = { 1u, 63u, 64u, 65u, 251u, 1000u, 7u };
    sha256_t s;
    sha256_init(&s);
    size_t done = 0, k = 0;
    while (done < 1000000u) {
        size_t n = chunks[k++ % (sizeof(chunks) / sizeof(chunks[0]))];
        if (n > 1000000u - done) n = 1000000u - done;
        sha256_update(&s, a, n);
        done += n;
    }
    uint8_t d[32];
    sha256_final(&s, d);
    CHECK(hex_is(d, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    return true;
}

// Lengths around the 55/56/64-byte padding boundaries, one-shot against
// byte-at-a-time.
static bool sha_padding_boundaries(void) {
    uint8_t msg[130];
    for (size_t i = 0; i < sizeof(msg); i++) msg[i] = (uint8_t)(i * 7u + 1u);
    for (size_t n = 50; n <= 130; n++) {
        sha256_t s1, s2;
        uint8_t d1[32], d2[32];
        sha256_init(&s1);
        sha256_update(&s1, msg, n);
        sha256_final(&s1, d1);
        sha256_init(&s2);
        for (size_t i = 0; i < n; i++) sha256_update(&s2, &msg[i], 1u);
        sha256_final(&s2, d2);
        CHECK(memcmp(d1, d2, 32u) == 0);
    }
    // And one known value at exactly 64 bytes ("a" x 64).
    uint8_t a64[64];
    memset(a64, 'a', sizeof(a64));
    sha256_t s;
    uint8_t d[32];
    sha256_init(&s);
    sha256_update(&s, a64, sizeof(a64));
    sha256_final(&s, d);
    CHECK(hex_is(d, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));
    return true;
}

// ---------------------------------------------------------------------------

typedef bool (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } test_case;
#define TEST(name) { #name, name }

// ---------------------------------------------------------------------------
// The Pi's CLI (link_cli.c, protocol 4).

static bool cli_valid_line(void) {
    static uint8_t buf[LINK_CLI_LINE_MAX + 2u];
    CHECK(link_cli_valid_line((const uint8_t *)"id", 2u));
    CHECK(link_cli_valid_line((const uint8_t *)" ", 1u));                  // printable; splits to nothing
    CHECK(link_cli_valid_line((const uint8_t *)"~", 1u));
    CHECK(!link_cli_valid_line((const uint8_t *)"", 0u));
    CHECK(!link_cli_valid_line(NULL, 2u));
    memset(buf, 'a', sizeof(buf));
    CHECK(link_cli_valid_line(buf, LINK_CLI_LINE_MAX));                    // 95: the USB buffer's
    CHECK(!link_cli_valid_line(buf, LINK_CLI_LINE_MAX + 1u));
    static const uint8_t bad[] = { '\t', '\n', '\r', 0x00, 0x1F, 0x7F, 0x80, 0xFF };
    for (size_t k = 0; k < sizeof(bad); k++) {
        uint8_t l[3] = { 'i', bad[k], 'd' };
        CHECK(!link_cli_valid_line(l, 3u));
    }
    return true;
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// A CLI or CLI_ABORT payload: session, ticket, then the line (NULL for none).
static size_t req_id(uint8_t *p, uint32_t session, uint32_t ticket, const char *line) {
    wr32(p, session);
    wr32(&p[4], ticket);
    size_t n = LINK_CLI_ID_BYTES;
    if (line) { memcpy(&p[n], line, strlen(line)); n += strlen(line); }
    return n;
}

// The CLI family's answer, decoded (protocol 4: the session at [2], image 1.16).
typedef struct { uint8_t status, flags; uint32_t session, ticket, out_bytes; uint16_t out_crc; } res_t;
static res_t res_of(const uint8_t *r) {
    res_t x = { r[0], r[1], rd32(&r[2]), rd32(&r[6]), rd32(&r[10]), (uint16_t)(r[14] | (r[15] << 8)) };
    return x;
}

// A miniature board: the book, and a log of what each released ticket ran.
static link_cli_book_t g_book;
static char     g_ran[32][LINK_CLI_LINE_MAX + 1u];
static unsigned g_runs;
static uint8_t  g_rsp[LINK_MAX_PAYLOAD];

static uint32_t begin_ticket(void) {
    bool run = true;
    uint32_t t = 99u;
    size_t n = link_cli_idle(&g_book, LINK_CLI_BEGIN, NULL, 0u, g_rsp, &run, &t);
    if (n != LINK_CLI_BEGIN_BYTES || g_rsp[0] != 0u || run) return 0u;
    if (rd32(&g_rsp[1]) != g_book.session) return 0u;
    return rd32(&g_rsp[5]);
}

// Send a CLI request to the miniature board. If the book releases it, "run" it
// (logged), and finish it with `flags`. Returns the answer the Pi would get.
static res_t send_cli(uint32_t session, uint32_t ticket, const char *line, uint8_t flags) {
    uint8_t p[LINK_MAX_PAYLOAD];
    size_t n = req_id(p, session, ticket, line);
    bool run = false;
    uint32_t t = 0u;
    size_t len = link_cli_idle(&g_book, LINK_CLI, p, n, g_rsp, &run, &t);
    if (run) {
        snprintf(g_ran[g_runs++ % 32u], LINK_CLI_LINE_MAX + 1u, "%.*s", (int)(n - LINK_CLI_ID_BYTES),
                 (const char *)&p[LINK_CLI_ID_BYTES]);
        const link_cli_result_t r = { 0u, flags, g_book.session, t, 7u * g_runs,
                                      (uint16_t)(0x1000u + g_runs) };
        link_cli_finish(&g_book, &r);
        link_cli_put_result(g_rsp, &r);
    } else if (len != LINK_CLI_RESULT_BYTES) {
        memset(g_rsp, 0xEE, LINK_CLI_RESULT_BYTES);
    }
    return res_of(g_rsp);
}

static res_t send_abort(uint32_t session, uint32_t ticket) {
    uint8_t p[16];
    size_t n = req_id(p, session, ticket, NULL);
    bool run = true;
    uint32_t t = 0u;
    size_t len = link_cli_idle(&g_book, LINK_CLI_ABORT, p, n, g_rsp, &run, &t);
    if (run || len != LINK_CLI_RESULT_BYTES) memset(g_rsp, 0xEE, LINK_CLI_RESULT_BYTES);
    return res_of(g_rsp);
}

static void fresh_board(uint32_t session) {
    link_cli_book_init(&g_book, session);
    memset(g_ran, 0, sizeof(g_ran));
    g_runs = 0u;
}

// A ticket runs once; its resend is answered from the record; the same ticket
// with another line runs nothing.
static bool cli_ticket_once(void) {
    fresh_board(0xC0FFEEu);
    const uint32_t t = begin_ticket();
    CHECK(t == 1u);
    res_t r = send_cli(0xC0FFEEu, t, "strobe pulse 20", LINK_CLI_F_STOP_ASKED);
    CHECK(g_runs == 1u && !strcmp(g_ran[0], "strobe pulse 20"));
    CHECK(r.status == 0u && r.flags == LINK_CLI_F_STOP_ASKED && r.ticket == t);
    r = send_cli(0xC0FFEEu, t, "strobe pulse 20", 0u);                    // the resend
    CHECK(g_runs == 1u);                                                    // not run again
    CHECK(r.status == 0u && r.flags == (LINK_CLI_F_STOP_ASKED | LINK_CLI_F_REPEAT));
    CHECK(r.out_bytes == 7u && r.out_crc == 0x1001u);                       // as recorded
    r = send_cli(0xC0FFEEu, t, "strobe pulse 21", 0u);                    // same length, other text
    CHECK(g_runs == 1u && r.status == LINK_E_MISMATCH && r.ticket == t);
    r = send_cli(0xC0FFEEu, t, "strobe pulse 2", 0u);                     // other length
    CHECK(g_runs == 1u && r.status == LINK_E_MISMATCH);
    // A finish for a ticket that is not running changes nothing.
    const link_cli_result_t bogus = { 0u, LINK_CLI_F_STOPPED, g_book.session, t + 1u, 1u, 1u };
    link_cli_finish(&g_book, &bogus);
    r = send_cli(0xC0FFEEu, t, "strobe pulse 20", 0u);
    CHECK(r.flags == (LINK_CLI_F_STOP_ASKED | LINK_CLI_F_REPEAT));
    return true;
}

// V113-F1, the reviewer's first case: client A's answer to `stat` is lost,
// client B runs `id`, A resends. 1.13 ran stat, id, stat; now stat runs once.
static bool cli_two_clients(void) {
    fresh_board(0x1234u);
    const uint32_t a = begin_ticket(), b = begin_ticket();
    CHECK(a && b && a != b);
    (void)send_cli(0x1234u, a, "stat", 0u);                                // A's answer is lost
    (void)send_cli(0x1234u, b, "id", 0u);                                  // B, in between
    res_t r = send_cli(0x1234u, a, "stat", 0u);                            // A sends again
    CHECK(g_runs == 2u && !strcmp(g_ran[0], "stat") && !strcmp(g_ran[1], "id"));
    CHECK(r.status == 0u && (r.flags & LINK_CLI_F_REPEAT) && r.ticket == a);
    r = send_cli(0x1234u, b, "id", 0u);
    CHECK(g_runs == 2u && (r.flags & LINK_CLI_F_REPEAT));
    // One client's ticket cannot be used for another's line either.
    r = send_cli(0x1234u, b, "stat", 0u);
    CHECK(g_runs == 2u && r.status == LINK_E_MISMATCH);
    return true;
}

// V113-F1, the reviewer's second case: after a `stat` and 255 other requests,
// a fresh `stat` was taken for the old one (the 8-bit seq wrapped). Tickets do
// not wrap: the fresh one runs, and the record keeps LINK_CLI_BOOK tickets --
// an older one is refused as stale, and runs nothing.
static bool cli_wrap_and_age(void) {
    fresh_board(77u);
    const uint32_t first = begin_ticket();
    (void)send_cli(77u, first, "stat", 0u);
    for (unsigned k = 0; k < LINK_CLI_BOOK - 1u; k++) CHECK(begin_ticket() != 0u);
    res_t r = send_cli(77u, first, "stat", 0u);                            // still in the book
    CHECK(g_runs == 1u && (r.flags & LINK_CLI_F_REPEAT));
    CHECK(begin_ticket() == first + LINK_CLI_BOOK);                         // displaces `first`
    r = send_cli(77u, first, "stat", 0u);
    CHECK(g_runs == 1u && r.status == LINK_E_STALE && r.ticket == first);
    r = send_abort(77u, first);
    CHECK(r.status == LINK_E_STALE);
    for (unsigned k = 0; k < 300u; k++) CHECK(begin_ticket() != 0u);       // past any 8-bit wrap
    const uint32_t fresh = begin_ticket();
    r = send_cli(77u, fresh, "stat", 0u);
    CHECK(g_runs == 2u && r.status == 0u && !(r.flags & LINK_CLI_F_REPEAT));
    // Issued but never sent, then displaced: it never runs.
    const uint32_t unused = begin_ticket();
    for (unsigned k = 0; k < LINK_CLI_BOOK; k++) (void)begin_ticket();
    r = send_cli(77u, unused, "id", 0u);
    CHECK(g_runs == 2u && r.status == LINK_E_STALE);
    return true;
}

// V114-N3: a session's count never wraps. 1.14 went on from 2^32 - 1 to 0 and
// then reissued 1, 2, ... in the same session. Now the last number is issued,
// then nothing -- the book is full, and pi_link.c starts a new session -- and
// every ticket already issued keeps its record.
static bool cli_book_never_wraps(void) {
    fresh_board(0x5E55u);
    g_book.issued = UINT32_MAX - 2u;
    const uint32_t a = begin_ticket();
    CHECK(a == UINT32_MAX - 1u && !link_cli_book_full(&g_book));
    res_t r = send_cli(0x5E55u, a, "stat", 0u);
    CHECK(g_runs == 1u && r.status == 0u);
    const uint32_t last = begin_ticket();
    CHECK(last == UINT32_MAX && link_cli_book_full(&g_book));
    bool run = true;
    uint32_t t = 99u;
    CHECK(link_cli_idle(&g_book, LINK_CLI_BEGIN, NULL, 0u, g_rsp, &run, &t) == LINK_CLI_RESULT_BYTES);
    CHECK(!run && g_rsp[0] == LINK_E_SESSION);                              // no number 0, no 1
    CHECK(link_cli_issue(&g_book) == 0u && g_book.issued == UINT32_MAX);
    r = send_cli(0x5E55u, last, "id", 0u);                                 // the last still runs
    CHECK(g_runs == 2u && r.status == 0u);
    r = send_cli(0x5E55u, a, "stat", 0u);                                  // and a's record holds
    CHECK(g_runs == 2u && (r.flags & LINK_CLI_F_REPEAT));
    r = send_cli(0x5E55u, 1u, "id", 0u);                                   // 1 was never reissued
    CHECK(g_runs == 2u && r.status == LINK_E_STALE);
    return true;
}

// A ticket from another session (the board has restarted: a new id), ticket 0,
// or one never issued, runs nothing.
static bool cli_session(void) {
    fresh_board(0xAAAA0001u);
    const uint32_t t = begin_ticket();
    res_t r = send_cli(0xAAAA0002u, t, "stat", 0u);
    CHECK(g_runs == 0u && r.status == LINK_E_SESSION);
    r = send_cli(0xAAAA0001u, 0u, "stat", 0u);
    CHECK(g_runs == 0u && r.status == LINK_E_SESSION);
    r = send_cli(0xAAAA0001u, t + 1u, "stat", 0u);
    CHECK(g_runs == 0u && r.status == LINK_E_SESSION);
    r = send_cli(0xAAAA0001u, t, "stat", 0u);
    CHECK(g_runs == 1u && r.status == 0u);
    // The board restarts: a new book, a new id. The old ticket is refused, not
    // run (it may have run before the restart: the Pi's tool says so).
    fresh_board(0xBBBB0001u);
    CHECK(begin_ticket() == 1u);                                            // the same number...
    r = send_cli(0xAAAA0001u, t, "stat", 0u);                              // ...another session
    CHECK(g_runs == 0u && r.status == LINK_E_SESSION);
    r = send_abort(0xAAAA0001u, t);
    CHECK(r.status == LINK_E_SESSION);
    // Malformed: too short for a ticket and a line, or not a valid line.
    uint8_t p[16];
    bool run = true;
    uint32_t tk = 0u;
    size_t n = req_id(p, 0xBBBB0001u, 1u, NULL);
    CHECK(link_cli_idle(&g_book, LINK_CLI, p, n, g_rsp, &run, &tk) == LINK_CLI_RESULT_BYTES);
    CHECK(!run && g_rsp[0] == LINK_E_LENGTH);
    n = req_id(p, 0xBBBB0001u, 1u, "id\n");
    CHECK(link_cli_idle(&g_book, LINK_CLI, p, n, g_rsp, &run, &tk) == LINK_CLI_RESULT_BYTES);
    CHECK(!run && g_rsp[0] == LINK_E_LENGTH && rd32(&g_rsp[2]) == 0xBBBB0001u && rd32(&g_rsp[6]) == 1u);
    r = send_cli(0xBBBB0001u, 1u, "id", 0u);                               // still issued: runs
    CHECK(g_runs == 1u && r.status == 0u);
    CHECK(link_cli_idle(&g_book, LINK_CLI_BEGIN, p, 1u, g_rsp, &run, &tk) == LINK_CLI_RESULT_BYTES);
    CHECK(!run && g_rsp[0] == LINK_E_LENGTH);
    CHECK(link_cli_idle(&g_book, LINK_PING, NULL, 0u, g_rsp, &run, &tk) == 0u && !run);
    return true;
}

// V113-F2: a CLI_ABORT for a ticket not yet run cancels it -- it never runs,
// whatever arrives after. For one that ran, the answer is its record; the
// command is not "cancelled".
static bool cli_cancel(void) {
    fresh_board(5u);
    const uint32_t t = begin_ticket();
    res_t r = send_abort(5u, t);
    CHECK(r.status == 0u && r.flags == LINK_CLI_F_CANCELLED && r.ticket == t);
    r = send_cli(5u, t, "strobe pulse 20", 0u);                            // the request, late
    CHECK(g_runs == 0u);
    CHECK(r.status == 0u && r.flags == (LINK_CLI_F_CANCELLED | LINK_CLI_F_REPEAT));
    r = send_abort(5u, t);
    CHECK(r.flags == (LINK_CLI_F_CANCELLED | LINK_CLI_F_REPEAT));
    CHECK(r.out_bytes == 0u && r.out_crc == 0u);

    const uint32_t u = begin_ticket();
    (void)send_cli(5u, u, "panel demo", LINK_CLI_F_STOP_ASKED | LINK_CLI_F_STOPPED);
    r = send_abort(5u, u);                                                   // done already
    CHECK(g_runs == 1u && r.status == 0u);
    CHECK(r.flags == (LINK_CLI_F_STOP_ASKED | LINK_CLI_F_STOPPED | LINK_CLI_F_REPEAT));
    CHECK(!(r.flags & LINK_CLI_F_CANCELLED) && r.out_bytes == 7u);

    uint8_t p[16];
    bool run = true;
    uint32_t tk = 0u;
    size_t n = req_id(p, 5u, u, NULL);
    CHECK(link_cli_idle(&g_book, LINK_CLI_ABORT, p, n - 1u, g_rsp, &run, &tk) == LINK_CLI_RESULT_BYTES);
    CHECK(!run && g_rsp[0] == LINK_E_LENGTH);
    CHECK(link_cli_idle(&g_book, LINK_CLI_ABORT, NULL, 0u, g_rsp, &run, &tk) == LINK_CLI_RESULT_BYTES);
    CHECK(g_rsp[0] == LINK_E_LENGTH);                                      // 1.13's form
    return true;
}

// V115-F4: every answer names the session as well as the ticket -- the request's,
// whatever the outcome -- so an answer about ticket 1 of one session cannot be
// taken for one about ticket 1 of another. The review's case: `stat` ran as
// (A, 1); the board restarted as B; (B, 1) was cancelled; that "cancelled" came
// late, ahead of the SESSION refusal of a query about (A, 1), and the Pi took it.
static bool cli_answer_names_session(void) {
    const uint32_t A = 0x5EED0001u, B = 0x5EED0002u;
    fresh_board(A);
    const uint32_t a1 = begin_ticket();
    res_t r = send_cli(A, a1, "stat", 0u);                                 // ran
    CHECK(r.status == 0u && r.session == A && r.ticket == a1);
    r = send_cli(A, a1, "stat", 0u);                                       // REPEAT
    CHECK((r.flags & LINK_CLI_F_REPEAT) && r.session == A && r.ticket == a1);
    r = send_cli(A, a1, "id", 0u);                                         // MISMATCH
    CHECK(r.status == LINK_E_MISMATCH && r.session == A && r.ticket == a1);
    r = send_abort(A, a1);                                                 // its record
    CHECK(r.status == 0u && r.session == A && r.ticket == a1);

    fresh_board(B);                                                        // the restart
    const uint32_t b1 = begin_ticket();
    CHECK(b1 == a1);                                                       // the same number
    r = send_abort(B, b1);                                                 // (B, 1) cancelled
    CHECK(r.flags == LINK_CLI_F_CANCELLED && r.session == B && r.ticket == 1u);
    r = send_abort(A, a1);                                                 // the query about (A, 1)
    CHECK(r.status == LINK_E_SESSION && r.session == A && r.ticket == 1u);
    r = send_cli(A, a1, "stat", 0u);
    CHECK(g_runs == 0u && r.status == LINK_E_SESSION && r.session == A && r.ticket == 1u);
    r = send_abort(B, b1);                                                 // REPEAT of the cancel
    CHECK(r.flags == (LINK_CLI_F_CANCELLED | LINK_CLI_F_REPEAT) && r.session == B);

    // STALE names the pair asked about, too.
    const uint32_t b2 = begin_ticket();
    for (unsigned k = 0; k < LINK_CLI_BOOK; k++) (void)begin_ticket();
    r = send_abort(B, b2);
    CHECK(r.status == LINK_E_STALE && r.session == B && r.ticket == b2);
    return true;
}

// Split a test string into argv, then apply the rule in the given state.
static link_cli_rule_t rule(const char *line, bool orderly, bool cuts, int *argc_out, char argv_out[8][16]) {
    static char copy[96];
    char *argv[8];
    int argc = 0;
    snprintf(copy, sizeof(copy), "%s", line);
    // Split on single spaces, as the test lines are written (cli.c splits the real ones).
    for (char *p = copy; *p && argc < 8;) {
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    const link_cli_power_t now = { orderly, cuts };
    link_cli_rule_t r = link_cli_power_rule(&argc, argv, &now);
    *argc_out = argc;
    for (int i = 0; i < argc; i++) snprintf(argv_out[i], 16, "%s", argv[i]);
    return r;
}

static bool cli_power_rule(void) {
    enum { RUN = 1, FLT = 2 };   // RUNNING (off orderly); FAULT with the rail up
    static const struct { const char *line; int state; link_cli_rule_t r; int argc; } v[] = {
        { "forceoff",                RUN, LINK_CLI_NEEDS_CONFIRM, 1 },
        { "forceoff confirm",        RUN, LINK_CLI_ALLOW,         1 },   // 'confirm' removed
        { "forceoff now",            RUN, LINK_CLI_NEEDS_CONFIRM, 2 },
        { "forceoff confirm now",    RUN, LINK_CLI_NEEDS_CONFIRM, 3 },
        { "reset",                   RUN, LINK_CLI_ALLOW,         1 },   // to the CLI's own Pi guard
        { "reset confirm",           RUN, LINK_CLI_ALLOW,         2 },   // ...as typed
        { "reset force",             RUN, LINK_CLI_NEEDS_CONFIRM, 2 },
        { "reset force confirm",     RUN, LINK_CLI_ALLOW,         2 },   // -> 'reset force'
        { "reset force confirm now", RUN, LINK_CLI_NEEDS_CONFIRM, 4 },
        { "bootsel",                 RUN, LINK_CLI_ALLOW,         1 },
        { "bootsel force",           RUN, LINK_CLI_NEEDS_CONFIRM, 2 },
        { "bootsel force confirm",   RUN, LINK_CLI_ALLOW,         2 },
        { "adc5vcal",                RUN, LINK_CLI_ALLOW,         1 },   // its usage only
        { "adc5vcal 5.20",           RUN, LINK_CLI_NEEDS_CONFIRM, 2 },
        { "adc5vcal 5.20 confirm",   RUN, LINK_CLI_ALLOW,         2 },   // -> 'adc5vcal 5.20'
        { "adc5vcal confirm",        RUN, LINK_CLI_NEEDS_CONFIRM, 2 },   // not the confirmed form
        { "adc5vcal 5.2 confirm x",  RUN, LINK_CLI_NEEDS_CONFIRM, 4 },
        { "off",                     RUN, LINK_CLI_ALLOW,         1 },   // RUNNING: orderly
        { "off",                     0,   LINK_CLI_NOT_ORDERLY,   1 },   // BENCH_RUNNING, PI_BOOTING ...
        { "off",                     FLT, LINK_CLI_NOT_ORDERLY,   1 },   // FAULT: a hard cut
        { "off confirm",             FLT, LINK_CLI_NOT_ORDERLY,   2 },   // no confirmed form of `off`
        { "fault",                   FLT, LINK_CLI_ALLOW,         1 },   // shows the fault
        { "fault clear",             FLT, LINK_CLI_NEEDS_CONFIRM, 2 },   // would drop the rail
        { "fault clear now",         FLT, LINK_CLI_NEEDS_CONFIRM, 3 },
        { "fault clear confirm",     FLT, LINK_CLI_ALLOW,         2 },   // -> 'fault clear'
        { "fault clear",             RUN, LINK_CLI_ALLOW,         2 },   // the code only
        { "fault clear confirm",     RUN, LINK_CLI_ALLOW,         2 },   // 'confirm' just removed
        { "strobe live on confirm",  RUN, LINK_CLI_ALLOW,         4 },   // other commands untouched
        { "Forceoff",                RUN, LINK_CLI_ALLOW,         1 },   // not a command: the CLI says '?'
    };
    char out[8][16];
    for (size_t k = 0; k < sizeof(v) / sizeof(v[0]); k++) {
        int argc = 0;
        CHECK(rule(v[k].line, v[k].state == RUN, v[k].state == FLT, &argc, out) == v[k].r);
        CHECK(argc == v[k].argc);
    }
    int argc = 0;
    CHECK(rule("reset force confirm", true, false, &argc, out) == LINK_CLI_ALLOW);
    CHECK(!strcmp(out[0], "reset") && !strcmp(out[1], "force"));
    CHECK(rule("adc5vcal 5.203 confirm", true, false, &argc, out) == LINK_CLI_ALLOW);
    CHECK(argc == 2 && !strcmp(out[1], "5.203"));
    CHECK(rule("strobe live on confirm", true, false, &argc, out) == LINK_CLI_ALLOW && !strcmp(out[3], "confirm"));
    argc = 0;
    const link_cli_power_t now = { true, false };
    CHECK(link_cli_power_rule(&argc, NULL, &now) == LINK_CLI_ALLOW);       // nothing to judge
    return true;
}

typedef struct { uint8_t got[1024]; size_t n; unsigned calls; size_t sizes[16]; uint32_t offs[16]; } sink_t;

static void sink(uint32_t offset, const uint8_t *p, size_t n, void *ctx) {
    sink_t *s = ctx;
    memcpy(&s->got[s->n], p, n);
    s->n += n;
    if (s->calls < 16u) { s->sizes[s->calls] = n; s->offs[s->calls] = offset; }
    s->calls++;
}

// V113-F5: every chunk carries its offset in the whole, and the total and the
// CRC of everything sent are kept for the answer.
static bool cli_out_offsets(void) {
    static sink_t s;
    static char text[600];
    link_cli_out_t o;
    for (size_t i = 0; i < sizeof(text); i++) text[i] = (char)('a' + i % 26u);
    memset(&s, 0, sizeof(s));
    link_cli_out_reset(&o);
    CHECK(o.sent == 0u && o.crc == LINK_CRC_INIT);
    link_cli_out_put(&o, text, 100u, sink, &s);
    CHECK(s.calls == 0u);                                                  // held
    link_cli_out_put(&o, text + 100, 500u, sink, &s);
    CHECK(s.calls == 2u && s.sizes[0] == LINK_CLI_OUT_CHUNK && s.sizes[1] == LINK_CLI_OUT_CHUNK);
    CHECK(s.offs[0] == 0u && s.offs[1] == LINK_CLI_OUT_CHUNK);
    link_cli_out_flush(&o, false, sink, &s);
    CHECK(s.calls == 3u && s.sizes[2] == 600u - 2u * LINK_CLI_OUT_CHUNK);
    CHECK(s.offs[2] == 2u * LINK_CLI_OUT_CHUNK);
    CHECK(s.n == 600u && memcmp(s.got, text, 600u) == 0);                  // in order, whole
    CHECK(o.sent == 600u && o.crc == link_crc16((const uint8_t *)text, 600u, LINK_CRC_INIT));
    link_cli_out_flush(&o, false, sink, &s);
    CHECK(s.calls == 3u);                                                  // nothing held: no frame
    link_cli_out_flush(&o, true, sink, &s);
    CHECK(s.calls == 4u && s.sizes[3] == 0u && s.offs[3] == 600u);         // the keepalive
    CHECK(o.sent == 600u);
    link_cli_out_reset(&o);
    CHECK(o.sent == 0u && o.crc == LINK_CRC_INIT && o.n == 0u);
    return true;
}
_Static_assert(LINK_CLI_OUT_HDR + LINK_CLI_OUT_CHUNK <= LINK_MAX_PAYLOAD, "a CLI_OUT must fit one frame");
_Static_assert(LINK_CLI_ID_BYTES + LINK_CLI_LINE_MAX <= LINK_MAX_PAYLOAD, "a CLI request must fit one frame");
_Static_assert(LINK_CLI_RESULT_BYTES <= LINK_MAX_PAYLOAD && LINK_CLI_BEGIN_BYTES <= LINK_CLI_RESULT_BYTES,
               "the answers fit");

// An abort frame, encoded.
static size_t abort_frame(uint8_t *out, size_t cap, uint8_t type, uint8_t seq, uint32_t session,
                          uint32_t ticket, size_t len) {
    uint8_t p[16];
    req_id(p, session, ticket, NULL);
    return link_frame_encode(out, cap, type, seq, p, len);
}

// V113-F2: while a command runs, only ITS abort -- this session, this ticket,
// 8 bytes -- stops it. A late abort for an earlier ticket, a malformed one, a
// response and every other request are dropped.
static bool cli_scan_ticket(void) {
    uint8_t buf[512];
    size_t n = 0;
    n += link_frame_encode(buf + n, sizeof(buf) - n, LINK_PING, 1u, NULL, 0u);
    n += abort_frame(buf + n, sizeof(buf) - n, LINK_CLI_ABORT, 2u, 9u, 41u, 8u);    // earlier ticket
    n += abort_frame(buf + n, sizeof(buf) - n, LINK_CLI_ABORT, 3u, 8u, 42u, 8u);    // other session
    n += abort_frame(buf + n, sizeof(buf) - n, LINK_CLI_ABORT, 4u, 9u, 42u, 7u);    // short
    n += link_frame_encode(buf + n, sizeof(buf) - n, LINK_CLI_ABORT, 5u, NULL, 0u); // 1.13's
    n += abort_frame(buf + n, sizeof(buf) - n, (uint8_t)(LINK_CLI_ABORT | LINK_RESPONSE), 6u, 9u, 42u, 8u);
    buf[n++] = 0x00;                                                        // junk
    const size_t before = n;
    n += abort_frame(buf + n, sizeof(buf) - n, LINK_CLI_ABORT, 7u, 9u, 42u, 8u);    // this one

    link_dec_t d;
    uint32_t dropped = 0u;
    link_dec_init(&d);
    CHECK(!link_cli_scan(&d, buf, before, 0u, 9u, 42u, &dropped));
    CHECK(dropped == 6u);
    CHECK(link_cli_scan(&d, buf + before, n - before, 0u, 9u, 42u, &dropped));
    CHECK(dropped == 6u);

    // A byte at a time, as cli_poll() feeds it: the same.
    link_dec_init(&d);
    dropped = 0u;
    bool seen = false;
    for (size_t i = 0; i < n; i++) seen = link_cli_scan(&d, &buf[i], 1u, 0u, 9u, 42u, &dropped) || seen;
    CHECK(seen && dropped == 6u);

    // A corrupted abort is not one.
    link_dec_init(&d);
    buf[n - 3u] ^= 0x01u;
    CHECK(!link_cli_scan(&d, buf + before, n - before, 0u, 9u, 42u, NULL));
    CHECK(d.crc_errors == 1u);
    return true;
}

// V113-F2, the queued case: a request and then its cancel, waiting together in
// the receive buffer while a USB command held the board. Looked for in what
// follows the request, through a decoder of its own; across a wrap in two
// pieces; and only this ticket's.
static bool cli_abort_ahead(void) {
    uint8_t buf[512];
    size_t n = 0;
    n += link_frame_encode(buf + n, sizeof(buf) - n, LINK_PING, 1u, NULL, 0u);
    n += abort_frame(buf + n, sizeof(buf) - n, LINK_CLI_ABORT, 2u, 9u, 41u, 8u);
    const size_t mid = n;
    n += abort_frame(buf + n, sizeof(buf) - n, LINK_CLI_ABORT, 3u, 9u, 42u, 8u);

    link_dec_t s;
    link_dec_init(&s);
    CHECK(link_cli_abort_ahead(&s, buf, n, 0u, 9u, 42u));
    link_dec_init(&s);
    CHECK(!link_cli_abort_ahead(&s, buf, mid, 0u, 9u, 42u));               // only the others
    link_dec_init(&s);
    CHECK(!link_cli_abort_ahead(&s, buf, n, 0u, 9u, 43u));                 // another ticket's
    link_dec_init(&s);
    CHECK(!link_cli_abort_ahead(&s, buf, n, 0u, 8u, 42u));                 // another session's
    // Split inside the abort frame, as a ring wrap splits it: one decoder, two calls.
    link_dec_init(&s);
    CHECK(!link_cli_abort_ahead(&s, buf, mid + 5u, 0u, 9u, 42u));
    CHECK(link_cli_abort_ahead(&s, buf + mid + 5u, n - mid - 5u, 0u, 9u, 42u));
    CHECK(!link_cli_abort_ahead(NULL, buf, n, 0u, 9u, 42u));
    return true;
}

int main(int argc, char **argv) {
    static const test_case tests[] = {
        TEST(crc_check_value), TEST(encode_vectors), TEST(encode_limits), TEST(decode_round_trip),
        TEST(every_bit_flip_rejected), TEST(resync_after_junk), TEST(corrupt_then_next),
        TEST(gap_drops_partial), TEST(reset_keeps_counters), TEST(rx_settle),
        TEST(sha_vectors), TEST(sha_million_a_chunked), TEST(sha_padding_boundaries),
        TEST(cli_valid_line), TEST(cli_ticket_once), TEST(cli_two_clients), TEST(cli_wrap_and_age),
        TEST(cli_session), TEST(cli_book_never_wraps), TEST(cli_cancel), TEST(cli_power_rule),
        TEST(cli_answer_names_session),
        TEST(cli_out_offsets),
        TEST(cli_scan_ticket), TEST(cli_abort_ahead),
    };
    if (argc != 2) {
        fprintf(stderr, "usage: %s <test>\n", argv[0]);
        return 2;
    }
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (strcmp(argv[1], tests[i].name) == 0) {
            test_name = tests[i].name;
            return tests[i].fn() ? 0 : 1;
        }
    }
    fprintf(stderr, "unknown test: %s\n", argv[1]);
    return 2;
}
