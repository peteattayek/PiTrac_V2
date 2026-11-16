// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// fw_update.h -- installing a new image sent by the Pi, with automatic rollback.
//
// WHY IT WORKS THIS WAY. Any RP2354 reset opens the +5V latch (R12), which is a
// hard power cut to a Pi this board powers. So the Pi cannot flash the RP2354 the
// usual ways: SWD or BOOTSEL both end in a reset while the Pi is still running.
// Instead the running firmware takes the new image over the serial link, and the
// one reset happens after the Pi has shut down:
//
//   1. UPD_BEGIN / UPD_DATA / UPD_END: the Pi sends the image (build/pitrac.bin)
//      into a RAM staging buffer, and the board checks its SHA-256 and that it is
//      an RP2350 Arm executable flagged try-before-you-buy (TBYB).
//   2. UPD_APPLY: the board records in the NOTE (below) that it owes the Pi its
//      power back, then starts its normal orderly shutdown of the Pi
//      (RPI5_SHUTDOWN pulse, wait for the Pi to read down).
//   3. Back in STANDBY with the latch open, the board writes the image into the
//      OTHER partition of its A/B pair, one 4 KB sector per superloop pass, and
//      reads each one back. Sector 0, which holds the IMAGE_DEF, is erased first
//      and written last.
//   4. A FLASH_UPDATE reboot into that partition. The boot ROM runs the new
//      image once, on trial, with a 16.7 s watchdog.
//   5. The new image checks itself at the end of init and BUYS itself
//      (rom_explicit_buy), then powers the Pi back on.
//   6. If it never buys -- it crashed, hung, or failed its check -- the next boot
//      is a normal one, and the boot ROM will not launch an unbought TBYB image:
//      the old image runs again, reads the note, and powers the Pi back on to
//      report the rollback.
//
// WHAT THE OTHER PARTITION CAN HOLD (reviews 2026-10-20, R3-F1 and R4-F1). The
// boot ROM looks for a partition's first block only in the partition's first
// 4 KB (pico-bootrom-rp2350: BLOCK_LIST_SEARCH_MAX, s_varm_crit_next_block). It
// takes the first start marker there, reads on past the sector to finish that
// block, searches up to ~1 KB further if the block is invalid, then follows the
// block's links. A first sector that reads all 0xFF or all 0x00 holds no start
// marker, and then the ROM reads nothing beyond it. That, not a parser's
// verdict, is what "cleared" means here.
//   - Before the last step: sector 0 erased, every byte read back 0xFF.
//   - A reset during the last step (programming sector 0): programming only takes
//     bits from 1 to 0 and the TBYB flag is a 1 in the image, so the image's own
//     block reads TBYB-flagged or malformed. A malformed one sends the ROM's
//     search on into the next ~1 KB, where the image's body already is: harmless
//     while the image has no other start marker there (an SDK image has none: the
//     2026-10-20 build's only one below 0x1800 is its IMAGE_DEF at 0x138). A
//     well-formed block assembled by chance from half-programmed cells is not
//     excluded, only vanishingly unlikely: it needs a 32-bit start marker, valid
//     items and a 32-bit end marker at once.
//   - Any step that FAILS -- an erase or program reports an error, or a read-back
//     differs; in the last step e.g. a flash fault that cleared the TBYB bit and
//     left a valid image a normal boot WOULD launch: sector 0 is zeroed (16 page
//     programs, no erase needed) and, if any byte then reads back other than
//     0x00, erased. It counts as cleared only if every byte reads back 0x00, or
//     0xFF after the erase. Otherwise the install ends in FWU_E_CLEANUP: the
//     partition may then hold an image a reset would launch without a trial, and
//     the board must be reflashed over USB before any other reset.
//   - The one window left: two faults together. A fault in the last write that
//     leaves a valid non-TBYB block, then a reset before the cleanup has
//     overwritten that block: during the last write's remaining page programs,
//     its read-back, or the cleanup's programs up to the page that holds the
//     block. No duration is claimed for that window.
//   - The reboot refused (step 4 failing): the complete, TBYB-flagged image is
//     cleared the same way, so a failed install never leaves anything behind.
//   - The board leaving STANDBY mid-install: no flash write is allowed then, and
//     none is needed. Sector 0 is erased, or step 0 has not run, and the last
//     step runs whole within one superloop pass.
//
// WHAT THE IMAGE CHECK READS (UPD_END). fwu_parse_image() takes the first
// well-formed block wholly inside the image's first 4 KB that carries an
// IMAGE_TYPE item; the boot ROM walks the block loop from the first start marker
// it finds. For an SDK image these are the same block (the 2026-10-20 build: one
// start marker below 0x1800, and its loop through the end block at the image's
// end carries no other IMAGE_TYPE). But the check is not the ROM's: it stops a
// wrong build (not TBYB, not an RP2350 Arm executable), not an image crafted to
// differ. The Pi is trusted to send this repository's builds.
//
// THE PI'S POWER (review 2026-10-20, R3-F2). Owing the Pi its power back is a
// fact in the note, committed at UPD_APPLY, not a flag in RAM, so no warm reset
// can lose it. It goes live once the board is in STANDBY (the update has the Pi
// shut down by then; after a reset at once, since the reset cut the Pi's power)
// and ends when the FSM leaves STANDBY again: the board's own request acted on
// (the latch closed, or the USB guard refused it with a fault), or someone
// powering the board first. The request waits FWU_REPOWER_DELAY_MS into STANDBY,
// never runs during an install, and is made again each pass while the FSM
// refuses it; once accepted it is not repeated, since the FSM acts on it at its
// next step. A reset before that step leaves it owed, and the next boot asks
// again. Once met, it is gone: a later reset powers nothing. `upd abort` cancels
// it; an arm timeout that never saw STANDBY (the Pi never went down) cancels it
// too. A power-on (cold) reset loses the note, and with it the obligation, like
// any other state.
//
// No flash is written while a Pi is powered: the image waits in RAM, and the
// install is gated exactly like `cfg save` (config_store.h, "THE GUARD").
//
// FLASH LAYOUT (partition_table.json, installed once per board over USB):
//   0x000000  8 KB   partition table (unpartitioned)
//   0x002000  1016 KB  partition A
//   0x100000  1016 KB  partition B, linked to A
//   0x1FE000  8 KB   config sectors (unpartitioned; config_store.c)
// A board without the table still runs, flat at offset 0; it just cannot take
// an update from the Pi.
//
// Logic only: flash, the boot ROM and the layout are reached through the
// fwu_port_* functions (fw_layout.c on the board, the test file on the host).
// Compiled into the native host tests (tests/fw_update_test.c).

