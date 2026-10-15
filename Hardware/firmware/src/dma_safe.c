// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
#include "dma_safe.h"

#include "hardware/dma.h"
#include "pico/stdlib.h"

// "This generally takes only a few cycles" (12.6.8). 100 us is a generous bound
// that still cannot hang the caller.
#define DMA_STOP_TIMEOUT_US  100u

dma_stop_t dma_stop_safely(unsigned ch) {
    dma_stop_t r = { false, false, false };
    if (ch >= NUM_DMA_CHANNELS) return r;
    dma_channel_hw_t *hw = dma_channel_hw_addr(ch);

    // 1. EN off and CHAIN_TO to itself, through the non-triggering CTRL alias.
    //    READ_ERROR / WRITE_ERROR are write-1-to-clear: masking them to 0 in the
    //    written value leaves them set for step 4 to report.
    const uint32_t wc = DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
    uint32_t ctrl = hw->al1_ctrl;
    ctrl &= ~(wc | DMA_CH0_CTRL_TRIG_EN_BITS | DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS);
    ctrl |= (ch << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB) & DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS;
    hw->al1_ctrl = ctrl;

    // 2-3. Abort, then poll CHAN_ABORT (not just BUSY) until the channel's bit
    //      clears: in-flight transfers have then been flushed.
    dma_hw->abort = 1u << ch;
    const uint64_t t0 = time_us_64();
    bool done = false;
    while (time_us_64() - t0 < DMA_STOP_TIMEOUT_US) {
        if ((dma_hw->abort & (1u << ch)) == 0u &&
            (hw->ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) == 0u) { done = true; break; }
    }

    // 4. Report, do not clear, the bus-error flags (dma_clear_errors() does).
    const uint32_t after = hw->ctrl_trig;
    r.completed   = done;
    r.read_error  = (after & DMA_CH0_CTRL_TRIG_READ_ERROR_BITS) != 0u;
    r.write_error = (after & DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS) != 0u;
    return r;
}

void dma_clear_errors(unsigned ch) {
    if (ch >= NUM_DMA_CHANNELS) return;
    dma_channel_hw_t *hw = dma_channel_hw_addr(ch);
    const uint32_t wc = DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
    hw->al1_ctrl = (hw->al1_ctrl & ~DMA_CH0_CTRL_TRIG_EN_BITS) | wc;
}
