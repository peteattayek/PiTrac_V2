// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
//
// rom_scan.c -- see rom_scan.h. A test oracle only; never part of the firmware.
// fw_update_test.c includes this file at its end rather than linking it, so a
// probe that includes the test file builds with no extra source.
//
// s_varm_crit_next_block() below is reproduced UNCHANGED from pico-bootrom-rp2350,
// src/main/arm/varm_blocks.c (master, fetched 2026-10-20), and keeps that file's
// BSD-3-Clause licence and copyright. The same body ran in the reviewer's
// harness (captures/review_2026-10-19/sol_rom_scan_r4_probe.c). Everything else
// in this file -- the host definitions it needs, and rom_scan_partition() -- is
// PiTrac's, after that harness. The A4 boot ROM is assumed to scan as this source
// does; that is not shown here.

#include "rom_scan.h"

#include <assert.h>
#include <string.h>

// ---- host definitions the scanner body needs ----------------------------------
typedef unsigned uint;
typedef uint8_t aligned4_uint8_t;
#define PICOBIN_MAX_BLOCK_SIZE 1024u
#define PICOBIN_BLOCK_MARKER_START 0xffffded3u
#define PICOBIN_BLOCK_MARKER_END 0xab123579u
#define PICOBIN_BLOCK_ITEM_2BS_LAST 0xffu
#define static_assert _Static_assert
#define next_block_printf(...) ((void)0)
#define unsupported_block_error(...) return -1       // as the ROM's own definition
#define bootrom_assert(category, cond) assert(cond)
#define __get_opaque_value(x) (x)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define to_block_word_index_t(v) (assert((v) < 256u), (v))
typedef uint32_t block_word_index_t;
typedef struct {
    struct { uint32_t block_buffer[PICOBIN_MAX_BLOCK_SIZE / 4u]; } block_buffer_or_signature_workspace;
} workspace_t;
typedef struct {
    struct { uint32_t base, size; } current_search_window;
    workspace_t *scan_workarea;
} context_t;
typedef struct {
    context_t *ctx;
    uint32_t buf_word_pos, buf_word_count, window_rel_buf_start_word_offset;
    uint32_t window_rel_next_read_word_offset, window_rel_first_block_max_word_offset;
    int32_t window_rel_first_block_word_offset;
} block_scan_t;

static uint32_t *s_window;      // the partition, as words; the window base is 0

static uint32_t *inline_s_resolve_ram_or_absolute_flash_addr(uint32_t addr) {
    return &s_window[addr / 4u];
}
static void call_s_varm_crit_mem_copy_by_words(uint32_t *d, const uint32_t *s, uint32_t n) {
    memmove(d, s, n);
}
static uint32_t call_decode_item_size(uint32_t h) {
    return (h >> 8u) & ((h & 0x80u) ? 0xffffu : 0xffu);
}
static bool first_block_found(const block_scan_t *bs) {
    return bs->window_rel_first_block_word_offset >= 0;
}