#ifndef PITRAC_FW_UPDATE_H
#define PITRAC_FW_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// The image version, (major << 16) | minor, from pico_set_binary_version() in
// CMakeLists.txt. The boot ROM compares it between A and B on a normal boot; the
// Pi tool shows it. It is not an ordering rule here: on a FLASH_UPDATE boot the
// ROM prefers the updated partition whatever its version. At the buy, the ROM
// erases the other one's first sector only if the other would otherwise win the
// next normal boot: a higher version, or an equal version in A (equal versions
// boot A). An equal-version image bought in A leaves B as a fallback.
// ---------------------------------------------------------------------------
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#  ifndef PICO_CRT0_VERSION_MAJOR
#    error "No image version: CMakeLists.txt must call pico_set_binary_version()"
#  endif
#else
#  ifndef PICO_CRT0_VERSION_MAJOR
#    define PICO_CRT0_VERSION_MAJOR 1
#  endif
#  ifndef PICO_CRT0_VERSION_MINOR
#    define PICO_CRT0_VERSION_MINOR 1
#  endif
#endif
#define FWU_VERSION(major, minor)  (((uint32_t)(major) << 16) | (uint32_t)(minor))
#define PITRAC_FW_VERSION          FWU_VERSION(PICO_CRT0_VERSION_MAJOR, PICO_CRT0_VERSION_MINOR)

#define FWU_SECTOR             4096u
#define FWU_PAGE               256u

// RAM the image is staged in while the Pi runs. Bigger images are refused at
// UPD_BEGIN. 2026-10-20 (r4): the image is 220 KB and the firmware's other RAM
// ~155 KB of 512; this leaves ~68 KB free. Raise it only with the RAM to spare.
#define FWU_STAGE_BYTES        (288u * 1024u)

// UPD_APPLY must reach STANDBY within this. The orderly shutdown it starts ends
// by PI_SHUTDOWN_MAX_WAIT_MS (60 s) whatever the Pi does; this is a backstop.
#define FWU_ARM_TIMEOUT_MS     120000u
// STANDBY time before the first flash write of an install.
#define FWU_INSTALL_QUIET_MS   1000u
// STANDBY time before the Pi is powered again, after an update.
#define FWU_REPOWER_DELAY_MS   1000u

// ---------------------------------------------------------------------------
typedef enum {
    FWU_IDLE = 0,       // nothing staged
    FWU_RECEIVING,      // UPD_BEGIN accepted: UPD_DATA expected at `received`
    FWU_VERIFIED,       // the whole image is in RAM; digest and image checked
    FWU_ARMED,          // UPD_APPLY accepted: install once the Pi is powered down
    FWU_INSTALLING,     // writing the other partition, one sector per pass
    FWU__STATES
} fwu_state_t;

