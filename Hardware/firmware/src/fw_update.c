// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PiTrac contributors
// fw_update.c -- see fw_update.h. Logic only: compiled into the host tests.

#include "fw_update.h"
#include "sha256.h"
#include "board.h"
#include "power_fsm.h"
#include "config_store.h"
#include "adc_engine.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
// Not zeroed by the runtime: survives the update reboot and the boot ROM's trial
// watchdog. The boot path keeps its workspace in USB RAM and its stack in boot
// RAM (pico-bootrom-rp2350, varm_boot_path.c), never main SRAM. A power-on
// leaves noise, which the magic and the CRC reject.
#define FWU_NOINIT(name) __uninitialized_ram(name)
#define STORE_POINT()    ((void)0)
#else
#define FWU_NOINIT(name) name
void (*fwu_test_store_hook)(void);
#define STORE_POINT() do { if (fwu_test_store_hook) fwu_test_store_hook(); } while (0)
#endif

// ---------------------------------------------------------------------------
// picobin blocks (RP2350 datasheet 5.9.1; SDK boot/picobin.h)
// ---------------------------------------------------------------------------
#define PICOBIN_MARKER_START   0xffffded3u
#define PICOBIN_MARKER_END     0xab123579u
#define ITEM_IMAGE_TYPE        0x42u    // PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE
#define ITEM_VERSION           0x48u    // PICOBIN_BLOCK_ITEM_1BS_VERSION
#define ITEM_LAST              0xffu    // PICOBIN_BLOCK_ITEM_2BS_LAST
#define IMAGE_TYPE_EXE_ARM_S_RP2350_MASK  0x773fu   // type, security, cpu, chip fields
#define IMAGE_TYPE_EXE_ARM_S_RP2350       0x1021u   // EXE, Secure, Arm, RP2350
#define IMAGE_TYPE_TBYB                   0x8000u

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// One block at `at` (its start marker already matched). Items until LAST, whose
// size field must equal the words walked; then the link word and the end
// marker. False if it is not well formed or carries no IMAGE_TYPE item.
static bool parse_block(const uint8_t *p, size_t n, size_t at, fwu_image_t *img) {
    memset(img, 0, sizeof(*img));
    bool have_type = false;
    size_t i = at + 4u;
    uint32_t words = 0u;
    while (i + 4u <= n) {
        const uint8_t type = p[i];
        const uint32_t size = (type & 0x80u) ? rd16(&p[i + 1u]) : p[i + 1u];   // in words
        if (type == ITEM_LAST) {
            if (size != words || i + 12u > n || rd32(&p[i + 8u]) != PICOBIN_MARKER_END) return false;
            img->found = have_type;
            return have_type;
        }
        if (size == 0u || i + 4u * (size_t)size > n) return false;
        if (type == ITEM_IMAGE_TYPE && size == 1u) {
            img->image_type = rd16(&p[i + 2u]);
            have_type = true;
        } else if (type == ITEM_VERSION && size >= 2u) {
            img->has_version = true;
            img->version = FWU_VERSION(rd16(&p[i + 6u]), rd16(&p[i + 4u]));
        }
        i += 4u * (size_t)size;
        words += size;
    }
    return false;
}