// ---- the boot ROM's scanner, unchanged ------------------------------------------
int s_varm_crit_next_block(block_scan_t *bs) {
    uint32_t *buffer = (uint32_t *)bs->ctx->scan_workarea->block_buffer_or_signature_workspace.block_buffer;
    static_assert(sizeof(bs->ctx->scan_workarea->block_buffer_or_signature_workspace.block_buffer) == PICOBIN_MAX_BLOCK_SIZE, "");

    // note: there is absolutely no need to copy data into a buffer, except when using flash, however
    // we do so to keep the code paths largely identical, and thus save code size

    do {
        next_block_printf("FIND BLOCK LOOP buf_word_pos=%08x, buf_word_count=%08x\n", (uint)bs->buf_word_pos, (uint)bs->buf_word_count);
        // refill buffer as much as we can
        if (bs->buf_word_pos == bs->buf_word_count) {
            bs->buf_word_pos = bs->buf_word_count = 0;
            bs->window_rel_buf_start_word_offset = bs->window_rel_next_read_word_offset;
        }
        if (bs->window_rel_buf_start_word_offset + bs->buf_word_pos == (uint32_t)bs->window_rel_first_block_word_offset) {
            next_block_printf("  looped back to first block at %08x\n", debug_real_addr(bs, 0));
            return 0;
        }
        uint32_t max_read_word_count = bs->ctx->current_search_window.size/4 - bs ->window_rel_next_read_word_offset;
//        printf("maxrwc 0x%08x bwp %d bwc %d\n", max_read_word_count, bs->buf_word_pos, bs->buf_word_count);
        if (max_read_word_count && max_read_word_count <= bs->ctx->current_search_window.size/4) {
            int32_t to_read_word_count = (PICOBIN_MAX_BLOCK_SIZE / 4 - bs->buf_word_count);
            to_read_word_count = MIN(to_read_word_count, (int)max_read_word_count);
            to_read_word_count = MIN(to_read_word_count, 0x200 / 4); // limit read to 0x200 so we don't overshoot 0x1000 (performance)
            if (to_read_word_count > 0) {
                next_block_printf("READ %08x(%08x) + %08x -> %p - %p\n",
                                  (uint) bs->ctx->current_search_window.base + bs->window_rel_next_read_word_offset*4,
                                  inline_s_resolve_ram_or_absolute_flash_addr(bs->ctx->current_search_window.base) + bs->window_rel_next_read_word_offset,
                                  (uint) to_read_word_count * 4,
                                  buffer + bs->buf_word_count,
                                  buffer + bs->buf_word_count + to_read_word_count);
                // note: this was an inlines flash_read_data; however we now support reading from RAM too, and flash-read_data is just assert + memcpy
                // hx_assert_equal2i(addr & -(1u << 24), 0);
                call_s_varm_crit_mem_copy_by_words(buffer + bs->buf_word_count,
                                                   inline_s_resolve_ram_or_absolute_flash_addr(
                                                           bs->ctx->current_search_window.base) + bs->window_rel_next_read_word_offset, (uint32_t)to_read_word_count * 4);
                bs->window_rel_next_read_word_offset += (uint32_t)to_read_word_count;
                bs->buf_word_count += (block_word_index_t)to_read_word_count;
            }
        } else if (!bs->buf_word_count) {
            // no more, but leaving around just in case
            // pico_default_asm_volatile("nop"); // for whatever reason, the code is smaller with this NOP ;-)
            unsupported_block_error(128, "attempt to read outside of window at %08x(%08x)\n",
                                    (uint) bs->ctx->current_search_window.base +
                                    bs->window_rel_next_read_word_offset * 4,
                                    inline_s_resolve_ram_or_absolute_flash_addr(bs->ctx->current_search_window.base) +
                                    bs->window_rel_next_read_word_offset);
        }
        uint32_t block_marker_start = __get_opaque_value(PICOBIN_BLOCK_MARKER_START);
        if (!first_block_found(bs)) {
            next_block_printf("searching for first block\n");
            // we must look for the first block
            while (bs->buf_word_pos < bs->buf_word_count) {
                uint32_t value = buffer[bs->buf_word_pos];
                if (value == block_marker_start) {
                    next_block_printf("possible first block start at buffer offset %08x = %08x\n", (uint) bs->buf_word_pos*4, debug_real_addr(bs, bs->buf_word_pos));
                    break;
                }
                bs->buf_word_pos++;
            }
            if (bs->buf_word_pos >= bs->buf_word_count) {
                if (bs->window_rel_next_read_word_offset >= bs->window_rel_first_block_max_word_offset) {
                    next_block_printf("first block scan exhausted with no marker found\n");
                    return 0;
                }
                // we will keep reading
                next_block_printf("no block start found in current buffer, need to read more\n");
                continue;
            }
        } else {
            uint32_t value = buffer[bs->buf_word_pos];
            if (value != block_marker_start) {
                unsupported_block_error(1, "no block marker at next expected pos %p\n", debug_real_addr(bs, bs->buf_word_pos));
                // if we've found the first block, then we expect to immediately find a block where we're looking
            }
        }
        next_block_printf("block is at buffer offset %08x = %08x\n", (uint) bs->buf_word_pos*4, debug_real_addr(bs, bs->buf_word_pos));
        // so at this point there is a block start marker at buf_pos
        if (bs->buf_word_pos) {
            next_block_printf("copying %d words from %08x to start of buffer\n", bs->buf_word_count - bs->buf_word_pos, buffer + bs->buf_word_pos);
            // buf_pos is not at start of buffer, so we will move everything down
            bootrom_assert(BLOCK_SCAN, bs->buf_word_pos != bs->buf_word_count);
            call_s_varm_crit_mem_copy_by_words(buffer, buffer + bs->buf_word_pos, (bs->buf_word_count - bs->buf_word_pos)*4u);
            bs->buf_word_count -= bs->buf_word_pos;
            bs->window_rel_buf_start_word_offset += bs->buf_word_pos;
            bs->buf_word_pos = 0;
            // we continue to complete filling of buffer
            continue;
        }
        // we now have a full buffer with PICOBIN_BLOCK_MARKER_START at the beginning.
        // note that the entire block is in our buffer
//        uint32_t size = 0;
        uint32_t item_pos = 1;
        uint32_t end_marker_buf_pos = 0;
        while (item_pos < bs->buf_word_count) {
            const aligned4_uint8_t *ptr = (const aligned4_uint8_t *)(buffer + item_pos);
            next_block_printf("ITEM AT %08x (off %d) %08x\n", (int)item_pos*4, (int)item_pos, ((const uint32_t*)ptr)[0]);
            uint8_t type = ptr[0];
            uint32_t item_size = call_decode_item_size(*(const uint32_t *)ptr);
            if (!item_size) {
                unsupported_block_error(2, "invalid zero sized item");
            }
            if (type == PICOBIN_BLOCK_ITEM_2BS_LAST) {
                // block size in words is always item_pos - 1 since we start at 1
                if (item_pos - 1 == item_size) {
                    if (item_pos < bs->buf_word_count && buffer[item_pos + 2] == PICOBIN_BLOCK_MARKER_END) {
                        end_marker_buf_pos = item_pos;
                    } else {
                        unsupported_block_error(4, "no end marker in range");
                    }
                } else {
                    unsupported_block_error(8, "size mismatch %d != %d\n", (uint)(item_pos - 1), (uint)item_size);
                }
                break;
            }
//            size += item_size;
            item_pos += item_size;
        }
        if (end_marker_buf_pos) {
            // this is a valid block
            next_block_printf("WOOT BLOCK IS GOOD!!\n");
            uint32_t next_block_buf_pos = (uint32_t)(((int32_t)buffer[end_marker_buf_pos + 1])/4);
            if (!first_block_found(bs)) {
                bs->window_rel_first_block_word_offset = (int32_t)bs->window_rel_buf_start_word_offset;
            } else if (!next_block_buf_pos) {
                unsupported_block_error(16, "non-first block loops to itself");
            }

            // offset is in bytes from start of our buffer (which happens to be at buf pos 0 (the block is at the start of the buffer)
            next_block_printf("next block buf_offset %08x = %08x\n", (uint)next_block_buf_pos, debug_real_addr(bs, next_block_buf_pos));
            if (next_block_buf_pos < bs->buf_word_count) {
                next_block_printf("  ... which is in the buffer\n");
                // if it is in the current buffer, then it can't be a link to before the first block
                bs->buf_word_pos = to_block_word_index_t(next_block_buf_pos);
            } else {
                next_block_printf("  ... which is not in the buffer, so setting up for new read\n");
                bs->window_rel_next_read_word_offset += (next_block_buf_pos - bs->buf_word_count);
                if (bs->window_rel_next_read_word_offset < (uint32_t)bs->window_rel_first_block_word_offset) {
                    unsupported_block_error(32, "the block link goes to lower address than first block");
                }
                bs->buf_word_pos = bs->buf_word_count; // need whole new buffer
            }
            // size of block (well the bit that matters)
            return (int)end_marker_buf_pos;
        }
        if (first_block_found(bs)) {
            unsupported_block_error(64, "(non first) block was not valid, done");
        }
        bs->buf_word_pos++;
        next_block_printf("block was not valid, continue searching from buffer offset %08x = %08x\n", (uint) bs->buf_word_pos * 4, debug_real_addr(bs, bs->buf_word_pos));
    } while (true);
}