// Status codes, as sent to the Pi (pi/pitrac_link.py keeps the same names).
typedef enum {
    FWU_OK = 0,
    FWU_E_STATE,        // not allowed in the current update state
    FWU_E_NO_SLOT,      // no A/B partition to install into (see `upd`)
    FWU_E_SIZE,         // empty, over FWU_STAGE_BYTES, or over the partition
    FWU_E_OFFSET,       // UPD_DATA not at the next expected offset
    FWU_E_LENGTH,       // UPD_DATA empty, or past the declared size
    FWU_E_INCOMPLETE,   // UPD_END before every byte arrived
    FWU_E_DIGEST,       // the SHA-256 does not match UPD_BEGIN's
    FWU_E_IMAGE,        // no RP2350 Arm Secure executable IMAGE_DEF in the first 4 KB
    FWU_E_NOT_TBYB,     // not flagged try-before-you-buy: no rollback if it failed
    FWU_E_POWER,        // UPD_APPLY needs the board RUNNING a Pi
    FWU_E_ARM_TIMEOUT,  // the board did not reach STANDBY in FWU_ARM_TIMEOUT_MS
    FWU_E_INTERRUPTED,  // the install was cut short: the board left STANDBY, or reset
    FWU_E_FLASH,        // a sector did not read back as written (the partner was cleared)
    FWU_E_REBOOT,       // the boot ROM refused the update reboot (the partner was cleared)
    FWU_E_CLEANUP,      // a failed install's partner could NOT be cleared: reflash over USB
    FWU_E__COUNT
} fwu_err_t;

// What became of the last update, decided by the boot after it, or by the failed
// install itself.
typedef enum {
    FWU_RESULT_NONE = 0,
    FWU_RESULT_INSTALLED,       // the new image runs
    FWU_RESULT_ROLLED_BACK,     // the new image never bought itself; the old one runs
    FWU_RESULT_INSTALL_FAILED,  // the install did not finish; the old image never stopped
} fwu_result_t;

// Why a trial image did not buy itself.
typedef enum {
    FWU_TRIAL_OK = 0,
    FWU_TRIAL_LAYOUT,         // the image reaches into the config sectors
    FWU_TRIAL_ADC,            // +5V_IN could not be read FRESH: the ADC ring is not running
    FWU_TRIAL_BUY_FAILED,     // rom_explicit_buy() returned an error
    FWU_TRIAL__COUNT
} fwu_trial_t;

// INSTALLED's detail.
#define FWU_INSTALLED_ON_TRIAL   0u   // launched on trial, and bought itself
#define FWU_INSTALLED_NO_TRIAL   1u   // launched by a normal boot: no rollback was in place

// ---------------------------------------------------------------------------
// The IMAGE_DEF at the start of an image (RP2350 datasheet 5.9; SDK
// embedded_start_block.inc.S): the block found in the first 4 KB.
typedef struct {
    bool     found;            // a well-formed block with an IMAGE_TYPE item
    uint16_t image_type;       // the IMAGE_TYPE item's flags
    bool     exe_arm_s_rp2350; // an executable, Arm, Secure, for the RP2350
    bool     tbyb;             // flagged try-before-you-buy, i.e. not yet bought
    bool     has_version;
    uint32_t version;          // FWU_VERSION(major, minor), 0 without a VERSION item
} fwu_image_t;

// Looks only at the first min(n, 4096) bytes. The image check, not the boot ROM's
// view: see "WHAT THE IMAGE CHECK READS" above.
bool fwu_parse_image(const uint8_t *p, size_t n, fwu_image_t *out);

// ---------------------------------------------------------------------------
// The firmware side.

// End of init, before main() disarms the watchdog. Loads the note. On a trial
// boot (the boot ROM launched this image TBYB on a FLASH_UPDATE boot): check the
// image, and buy it, or reboot normally so the ROM runs the previous one. Then
// settle the note's result, if an update left one unsettled.
//
// THE ORDER IN main() IS LOAD-BEARING. The ROM's trial watchdog fires 16.7 s
// after launch unless the watchdog is disarmed, and main() disarms it right
// after this call. The Pi is powered again only from fwu_service(), i.e. from
// the superloop, after that disarm: no reset may be pending once it is up.
void fwu_boot_check(void);

// Superloop only (never from pitrac_service(): it writes flash): the ARMED wait,
// the install a sector at a time, the reboot, the arm timeout, and the Pi's power
// after an update.
void fwu_service(void);

