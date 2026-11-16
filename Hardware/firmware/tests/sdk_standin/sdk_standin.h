// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// sdk_standin.h -- just enough of the Pico SDK's declarations to compile the
// UNCHANGED strobe.c, camera.c and shot.c on a PC (tests/engine_test.c). Every
// function is defined by the test, as a small model of the registers those files
// read and write: PIO state machines (enable, PC, FIFOs, IRQ flags), GPIO pads
// (function, SIO output, direction, input level), the gate DAC's PWM compare.
//
// It models control flow and register state, NOT bus timing, analog behaviour or
// what a state machine does on its own: the test moves a PC, raises an IRQ flag,
// or drives a pad, the way the hardware would at that point. The PIO programs
// themselves are checked cycle by cycle in tests/cam_pio_test.c.
//
// Added 2026-11-10 for the independent review's findings H7-F2 and H7-F3.

#ifndef PITRAC_SDK_STANDIN_H
#define PITRAC_SDK_STANDIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned int uint;
typedef uint64_t absolute_time_t;

#define count_of(a) (sizeof(a) / sizeof((a)[0]))
void standin_hard_assert(int ok, const char *what, const char *file, int line);
#define hard_assert(x) standin_hard_assert((x) ? 1 : 0, #x, __FILE__, __LINE__)

// --- clocks, time, interrupts -------------------------------------------------
#define clk_sys 5
uint32_t        clock_get_hz(int clk);
uint64_t        time_us_64(void);
absolute_time_t get_absolute_time(void);
uint32_t        to_ms_since_boot(absolute_time_t t);
void            busy_wait_us(uint64_t us);
uint32_t        save_and_disable_interrupts(void);
void            restore_interrupts(uint32_t status);

// --- GPIO ---------------------------------------------------------------------
typedef enum {
    GPIO_FUNC_HSTX = 0, GPIO_FUNC_SPI = 1, GPIO_FUNC_UART = 2, GPIO_FUNC_I2C = 3,
    GPIO_FUNC_PWM = 4, GPIO_FUNC_SIO = 5, GPIO_FUNC_PIO0 = 6, GPIO_FUNC_PIO1 = 7,
    GPIO_FUNC_PIO2 = 8, GPIO_FUNC_NULL = 0x1f
} gpio_function_t;
#define GPIO_OUT 1
#define GPIO_IN  0
bool            gpio_get(uint pin);
bool            gpio_get_out_level(uint pin);
bool            gpio_is_dir_out(uint pin);
gpio_function_t gpio_get_function(uint pin);
void            gpio_put(uint pin, bool value);
void            gpio_set_dir(uint pin, bool out);
void            gpio_set_function(uint pin, gpio_function_t fn);

// --- PIO ----------------------------------------------------------------------
typedef struct {
    volatile uint32_t ctrl;      // SM_ENABLE in bits 3:0 (kept by the model)
    volatile uint32_t txf[4];    // DMA destination only
} pio_hw_t;
typedef pio_hw_t *PIO;
extern pio_hw_t standin_pio[3];
#define pio0 (&standin_pio[0])
#define pio1 (&standin_pio[1])
#define pio2 (&standin_pio[2])
#define PIO_INSTRUCTION_COUNT   32u
#define PIO_CTRL_SM_ENABLE_LSB  0u

typedef struct pio_program {
    const uint16_t *instructions;
    uint8_t         length;
    int8_t          origin;
} pio_program_t;

// The generated headers are compiled with PICO_NO_HARDWARE=1, which keeps only
// the instruction arrays; the test defines these from them.
extern const pio_program_t strobe_burst_program;
extern const pio_program_t strobe_gate_program;
extern const pio_program_t cam_handshake_program;
void strobe_burst_program_init(PIO pio, uint sm, uint offset, uint pin);
void cam_handshake_program_init(PIO pio, uint sm, uint offset, uint pin_trigger,
                                uint pin_strobe0, uint pin_strobe1);

uint  pio_get_index(PIO pio);
uint  pio_get_funcsel(PIO pio);
uint  pio_get_gpio_base(PIO pio);
void  pio_sm_claim(PIO pio, uint sm);
int   pio_add_program(PIO pio, const pio_program_t *prog);
int   pio_add_program_at_offset(PIO pio, const pio_program_t *prog, uint offset);
void  pio_sm_set_enabled(PIO pio, uint sm, bool enabled);
void  pio_sm_clear_fifos(PIO pio, uint sm);
void  pio_sm_restart(PIO pio, uint sm);
uint  pio_encode_jmp(uint addr);
void  pio_sm_exec(PIO pio, uint sm, uint instr);
void  pio_sm_set_pins_with_mask(PIO pio, uint sm, uint32_t values, uint32_t mask);
void  pio_sm_set_pindirs_with_mask(PIO pio, uint sm, uint32_t dirs, uint32_t mask);
void  pio_gpio_init(PIO pio, uint pin);
uint  pio_get_dreq(PIO pio, uint sm, bool is_tx);
uint  pio_sm_get_pc(PIO pio, uint sm);
uint  pio_sm_get_tx_fifo_level(PIO pio, uint sm);
uint  pio_sm_get_rx_fifo_level(PIO pio, uint sm);
bool  pio_sm_is_rx_fifo_empty(PIO pio, uint sm);
void  pio_sm_put(PIO pio, uint sm, uint32_t data);
uint32_t pio_sm_get(PIO pio, uint sm);
bool  pio_interrupt_get(PIO pio, uint irq);
void  pio_interrupt_clear(PIO pio, uint irq);

// --- DMA ----------------------------------------------------------------------
enum dma_channel_transfer_size { DMA_SIZE_8 = 0, DMA_SIZE_16 = 1, DMA_SIZE_32 = 2 };
typedef struct { uint32_t ctrl; } dma_channel_config;
int                dma_claim_unused_channel(bool required);
dma_channel_config dma_channel_get_default_config(uint ch);
void channel_config_set_transfer_data_size(dma_channel_config *c, enum dma_channel_transfer_size s);
void channel_config_set_read_increment(dma_channel_config *c, bool incr);
void channel_config_set_write_increment(dma_channel_config *c, bool incr);
void channel_config_set_dreq(dma_channel_config *c, uint dreq);
void dma_channel_configure(uint ch, const dma_channel_config *c, volatile void *write_addr,
                           const volatile void *read_addr, uint count, bool trigger);

// --- PWM (the gate DAC) ---------------------------------------------------------
typedef struct { uint32_t csr, div, top; } pwm_config;
typedef struct { struct { uint32_t csr, div, ctr, cc, top; } slice[12]; } pwm_hw_t;
extern pwm_hw_t *pwm_hw;
uint       pwm_gpio_to_slice_num(uint pin);
uint       pwm_gpio_to_channel(uint pin);
pwm_config pwm_get_default_config(void);
void       pwm_config_set_wrap(pwm_config *c, uint16_t wrap);
void       pwm_config_set_clkdiv_int(pwm_config *c, uint div);
void       pwm_init(uint slice, const pwm_config *c, bool start);
void       pwm_set_chan_level(uint slice, uint chan, uint16_t level);
void       pwm_set_enabled(uint slice, bool enabled);

#endif // PITRAC_SDK_STANDIN_H
