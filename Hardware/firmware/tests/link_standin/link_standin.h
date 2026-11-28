// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// link_standin.h -- just enough of the Pico SDK to compile the UNCHANGED
// src/pi_link.c on a PC (tests/pi_link_test.c, image 1.15, review finding
// V114-F2). The test defines every function: a clock that moves on, a UART that
// records what is sent, and a DMA ring whose write index the test advances,
// with each byte landing in RAM a little AFTER the index passed it -- the lag
// link_frame.h's LINK_RX_SETTLE_US exists for.

#ifndef PITRAC_LINK_STANDIN_H
#define PITRAC_LINK_STANDIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned int uint;
typedef uint64_t absolute_time_t;

// --- time ---------------------------------------------------------------------
uint64_t        time_us_64(void);
absolute_time_t get_absolute_time(void);
uint32_t        to_ms_since_boot(absolute_time_t t);
static inline void tight_loop_contents(void) {}

// --- stdio --------------------------------------------------------------------
typedef struct stdio_driver {
    void (*out_chars)(const char *buf, int len);
    void (*out_flush)(void);
} stdio_driver_t;
void stdio_set_driver_enabled(stdio_driver_t *driver, bool enabled);
void stdio_flush(void);

// --- random -------------------------------------------------------------------
uint32_t get_rand_32(void);

// --- UART ---------------------------------------------------------------------
typedef struct { volatile uint32_t dr; } uart_hw_t;
typedef struct uart_inst uart_inst_t;
extern uart_inst_t *const standin_uart1;
#define uart1 standin_uart1
#define UART_PARITY_NONE 0
uint       uart_init(uart_inst_t *uart, uint baud);
void       uart_set_format(uart_inst_t *uart, uint data_bits, uint stop_bits, int parity);
void       uart_set_hw_flow(uart_inst_t *uart, bool cts, bool rts);
void       uart_set_fifo_enabled(uart_inst_t *uart, bool enabled);
uint       uart_get_dreq_num(uart_inst_t *uart, bool is_tx);
uart_hw_t *uart_get_hw(uart_inst_t *uart);
bool       uart_is_writable(uart_inst_t *uart);
void       uart_putc_raw(uart_inst_t *uart, char c);

// --- DMA ----------------------------------------------------------------------
// write_addr holds a host pointer here; pi_link.c's ring_w() takes the low 32
// bits of it and of the ring's address, as it does on the board.
typedef struct { volatile uintptr_t write_addr; } dma_channel_hw_t;
typedef struct { dma_channel_hw_t ch[16]; } dma_hw_t;
extern dma_hw_t *const dma_hw;
enum dma_channel_transfer_size { DMA_SIZE_8 = 0, DMA_SIZE_16 = 1, DMA_SIZE_32 = 2 };
typedef struct { uint32_t ctrl; } dma_channel_config;
int                dma_claim_unused_channel(bool required);
dma_channel_config dma_channel_get_default_config(uint channel);
void channel_config_set_transfer_data_size(dma_channel_config *c, enum dma_channel_transfer_size size);
void channel_config_set_read_increment(dma_channel_config *c, bool incr);
void channel_config_set_write_increment(dma_channel_config *c, bool incr);
void channel_config_set_ring(dma_channel_config *c, bool write, uint size_bits);
void channel_config_set_dreq(dma_channel_config *c, uint dreq);
void dma_channel_set_config(uint channel, const dma_channel_config *c, bool trigger);
void dma_channel_set_read_addr(uint channel, const volatile void *read_addr, bool trigger);
void dma_channel_set_write_addr(uint channel, volatile void *write_addr, bool trigger);
void dma_channel_set_trans_count(uint channel, uint32_t trans_count, bool trigger);
uint32_t dma_encode_endless_transfer_count(void);

#endif // PITRAC_LINK_STANDIN_H
