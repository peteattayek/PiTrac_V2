// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// dma_safe.h -- stopping a DMA channel the way the RP2350 datasheet says to.
//
// The SDK's dma_channel_abort() writes CHAN_ABORT and polls CTRL.BUSY. The
// datasheet (12.6.8, erratum RP2350-E5, and the CHAN_ABORT register itself)
// says otherwise:
//   1. clear the channel's EN bit (and disarm CHAIN_TO) first -- a channel under
//      ABORT can be re-triggered on the last cycle before ABORT clears, and then
//      "completes" with its transfer count held at zero, i.e. does nothing;
//   2. write CHAN_ABORT;
//   3. poll CHAN_ABORT until the channel's bit clears: "Until this point, it is
//      unsafe to restart the channel." In-flight transfers "cannot be revoked":
//      the bit clears only once they have been flushed through the address and
//      data FIFOs, so a completed abort is also a completion barrier.
// Independent review round 5, 2026-10-12. Every channel this firmware stops goes
// through here.

#ifndef PITRAC_DMA_SAFE_H
#define PITRAC_DMA_SAFE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool completed;    // CHAN_ABORT and BUSY both cleared within the bound
    bool read_error;   // CTRL.READ_ERROR was set (a bus error on a read)
    bool write_error;  // CTRL.WRITE_ERROR was set (a bus error on a write)
} dma_stop_t;

// Steps 1-3 above, bounded (tens of us). The error flags are reported and left
// as they were: changing EN and CHAIN_TO writes them as 0, which does not clear
// a write-1-to-clear flag. CHAIN_TO is set to the channel itself, which is how
// the hardware spells "no chaining" (0 would chain to channel 0).
dma_stop_t dma_stop_safely(unsigned ch);

// Clear a STOPPED channel's READ_ERROR / WRITE_ERROR (write-1-to-clear) before
// it is reconfigured, so a flag seen after the next run belongs to that run.
// Writes the non-triggering CTRL alias with EN = 0: it cannot start the channel.
void dma_clear_errors(unsigned ch);

#endif // PITRAC_DMA_SAFE_H
