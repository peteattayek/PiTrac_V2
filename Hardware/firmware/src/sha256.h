// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// sha256.h -- SHA-256 (FIPS 180-4) in software, for checking a firmware image
// received from the Pi before it goes anywhere near flash.
//
// Software rather than the RP2350's SHA-256 block: that block needs a DMA
// channel or the CPU feeding it word by word, and the boot ROM takes a lock on
// it for its own partition-table calls. This runs in a few tens of ms over a
// whole image, a chunk at a time as the image arrives. No SDK calls: compiled
// into the host tests against the FIPS 180-4 example vectors.

#ifndef PITRAC_SHA256_H
#define PITRAC_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_BYTES 32u

typedef struct {
    uint32_t h[8];
    uint64_t total;        // bytes hashed so far
    uint8_t  block[64];
    uint32_t used;         // bytes waiting in block[]
} sha256_t;

void sha256_init(sha256_t *s);
void sha256_update(sha256_t *s, const void *data, size_t n);
void sha256_final(sha256_t *s, uint8_t out[SHA256_BYTES]);

#endif // PITRAC_SHA256_H