// The link's commands. Each is idempotent for a retry of the same request:
// BEGIN restarts, a repeated DATA chunk is accepted again, END and APPLY repeat
// their answer.
fwu_err_t fwu_begin(uint32_t size, const uint8_t digest[32]);
fwu_err_t fwu_data(uint32_t offset, const uint8_t *p, uint32_t n, uint32_t *next);
fwu_err_t fwu_end(uint32_t *version);
fwu_err_t fwu_apply(void);
// Anything staged is dropped and an install stops; a pending power-on for the Pi
// is cancelled (`upd abort`).
fwu_err_t fwu_abort(void);

typedef struct {
    fwu_state_t  state;
    fwu_err_t    last_error;      // the result of the last command or install step
    uint32_t     size, received;  // of the staged image
    uint32_t     staged_version;  // once verified
    uint32_t     install_step, install_steps;
    uint32_t     armed_for_ms;    // ARMED: since UPD_APPLY
    // This boot:
    bool         trial;           // launched TBYB by the boot ROM
    bool         bought;          // ...and bought itself
    int          buy_rc;          // rom_explicit_buy()'s return
    // The last update, from the note (FWU_RESULT_NONE if there is none):
    fwu_result_t result;
    uint32_t     result_from, result_to;   // versions
    // INSTALLED: FWU_INSTALLED_*; ROLLED_BACK: a fwu_trial_t; INSTALL_FAILED: a
    // fwu_err_t. fwu_detail_name() says it in words.
    uint32_t     result_detail;
    bool         result_this_boot;  // decided at this boot, not an earlier one
    bool         repower_pending;   // the board still owes the Pi its power
} fwu_status_t;

void fwu_status(fwu_status_t *out);

const char *fwu_state_name(fwu_state_t s);
const char *fwu_err_name(fwu_err_t e);
const char *fwu_result_name(fwu_result_t r);
const char *fwu_trial_name(fwu_trial_t t);
const char *fwu_detail_name(fwu_result_t r, uint32_t detail);

// The staging buffer, for the boot ROM's 4 KB work area at boot (partition-table
// load, explicit buy), when nothing can be staged yet. NULL once an update has
// started.
uint8_t *fwu_scratch(size_t *size);

// ---------------------------------------------------------------------------
// The port: what fw_update.c needs from the board (fw_layout.c) or a test.

typedef struct {
    bool     partitioned;      // this image runs in one partition of an A/B pair
    uint32_t run_start, run_size;       // its partition (unpartitioned: 0, flash size)
    uint32_t other_start, other_size;   // the partner; size 0 when there is none
    uint32_t cfg_start, cfg_size;       // the config sectors
    bool     buy_pending;      // launched TBYB on a FLASH_UPDATE boot: not bought yet
    uint8_t  boot_type;        // the boot ROM's BOOT_TYPE_*
    int8_t   boot_partition;   // the ROM's partition index, -1 for none
    const char *why_not;       // !partitioned: why, for `upd` and the Pi
} fwu_layout_t;

const fwu_layout_t *fwu_port_layout(void);
// Physical flash offsets. Erase is one 4 KB sector; program is whole pages
// inside one sector. False if the operation could not be run.
bool fwu_port_erase(uint32_t offs);
bool fwu_port_program(uint32_t offs, const uint8_t *src, uint32_t n);
// Physical flash, uncached and untranslated: what is really there.
const uint8_t *fwu_port_read(uint32_t offs);
// rom_explicit_buy(). 0 on success.
int  fwu_port_buy(void);
// A FLASH_UPDATE reboot into the partition starting at `start`. Returns only on
// failure, with the boot ROM's error.
int  fwu_port_reboot_update(uint32_t start);
// A normal reboot (a trial that will not buy). Returns only in the host tests.
void fwu_port_reboot_normal(void);
// One line for the USB console ("update: <msg>"). Nothing else is printed here.
void fwu_port_log(const char *msg);

#if !(defined(PICO_ON_DEVICE) && PICO_ON_DEVICE)
// Host tests only:
//   - what a power-on does to the note (noise in both copies);
//   - a reset of this module's RAM, as a reboot would, keeping the note;
//   - one corrupted word in the newest copy of the note, or in the other one;
//   - a hook called before every word a note commit stores and once after the
//     last: where a test can stage a reset in the middle of a commit.
void fwu_test_power_on(void);
void fwu_test_reboot(void);
void fwu_test_note_flip(bool newest, unsigned word, uint32_t mask);
extern void (*fwu_test_store_hook)(void);
#endif

#endif // PITRAC_FW_UPDATE_H