// ---- PiTrac's walk over it ---------------------------------------------------------

#define ITEM_IMAGE_TYPE   0x42u
#define ITEM_VERSION      0x48u
#define FIRST_BLOCK_SEARCH 4096u            // varm_flash_boot.c: BLOCK_LIST_SEARCH_MAX
#define MAX_HOPS          32

rom_view_t rom_scan_partition(const uint8_t *part, uint32_t size) {
    static workspace_t work;
    rom_view_t v;
    memset(&v, 0, sizeof(v));
    memset(&work, 0, sizeof(work));
    context_t ctx = { { 0u, size }, &work };
    block_scan_t bs;
    memset(&bs, 0, sizeof(bs));
    bs.ctx = &ctx;
    // s_varm_crit_init_block_scan(bs, ctx, 0, BLOCK_LIST_SEARCH_MAX)
    bs.window_rel_next_read_word_offset = 0u;
    bs.window_rel_first_block_word_offset = -1;
    bs.window_rel_first_block_max_word_offset = MIN(size / 4u, FIRST_BLOCK_SEARCH / 4u);
    s_window = (uint32_t *)(uintptr_t)part;

    for (int hop = 0; hop < MAX_HOPS; hop++) {
        const int last = s_varm_crit_next_block(&bs);
        if (last <= 0) {
            v.loop_closed = last == 0 && v.found;
            break;
        }
        if (!v.found) {
            v.found = true;
            v.first = (uint32_t)bs.window_rel_first_block_word_offset * 4u;
        }
        v.blocks++;
        // The block is at the start of the buffer; its items run from word 1 to
        // LAST at word `last`.
        const uint32_t *b = work.block_buffer_or_signature_workspace.block_buffer;
        bool has_type = false;
        uint16_t type = 0u;
        uint32_t version = 0u;
        for (uint32_t w = 1u; w < (uint32_t)last; ) {
            const uint32_t sz = call_decode_item_size(b[w]);
            if ((b[w] & 0xffu) == ITEM_IMAGE_TYPE && sz == 1u) {
                has_type = true;
                type = (uint16_t)(b[w] >> 16);
            } else if ((b[w] & 0xffu) == ITEM_VERSION && sz >= 2u) {
                version = b[w + 1u];        // minor | major << 16
            }
            w += sz ? sz : 1u;
        }
        if (has_type && (type & 0x773fu) == 0x1021u) {
            if (type & 0x8000u) {
                v.trial_only = true;
            } else {
                v.launchable = true;
                if (version > v.version) v.version = version;
            }
        }
    }
    return v;
}

// Nothing of the scanner's host scaffolding outlives this file.
#undef PICOBIN_MAX_BLOCK_SIZE
#undef PICOBIN_BLOCK_MARKER_START
#undef PICOBIN_BLOCK_MARKER_END
#undef PICOBIN_BLOCK_ITEM_2BS_LAST
#undef next_block_printf
#undef unsupported_block_error
#undef bootrom_assert
#undef __get_opaque_value
#undef MIN
#undef to_block_word_index_t
#undef ITEM_IMAGE_TYPE
#undef ITEM_VERSION
#undef FIRST_BLOCK_SEARCH
#undef MAX_HOPS
