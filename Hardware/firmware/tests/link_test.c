// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
//
// Host tests for the Pi link's framing (link_frame.c) and the SHA-256 the update
// checks images with (sha256.c). No mocks: both are pure. The frame bytes below
// are also in pi/tests/test_pitrac_link.py, so the two ends are tested against
// the same vectors.
#include "link_frame.h"
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

int main(int argc, char **argv) {
    static const test_case tests[] = {
        TEST(crc_check_value), TEST(encode_vectors), TEST(encode_limits), TEST(decode_round_trip),
        TEST(every_bit_flip_rejected), TEST(resync_after_junk), TEST(corrupt_then_next),
        TEST(gap_drops_partial), TEST(reset_keeps_counters), TEST(rx_settle),
        TEST(sha_vectors), TEST(sha_million_a_chunked), TEST(sha_padding_boundaries),
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