bool fwu_parse_image(const uint8_t *p, size_t n, fwu_image_t *out) {
    memset(out, 0, sizeof(*out));
    if (!p) return false;
    if (n > FWU_SECTOR) n = FWU_SECTOR;
    for (size_t at = 0; at + 16u <= n; at += 4u) {
        if (rd32(&p[at]) != PICOBIN_MARKER_START) continue;
        fwu_image_t img;
        if (!parse_block(p, n, at, &img)) continue;
        img.exe_arm_s_rp2350 = (img.image_type & IMAGE_TYPE_EXE_ARM_S_RP2350_MASK)
                               == IMAGE_TYPE_EXE_ARM_S_RP2350;
        img.tbyb = (img.image_type & IMAGE_TYPE_TBYB) != 0u;
        *out = img;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
static uint8_t s_stage[FWU_STAGE_BYTES] __attribute__((aligned(4)));
#else
static uint8_t s_stage[FWU_STAGE_BYTES];
#endif
// What clears a partner's sector 0. In RAM: a flash program's source may not be
// in flash, which is not readable while it is being written.
static uint8_t s_zero_page[FWU_PAGE];

static fwu_state_t s_state;
static fwu_err_t   s_last_error;
static uint32_t    s_size, s_received, s_last_n;
static uint8_t     s_digest[SHA256_BYTES];
static sha256_t    s_sha;
static uint32_t    s_version;            // the staged image's, once verified
static uint32_t    s_t_armed;
static uint32_t    s_step;               // install: the next step, 0..sectors

// This boot.
static bool s_trial, s_bought, s_settled_here;
static int  s_buy_rc;
static bool s_seen_standby;              // a pending power-on has seen STANDBY (it is live)
static bool s_asked;                     // ...and has been requested (for the log only)

// ---------------------------------------------------------------------------
// The note: what an update leaves for the boots after it, in no-init RAM.
//
// It survives the update reboot, the boot ROM's trial watchdog and any warm reset;
// a power-on leaves noise, which the magic and the CRC reject. Like the telemetry
// mirror it is TWO copies, each one whole commit with a sequence number and a
// CRC, written alternately: a reset partway through a commit leaves the previous
// one as the newest valid copy, so a reset sees the note as it was before a
// change or after it, never a mix, and never none where there was one.
//
// THE LAYOUT AND ADDRESS ARE A CONTRACT BETWEEN IMAGES: the image that installs
// writes the note, the next image reads it. tools/check_retained.cmake fails the
// build if s_notes (or the telemetry mirror) moves.
// ---------------------------------------------------------------------------
#define FWU_NOTE_MAGIC  0x46575532u      // "FWU2": this layout
enum {
    NOTE_ARMED = 1u,      // UPD_APPLY accepted: the Pi is being shut down for an install
    NOTE_TRYING,          // installed and verified; rebooting into it on trial
    NOTE_TRIAL_FAILED,    // the trial image's check failed; it rebooted to the old one
    NOTE_DONE,            // the result is settled
};
typedef struct {
    uint32_t magic;
    uint32_t seq;              // of two valid copies, the higher wins
    uint32_t phase;            // NOTE_*
    uint32_t target_start;     // the partition the image goes to
    uint32_t target_version;
    uint32_t from_start;       // where the installing image runs
    uint32_t from_version;
    uint32_t result;           // fwu_result_t, once NOTE_DONE
    uint32_t detail;           // per result (fwu_status_t); a trial's fwu_trial_t before that
    uint32_t repower;          // 1: the board owes the Pi its power back (fw_update.h)
    uint32_t crc;              // over everything above
} fwu_note_t;
#define NOTE_WORDS (sizeof(fwu_note_t) / sizeof(uint32_t))

static fwu_note_t FWU_NOINIT(s_notes)[2];
static fwu_note_t s_note;       // the newest commit (loaded at boot, or just made)
static bool       s_note_ok;    // s_note is one
static unsigned   s_note_next;  // the copy the next commit writes

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

// Reflected CRC-32, bitwise: ten words.
static uint32_t note_crc(const fwu_note_t *n) {
    const uint8_t *p = (const uint8_t *)n;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < offsetof(fwu_note_t, crc); i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static bool note_valid(const fwu_note_t *n) {
    return n->magic == FWU_NOTE_MAGIC && n->crc == note_crc(n)
        && n->phase >= NOTE_ARMED && n->phase <= NOTE_DONE;
}

// The newest valid copy, or -1 when neither is valid.
static int note_newest(void) {
    const bool v0 = note_valid(&s_notes[0]), v1 = note_valid(&s_notes[1]);
    if (v0 && v1) return (int32_t)(s_notes[1].seq - s_notes[0].seq) > 0 ? 1 : 0;
    return v0 ? 0 : (v1 ? 1 : -1);
}

static void note_load(void) {
    const int k = note_newest();
    s_note_ok = k >= 0;
    if (s_note_ok) {
        s_note = s_notes[k];
        s_note_next = (unsigned)k ^ 1u;
    } else {
        memset(&s_note, 0, sizeof(s_note));
        s_note_next = 0u;
    }
}

// s_note as the next commit, into the copy that is not the newest, one word at a
// time, in whatever order: a reset partway through leaves that copy failing its
// CRC or still holding the commit before last, and the other copy -- the
// previous commit -- the newest valid one.
static void note_commit(void) {
    s_note.magic = FWU_NOTE_MAGIC;
    s_note.seq   = s_note_ok ? s_note.seq + 1u : 1u;
    s_note.crc   = note_crc(&s_note);
    const uint32_t *src = (const uint32_t *)&s_note;
    uint32_t *dst = (uint32_t *)&s_notes[s_note_next];
    for (size_t i = 0; i < NOTE_WORDS; i++) {
        STORE_POINT();
        dst[i] = src[i];
    }
    STORE_POINT();
    s_note_next ^= 1u;
    s_note_ok = true;
}

// The result, settled. The power-on obligation is left as it is.
static void note_done(fwu_result_t r, uint32_t detail) {
    s_note.phase  = NOTE_DONE;
    s_note.result = (uint32_t)r;
    s_note.detail = detail;
    note_commit();
}

static bool repower_pending(void) { return s_note_ok && s_note.repower != 0u; }

static fwu_err_t fail_cmd(fwu_err_t e) { s_last_error = e; return e; }

static uint32_t sectors(uint32_t bytes) { return (bytes + FWU_SECTOR - 1u) / FWU_SECTOR; }

static bool overlap(uint32_t a0, uint32_t an, uint32_t b0, uint32_t bn) {
    return a0 < b0 + bn && b0 < a0 + an;
}

// Somewhere to install: the partner partition, clear of this image and of the
// config sectors. fw_layout.c checks the same; this is the copy the host tests
// can see.
static bool layout_ok(const fwu_layout_t *L) {
    return L && L->partitioned && L->other_size >= FWU_SECTOR
        && L->other_start % FWU_SECTOR == 0u && L->other_size % FWU_SECTOR == 0u
        && !overlap(L->other_start, L->other_size, L->run_start, L->run_size)
        && !overlap(L->other_start, L->other_size, L->cfg_start, L->cfg_size);
}

// ---------------------------------------------------------------------------
// The image from the Pi
// ---------------------------------------------------------------------------
fwu_err_t fwu_begin(uint32_t size, const uint8_t digest[32]) {
    // Once UPD_APPLY is accepted the update runs its course; UPD_ABORT stops it.
    if (s_state == FWU_ARMED || s_state == FWU_INSTALLING) return fail_cmd(FWU_E_STATE);
    const fwu_layout_t *L = fwu_port_layout();
    if (!layout_ok(L)) return fail_cmd(FWU_E_NO_SLOT);
    if (size == 0u || size > FWU_STAGE_BYTES || size > L->other_size) return fail_cmd(FWU_E_SIZE);

    s_state    = FWU_RECEIVING;
    s_size     = size;
    s_received = 0u;
    s_last_n   = 0u;
    s_version  = 0u;
    memcpy(s_digest, digest, SHA256_BYTES);
    sha256_init(&s_sha);
    // The install writes whole sectors from here: the tail past the image is
    // programmed as 0xFF, the erased state.
    memset(s_stage, 0xFF, (size_t)sectors(size) * FWU_SECTOR);
    s_last_error = FWU_OK;
    return FWU_OK;
}

fwu_err_t fwu_data(uint32_t offset, const uint8_t *p, uint32_t n, uint32_t *next) {
    if (next) *next = s_received;
    if (s_state != FWU_RECEIVING) return fail_cmd(FWU_E_STATE);
    if (n == 0u || offset > s_size || n > s_size - offset) return fail_cmd(FWU_E_LENGTH);
    if (offset == s_received) {
        memcpy(&s_stage[offset], p, n);
        sha256_update(&s_sha, p, n);
        s_received += n;
        s_last_n = n;
        if (next) *next = s_received;
        s_last_error = FWU_OK;
        return FWU_OK;
    }
    // The chunk just taken, again: its answer was lost. The same bytes, or the
    // Pi has lost track.
    if (s_last_n && n == s_last_n && offset + n == s_received
        && memcmp(&s_stage[offset], p, n) == 0) {
        s_last_error = FWU_OK;
        return FWU_OK;
    }
    return fail_cmd(FWU_E_OFFSET);
}

fwu_err_t fwu_end(uint32_t *version) {
    if (s_state == FWU_VERIFIED || s_state == FWU_ARMED) {     // a retry
        if (version) *version = s_version;
        s_last_error = FWU_OK;
        return FWU_OK;
    }
    if (s_state != FWU_RECEIVING) return fail_cmd(FWU_E_STATE);
    if (s_received != s_size) return fail_cmd(FWU_E_INCOMPLETE);

    uint8_t d[SHA256_BYTES];
    sha256_t c = s_sha;
    sha256_final(&c, d);
    fwu_image_t img;
    memset(&img, 0, sizeof(img));
    fwu_err_t e = FWU_OK;
    if (memcmp(d, s_digest, SHA256_BYTES) != 0)                   e = FWU_E_DIGEST;
    else if (!fwu_parse_image(s_stage, s_size, &img) || !img.exe_arm_s_rp2350) e = FWU_E_IMAGE;
    else if (!img.tbyb)                                           e = FWU_E_NOT_TBYB;
    if (e != FWU_OK) {
        s_state = FWU_IDLE;
        return fail_cmd(e);
    }
    s_version = img.version;
    s_state = FWU_VERIFIED;
    s_last_error = FWU_OK;
    if (version) *version = s_version;
    return FWU_OK;
}

fwu_err_t fwu_apply(void) {
    if (s_state == FWU_ARMED) { s_last_error = FWU_OK; return FWU_OK; }   // a retry
    if (s_state != FWU_VERIFIED) return fail_cmd(FWU_E_STATE);
    // The board must be running the Pi that asks: the install needs that Pi
    // shut down by the board's own orderly sequence.
    if (power_fsm_state() != PS_RUNNING) return fail_cmd(FWU_E_POWER);
    const fwu_layout_t *L = fwu_port_layout();
    if (!layout_ok(L)) return fail_cmd(FWU_E_NO_SLOT);

    // From here the board owes the Pi its power back, whatever happens next: the
    // note says so before the shutdown starts, and outlives any warm reset
    // (review 2026-10-20, R3-F2).
    s_note.phase          = NOTE_ARMED;
    s_note.target_start   = L->other_start;
    s_note.target_version = s_version;
    s_note.from_start     = L->run_start;
    s_note.from_version   = PITRAC_FW_VERSION;
    s_note.result         = FWU_RESULT_NONE;
    s_note.detail         = 0u;
    s_note.repower        = 1u;
    note_commit();
    s_seen_standby = false;      // live once the board is back in STANDBY
    s_asked = false;

    s_state = FWU_ARMED;
    s_t_armed = now_ms();
    s_last_error = FWU_OK;
    power_request_shutdown();
    return FWU_OK;
}

fwu_err_t fwu_abort(void) {
    // An update stopped after UPD_APPLY is settled as cut short, and nothing
    // powers the Pi on behind the operator's back: `upd abort` cancels a pending
    // power-on (fw_update.h, "THE PI'S POWER"). One commit for both.
    if (s_note_ok && (s_note.phase == NOTE_ARMED || s_note.repower != 0u)) {
        if (s_note.phase == NOTE_ARMED) {
            s_note.phase  = NOTE_DONE;
            s_note.result = FWU_RESULT_INSTALL_FAILED;
            s_note.detail = FWU_E_INTERRUPTED;
        }
        s_note.repower = 0u;
        note_commit();
    }
    s_state = FWU_IDLE;
    s_size = s_received = s_last_n = 0u;
    s_version = 0u;
    s_last_error = FWU_OK;
    return FWU_OK;
}

// ---------------------------------------------------------------------------
// The install
// ---------------------------------------------------------------------------

// The same quiet as `cfg save`, and stricter: STANDBY only, the latch read back
// open, and a moment in STANDBY first. No Pi can be powered.
static bool install_allowed(void) {
    return power_fsm_state() == PS_STANDBY
        && !gpio_get_out_level(PIN_LATCH_CONTROL)
        && power_state_elapsed_ms() >= FWU_INSTALL_QUIET_MS
        && cfg_save_blocked_reason() == NULL;
}

// Does the sector read back exactly as `want`? Unreadable counts as no.
static bool sector_is(uint32_t offs, const uint8_t *want) {
    const uint8_t *f = fwu_port_read(offs);
    return f && memcmp(f, want, FWU_SECTOR) == 0;
}

// Does every byte of the sector read back as `v`? Unreadable counts as no.
static bool sector_filled(uint32_t offs, uint8_t v) {
    const uint8_t *f = fwu_port_read(offs);
    if (!f) return false;
    for (uint32_t i = 0; i < FWU_SECTOR; i++) if (f[i] != v) return false;
    return true;
}

// Leave nothing in the partner's first sector from which the boot ROM could
// start a block, and prove it from what is really there: every byte 0x00 after
// the zeros, or every byte 0xFF after an erase. Neither holds a start marker
// (0xffffded3), and the ROM looks for a partition's first block only in these
// 4 KB; with none there it reads no further. "fwu_parse_image() finds no
// IMAGE_DEF" would NOT be proof: that parser wants a whole block inside 4 KB,
// while the ROM finishes a block that only STARTS here, reading on into the next
// sector (review 2026-10-20, R4-F1). Nor are the programs' or the erase's own
// results trusted, either way: only the bytes read back count.
//
// Zeros first: programming only takes bits to 0, so it needs no erase and works
// on whatever a faulty write left. Then the erase, if any byte is not zero.
static bool partner_clear(uint32_t base) {
    for (uint32_t p = 0; p < FWU_SECTOR; p += FWU_PAGE)
        (void)fwu_port_program(base + p, s_zero_page, FWU_PAGE);
    if (sector_filled(base, 0x00u)) return true;
    (void)fwu_port_erase(base);
    return sector_filled(base, 0xFFu);
}

// The install is over without an update: settled in the note, the image still
// staged for another UPD_APPLY. The Pi's power stays owed: the board shut it down
// for this.
static void install_end(fwu_err_t e) {
    s_state = FWU_VERIFIED;
    s_last_error = e;
    note_done(FWU_RESULT_INSTALL_FAILED, (uint32_t)e);
    switch (e) {
    case FWU_E_CLEANUP:
        fwu_port_log("the other partition could NOT be cleared and may hold an image a reset "
                     "would launch WITHOUT a trial: reflash this board over USB (bootsel, which "
                     "launches no image) before any other reset or power cycle");
        break;
    case FWU_E_REBOOT:
        fwu_port_log("the boot ROM refused the update reboot; the other partition was cleared, "
                     "and this image stays the one that boots");
        break;
    case FWU_E_FLASH:
        fwu_port_log("a sector did not read back as written; the other partition was cleared, "
                     "nothing was installed, and this image stays the one that boots");
        break;
    case FWU_E_INTERRUPTED:
        fwu_port_log("the board left STANDBY: the install stopped, with no new image in the "
                     "other partition");
        break;
    default:
        fwu_port_log("no partition to install into: the install stopped");
        break;
    }
}

// Every install step that fails, fails here: whatever it wrote, the partner's
// first sector is cleared and that verified before the old image is said to stay
// (review 2026-10-20, R3-F1); FWU_E_CLEANUP if it cannot be.
static void install_failed(uint32_t base, fwu_err_t e) {
    install_end(partner_clear(base) ? e : FWU_E_CLEANUP);
}

// One step per pass. Sector 0 holds the IMAGE_DEF, so it is erased FIRST and
// written LAST. fw_update.h, "WHAT THE OTHER PARTITION CAN HOLD", says what that
// guarantees and what it does not.
static void install_step(void) {
    const fwu_layout_t *L = fwu_port_layout();
    if (!layout_ok(L) || sectors(s_size) * FWU_SECTOR > L->other_size) {
        // Never in practice: UPD_BEGIN and UPD_APPLY checked both. Nothing is
        // written here, and with no partner there is nothing to clear.
        install_end(FWU_E_NO_SLOT);
        return;
    }
    const uint32_t n = sectors(s_size);
    const uint32_t base = L->other_start;

    if (s_step == 0u) {
        // An erase only takes bits to 1, so it cannot clear a TBYB flag; the
        // read-back, every byte 0xFF, proves there is no block start left.
        if (!fwu_port_erase(base) || !sector_filled(base, 0xFFu)) { install_failed(base, FWU_E_FLASH); return; }
    } else if (s_step < n) {
        // Sector 0 is erased: with no block start in its 4 KB the boot ROM
        // reads no further, so nothing here can make the partition bootable.
        const uint32_t offs = base + s_step * FWU_SECTOR;
        const uint8_t *src = &s_stage[s_step * FWU_SECTOR];
        if (!fwu_port_erase(offs) || !fwu_port_program(offs, src, FWU_SECTOR) || !sector_is(offs, src)) {
            install_failed(base, FWU_E_FLASH);
            return;
        }
    } else {
        // A failed write here may leave a valid image a normal boot WOULD launch
        // (a cleared TBYB bit), so the read-back alone settles nothing.
        if (!fwu_port_program(base, s_stage, FWU_SECTOR) || !sector_is(base, s_stage)) {
            install_failed(base, FWU_E_FLASH);
            return;
        }
        // Read back whole, TBYB flag included: for the boots that follow, a trial.
        s_note.phase = NOTE_TRYING;
        note_commit();
        fwu_port_log("installed and verified; rebooting into it on trial");
        (void)fwu_port_reboot_update(base);
        // Only on failure. The image is TBYB-flagged, so a normal boot would not
        // launch it; it is cleared anyway, so a failed install leaves nothing.
        install_failed(base, FWU_E_REBOOT);
        return;
    }
    s_step++;
}

void fwu_service(void) {
    switch (s_state) {
    case FWU_ARMED:
        if (install_allowed()) {
            s_state = FWU_INSTALLING;
            s_step = 0u;
            s_last_error = FWU_OK;
            fwu_port_log("the Pi is off: installing into the other partition");
        } else if (now_ms() - s_t_armed > FWU_ARM_TIMEOUT_MS) {
            // Never in STANDBY: the Pi never went down, so nothing is owed to it.
            // In STANDBY with the gate shut it did, and it is powered again.
            if (!s_seen_standby && power_fsm_state() != PS_STANDBY) s_note.repower = 0u;
            s_state = FWU_VERIFIED;
            s_last_error = FWU_E_ARM_TIMEOUT;
            note_done(FWU_RESULT_INSTALL_FAILED, FWU_E_ARM_TIMEOUT);
        }
        break;
    case FWU_INSTALLING:
        if (!install_allowed()) {
            // Someone powered the board up, or the guard closed: no flash write
            // now. Sector 0 is erased, or step 0 has not run (the partner as it
            // was), and the last step runs whole within one pass, so there is no
            // new image there. Nothing installs behind anyone's back later:
            // UPD_APPLY again to retry.
            install_end(FWU_E_INTERRUPTED);
            break;
        }
        install_step();
        break;
    default:
        break;
    }

    // The Pi's power after an update (fw_update.h, "THE PI'S POWER"): live once
    // the board is in STANDBY, asked for after FWU_REPOWER_DELAY_MS (never during
    // an install), and done when the FSM has left STANDBY. A refused request is
    // made again next pass; an accepted one is not: power_fsm_step(), first in
    // the next pass, acts on it before anything else can. From the superloop
    // only: main() disarmed the trial watchdog before it.
    if (repower_pending()) {
        if (power_fsm_state() == PS_STANDBY) {
            s_seen_standby = true;
            if (!s_asked && s_state != FWU_ARMED && s_state != FWU_INSTALLING
                && power_state_elapsed_ms() >= FWU_REPOWER_DELAY_MS && power_request_on()) {
                s_asked = true;
                fwu_port_log("powering the Pi back on");
            }
        } else if (s_seen_standby) {
            s_note.repower = 0u;
            note_commit();
        }
    }
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

// What a trial image must show before it buys itself: it reaches the end of
// init, its config sectors are clear of it, and the ADC ring that the power FSM's
// supply monitor needs delivers fresh +5V_IN samples.
static fwu_trial_t trial_check(void) {
    if (cfg_image_overlaps()) return FWU_TRIAL_LAYOUT;
    float v = 0.0f;
    if (adc_5vin_read(&v) != ADC_5VIN_FRESH) return FWU_TRIAL_ADC;
    return FWU_TRIAL_OK;
}

void fwu_boot_check(void) {
    note_load();
    // Any reset opened the latch (R12): the Pi is off, so a power-on the note
    // owes it is live from the start (fwu_service()).
    s_seen_standby = true;
    const fwu_layout_t *L = fwu_port_layout();

    s_trial = L->buy_pending;
    if (s_trial) {
        fwu_trial_t why = trial_check();
        if (why == FWU_TRIAL_OK) {
            s_buy_rc = fwu_port_buy();
            if (s_buy_rc != 0) why = FWU_TRIAL_BUY_FAILED;
        }
        if (why != FWU_TRIAL_OK) {
            // Not bought, so not bootable: a normal boot runs the previous image,
            // which reads this and reports the rollback.
            if (s_note_ok && s_note.phase == NOTE_TRYING) {
                s_note.phase = NOTE_TRIAL_FAILED;
                s_note.detail = (uint32_t)why;
                note_commit();
            }
            fwu_port_log("this image failed its trial and will not buy itself; rebooting into the previous one");
            fwu_port_reboot_normal();
            return;
        }
        s_bought = true;
    }

    // Settle an update the note left open. Settling is not the Pi's power:
    // that obligation stays in the note until it is met (fwu_service()).
    if (!s_note_ok) return;
    const bool mine = L->run_start == s_note.target_start && PITRAC_FW_VERSION == s_note.target_version;
    if (s_note.phase != NOTE_DONE) {
        if (mine) {
            // TRYING: written TBYB-flagged and read back, then launched on trial;
            // a normal boot runs it only once it has bought itself, at this boot
            // or at one that reset before settling. Otherwise the update reboot
            // was never asked for: a normal boot launched it with no trial at all
            // (fw_update.h, "WHAT THE OTHER PARTITION CAN HOLD").
            const bool on_trial = s_note.phase == NOTE_TRYING;
            note_done(FWU_RESULT_INSTALLED, on_trial ? FWU_INSTALLED_ON_TRIAL : FWU_INSTALLED_NO_TRIAL);
            fwu_port_log(on_trial ? "the new image runs and has bought itself"
                                  : "the new image runs, launched WITHOUT a trial: no rollback was in place");
        } else if (s_note.phase == NOTE_ARMED) {
            // A reset during the shutdown or the install: it never finished.
            note_done(FWU_RESULT_INSTALL_FAILED, FWU_E_INTERRUPTED);
            fwu_port_log("the update was cut short by a reset; this image never stopped being the one that boots");
        } else {
            note_done(FWU_RESULT_ROLLED_BACK,
                      s_note.phase == NOTE_TRIAL_FAILED ? s_note.detail : (uint32_t)FWU_TRIAL_OK);
            fwu_port_log("ROLLED BACK: the new image never bought itself; the previous one runs again");
        }
        s_settled_here = true;
    } else if (mine && s_note.result == FWU_RESULT_INSTALL_FAILED) {
        // Reported failed, yet its image runs: a partner the cleanup could not
        // clear (FWU_E_CLEANUP), launched by a normal boot without a trial.
        note_done(FWU_RESULT_INSTALLED, FWU_INSTALLED_NO_TRIAL);
        fwu_port_log("the new image runs, launched WITHOUT a trial: no rollback was in place");
        s_settled_here = true;
    }
}

// ---------------------------------------------------------------------------
void fwu_status(fwu_status_t *out) {
    memset(out, 0, sizeof(*out));
    out->state          = s_state;
    out->last_error     = s_last_error;
    out->size           = s_size;
    out->received       = s_received;
    out->staged_version = s_version;
    out->install_step   = s_step;
    out->install_steps  = s_size ? sectors(s_size) + 1u : 0u;
    out->armed_for_ms   = s_state == FWU_ARMED ? now_ms() - s_t_armed : 0u;
    out->trial          = s_trial;
    out->bought         = s_bought;
    out->buy_rc         = s_buy_rc;
    out->repower_pending = repower_pending();
    if (s_note_ok && s_note.phase == NOTE_DONE) {
        out->result           = (fwu_result_t)s_note.result;
        out->result_from      = s_note.from_version;
        out->result_to        = s_note.target_version;
        out->result_detail    = s_note.detail;
        out->result_this_boot = s_settled_here;
    }
}

uint8_t *fwu_scratch(size_t *size) {
    if (s_state != FWU_IDLE || s_size != 0u) return NULL;
    if (size) *size = sizeof(s_stage);
    return s_stage;
}

const char *fwu_state_name(fwu_state_t s) {
    static const char *const k[FWU__STATES] = {
        [FWU_IDLE] = "idle", [FWU_RECEIVING] = "receiving", [FWU_VERIFIED] = "verified",
        [FWU_ARMED] = "armed", [FWU_INSTALLING] = "installing",
    };
    return (unsigned)s < FWU__STATES ? k[s] : "?";
}

const char *fwu_err_name(fwu_err_t e) {
    static const char *const k[FWU_E__COUNT] = {
        [FWU_OK] = "ok", [FWU_E_STATE] = "STATE", [FWU_E_NO_SLOT] = "NO_SLOT",
        [FWU_E_SIZE] = "SIZE", [FWU_E_OFFSET] = "OFFSET", [FWU_E_LENGTH] = "LENGTH",
        [FWU_E_INCOMPLETE] = "INCOMPLETE", [FWU_E_DIGEST] = "DIGEST", [FWU_E_IMAGE] = "IMAGE",
        [FWU_E_NOT_TBYB] = "NOT_TBYB", [FWU_E_POWER] = "POWER", [FWU_E_ARM_TIMEOUT] = "ARM_TIMEOUT",
        [FWU_E_INTERRUPTED] = "INTERRUPTED", [FWU_E_FLASH] = "FLASH", [FWU_E_REBOOT] = "REBOOT",
        [FWU_E_CLEANUP] = "CLEANUP",
    };
    return (unsigned)e < FWU_E__COUNT ? k[e] : "?";
}

const char *fwu_result_name(fwu_result_t r) {
    switch (r) {
        case FWU_RESULT_NONE:           return "none";
        case FWU_RESULT_INSTALLED:      return "installed";
        case FWU_RESULT_ROLLED_BACK:    return "ROLLED BACK";
        case FWU_RESULT_INSTALL_FAILED: return "INSTALL FAILED";
    }
    return "?";
}

const char *fwu_trial_name(fwu_trial_t t) {
    switch (t) {
        case FWU_TRIAL_OK:         return "none recorded";
        case FWU_TRIAL_LAYOUT:     return "the image reaches into the config sectors";
        case FWU_TRIAL_ADC:        return "+5V_IN could not be read fresh";
        case FWU_TRIAL_BUY_FAILED: return "the boot ROM's explicit buy failed";
        case FWU_TRIAL__COUNT:     break;
    }
    return "?";
}

const char *fwu_detail_name(fwu_result_t r, uint32_t d) {
    switch (r) {
    case FWU_RESULT_INSTALLED:
        return d == FWU_INSTALLED_ON_TRIAL ? "it ran on trial and bought itself"
             : "it was launched WITHOUT a trial: no rollback was in place";
    case FWU_RESULT_ROLLED_BACK:
        return d == FWU_TRIAL_OK ? "the new image never bought itself (it crashed, hung or did not start)"
                                 : fwu_trial_name((fwu_trial_t)d);
    case FWU_RESULT_INSTALL_FAILED:
        switch ((fwu_err_t)d) {
        case FWU_E_INTERRUPTED: return "cut short: the board left STANDBY, or reset";
        case FWU_E_ARM_TIMEOUT: return "the Pi was not powered down in time";
        case FWU_E_FLASH:       return "a flash sector did not read back as written";
        case FWU_E_REBOOT:      return "the boot ROM refused the update reboot";
        case FWU_E_CLEANUP:     return "the other partition could NOT be cleared: reflash over USB first";
        default:                return fwu_err_name((fwu_err_t)d);
        }
    case FWU_RESULT_NONE:
        break;
    }
    return "";
}

#if !(defined(PICO_ON_DEVICE) && PICO_ON_DEVICE)
void fwu_test_power_on(void) {
    memset(s_notes, 0x5A, sizeof(s_notes));
    fwu_test_reboot();
}

void fwu_test_reboot(void) {
    s_state = FWU_IDLE;
    s_last_error = FWU_OK;
    s_size = s_received = s_last_n = s_version = s_t_armed = s_step = 0u;
    memset(s_digest, 0, sizeof(s_digest));
    s_trial = s_bought = s_settled_here = s_seen_standby = s_asked = false;
    s_buy_rc = 0;
    memset(&s_note, 0, sizeof(s_note));
    s_note_ok = false;
    s_note_next = 0u;
}

void fwu_test_note_flip(bool newest, unsigned word, uint32_t mask) {
    int k = note_newest();
    if (k < 0) k = 0;
    if (!newest) k ^= 1;
    if (word < NOTE_WORDS) ((uint32_t *)&s_notes[k])[word] ^= mask;
}
#endif
