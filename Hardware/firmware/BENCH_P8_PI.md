# Phase 8 — Real Pi 5 integration and clean-shutdown acceptance

**Prereq:** Phases 1b (including the **Q10 rework**) and 2–7 passed.
**Power:** PSU 5.2 V, **current limit ≥ 5 A** — a Pi 5 alone can pull 3 A in bursts.
**Gear:** scope (2 channels minimum), DMM, a Pi 5 with a **backed-up** SD card.

This is the first time a Pi 5 is ever seated on J8. Everything before this point
exists to make this step boring.

---

## 8.0 — The gate. Do not seat a Pi until every line is ticked.

| | Check | Where |
|---|---|---|
| ☐ | Phase 1b full test matrix passed against a simulated Pi | `BENCH.md` Phase 1b |
| ☐ | Firmware built **after 2026-07-31** — you need the `reset`/`bootsel` guard | `PROGRESS.md` §9 |
| ☐ | **Q6 resolved** — Mira220 1.8 V I/O question, if cameras are connected | `PROGRESS.md` Q6 |
| ☐ | SD card imaged and the image verified restorable | — |
| ☐ | `PI_SHUTDOWN_ACTIVE_LOW` still 1, `gpio-shutdown` overlay params written to the Pi | 8.1 below |
| ○ | *Optional:* Q10 10 kΩ pull-up fitted. Defence-in-depth — **not a gate** | `BENCH.md` Q10 |

> The latch **is** the Pi's power switch. A firmware bug here corrupts a
> filesystem. That is the entire reason phases 1b and 8 are separate.

### 🔴 The one thing to internalise before you start

**An RP2354 reset is a hard power cut to the Pi, not a reboot.** Net 62 `LATCH_CONTROL` is
Q2's gate + R12 + GPIO15: on any reset the pad goes high-Z, R12 pulls the gate low, and the
+5 V rail opens. The Pi loses power instantly — no shutdown request, no sync, no warning.

This applies to **every** reset source:

| Source | Mitigated? |
|---|---|
| `reset` / `bootsel` over the CLI | ✅ Guarded — refuses while a Pi is powered. `force` overrides. |
| **SW2 (RUN button)** | ❌ **Operator discipline only. Do not press it with a Pi up.** |
| Hardware watchdog | ⚠ Policy: reset action enabled only while the Pi is down. Honour it. |
| Brownout on +5V_IN | ❌ Unavoidable, and it takes +3V3 down too |

It is a deliberate tradeoff, not a defect: R12's pull-down is what makes the latch
**fail-safe**, so the rail opens if the MCU dies — the right call on a board driving 9 A
strobe pulses. You cannot also have "the Pi survives an MCU reset" without a supervisor or
hold-up that would weaken exactly that property.

**Practical consequence: put a piece of tape over SW2 while a Pi is seated.**

---

## 8.1 — Overlay parameters and polarity (Q7)

On the Pi, in `/boot/firmware/config.txt`:

```
dtoverlay=gpio-shutdown,gpio_pin=26,active_low=1,gpio_pull=up
```

Firmware committed to **active-low** in `board.h` (`PI_SHUTDOWN_ACTIVE_LOW 1`)
because it matches this overlay default. The parameters above are written
explicitly rather than relying on defaults, so a future Raspberry Pi OS change
cannot silently invert the contract.

**Verify before trusting it:**

```bash
# On the Pi:
gpioinfo | grep -i 26          # should show the line consumed by gpio-shutdown
journalctl -b | grep -i shutdown
```

| Check | Pass |
|---|---|
| Overlay loaded, GPIO26 claimed | `gpioinfo` shows the line in use |
| From `RUNNING`, press the panel button | Pi begins an orderly halt within ~1 s |
| Scope J8.37 during the request | **200 ms low pulse**, already confirmed in Phase 1b |

**Q7 closes here.**

---

## 8.2 — Does the Pi's header 3V3 drop at halt? (Q4)

The FSM's "Pi is down" indicator is `!PI_3V3_SENSE || !RPI5_ON`. Which of the two
actually fires decides whether the design is resting on a real signal or on a
fallback.

```bash
sudo halt
```

Then, with the board still latched, DMM on **J8.1**:

| Reading after halt | Meaning |
|---|---|
| **~0 V** | The header 3V3 rail drops. `PI_3V3_SENSE` is a real primary indicator. |
| **~3.3 V** | The PMIC holds it up. `PI_3V3_SENSE` never fires; **RPI5_ON is doing all the work.** |

Record the answer in `PROGRESS.md` §6 and close Q4. If 3V3 stays up, say so
loudly in `board.h` — the OR in `power_pi_is_down()` stops being belt-and-braces
and becomes the only thing that works.

**Also record the shutdown duration**: time from the button press to the
down-indicator firing. `PI_SHUTDOWN_MIN_HOLDOFF_MS` should be about **2×** that,
and it is currently 15 s on an assumption, not a measurement.

---

## 8.3 — Pi 5 +5V → header 3V3 latency (Q11) — **new, and it sets a constant**

This is the measurement that `PI_DETECT_WINDOW_MS` was invented for and has never
been taken. Until it is, that constant is a guess.

**Why it matters:** `POWERING_ON` looks for a Pi for `PI_DETECT_WINDOW_MS` (3 s)
after the latch closes. If a real Pi takes longer than that to raise its header
3V3, the board concludes "no Pi" and drops into `BENCH_RUNNING`, where a button
press is a hard `FORCE_OFF` — a power cut on a booting Pi. The late-detect
promotion in `BENCH_RUNNING` is a backstop for exactly this, but the window
should be right on its own.

**Setup:**

| Scope ch | Point | Signal |
|---|---|---|
| 1 | **J8.2** | +5 V switched rail (the latch closing) |
| 2 | **J8.1** | Pi header 3V3 |

Trigger on **ch1 rising**. Press the panel button. Single-shot capture, ~5 s of
timebase.

**Measure:** time from ch1 crossing 4.5 V to **ch2 crossing 2.54 V**.

> The 2.54 V threshold is not arbitrary. `PI_3V3_SENSE` is GPIO24 behind the
> R45/R44 divider (×0.909), and the RP2350 needs ~2.31 V (0.7 × VDD) to read a
> reliable high. 2.31 / 0.909 = **2.54 V at J8.1**. That is the instant the
> firmware can actually see the Pi, which is later than the instant the rail
> starts moving.

| Measured latency | Action |
|---|---|
| **< 1 s** | 3 s window is comfortable. Leave it. |
| **1–2 s** | Leave it, but record the margin. |
| **> 2 s** | **Raise `PI_DETECT_WINDOW_MS` to ~3× the measurement.** |

Record in `PROGRESS.md` §6 and close Q11. Repeat from a **cold** board (rails
down for a minute) — a warm restart can be faster and would flatter the number.

---

## 8.4 — The reset guard, and Q10 if you fitted it

> ⚠ **Do not press SW2 as a test here.** It cuts the Pi's power (see the box in
> §8.0). There is nothing to learn from doing it deliberately that is worth an
> unclean shutdown. This section tests the *guard*, not the reset.

### The CLI guard (the mitigation that matters)

With a Pi up and running, from the board's CLI:

| Command | Pass |
|---|---|
| `reset` | **REFUSED**, with the latch explanation. Pi keeps running. |
| `bootsel` | **REFUSED** likewise |
| `stat` | still shows `RUNNING`, latch 1 |
| `off`, wait for the latch to drop, then `reset` | **Allowed** — no Pi powered, so no guard |

The `force` variants exist deliberately and are not tested here; just know they
bypass the guard and will cut the Pi.

### Q10, only if you fitted the pull-up

Skip this if you didn't — it is defence-in-depth and the latch drop dominates
regardless.

```bash
# On the Pi, in one terminal:
journalctl -f | grep -iE 'power|shutdown|halt|key'
```

With the Pi up, scope J8.37 and confirm it sits **above 2.3 V** at idle and
during a `reset force` (which you may not want to run at all). The meaningful
check is simply that the pull-up holds the line high whenever firmware is not
actively asserting — a static DMM reading answers it.

---

## 8.5 — Clean-shutdown acceptance

The actual acceptance test for the whole power subsystem.

```bash
# On the Pi, before starting:
sudo touch /forcefsck
sudo dmesg -w | grep -iE 'ext4|error|corrupt'
```

Run **20 full cycles**: button on → wait for full boot → button off → wait for
the latch to drop → repeat.

| Check | Pass |
|---|---|
| Every cycle reaches `RUNNING` | 20/20 |
| Every shutdown completes before `PI_SHUTDOWN_MAX_WAIT_MS` | no `PI_SHUTDOWN_TIMEOUT` faults |
| Latch never drops before `PI_SHUTDOWN_MIN_HOLDOFF_MS` | scope or `stat` |
| No filesystem errors after any cycle | `dmesg`, `journalctl -p err` |
| `fsck` clean at the end | — |

Twenty is not arbitrary — a 1-in-20 corruption rate is the kind of thing that
looks fine in a three-cycle test and destroys a card in a week of real use.

---

## 8.6 — Recording repeated or unrequested Pi halts

**Requirement:** if the Pi 5 halts repeatedly — especially when nobody asked it
to — that must leave a durable trace. A spurious halt is the exact symptom of a
Q10-class fault, and it is precisely the kind of intermittent problem that is
invisible without a counter.

### Two witnesses, and neither is sufficient alone

| Witness | Knows | Blind to |
|---|---|---|
| **RP2354** | Whether *it* asserted RPI5_SHUTDOWN — i.e. **intent** | What userspace did about it |
| **Pi journald** | The halt, its cause, the systemd chain | Whether the board ever asked |

A halt the board did **not** request is the smoking gun, and only the RP2354 can
identify one. Correlating the two is what turns "the Pi keeps rebooting" into a
diagnosis.

### Where it should live: the RP2354

It is the authoritative witness, because it is the only one that knows intent —
and because it **survives the failure**. If the SD card is the casualty, the Pi's
own log is the least trustworthy record of what happened to it.

**Ride this on the flash config block already scheduled for Phase 3**
(`PROGRESS.md` §9: versioned, CRC'd, writer running from RAM with interrupts off
and core 1 parked, because the chip executes XIP from that same flash). The
counters are a handful of words; the hard part is the flash-write machinery,
which is being built anyway for `demod_phase_ticks`.

Proposed counters:

| Field | Meaning |
|---|---|
| `boot_count` | RP2354 resets. Correlates a rise here with the Q10 hazard. |
| `pi_shutdown_requested` | Times we asserted RPI5_SHUTDOWN |
| `pi_shutdown_clean` | Pi indicated down before `PI_SHUTDOWN_MAX_WAIT_MS` |
| `pi_shutdown_timeout` | `FAULT_PI_SHUTDOWN_TIMEOUT` raised |
| **`pi_down_unrequested`** | **Pi went down while in `PS_RUNNING` with no assertion from us. The one that matters.** |
| `last_fault`, `last_fault_uptime_ms` | Context for the above |

Expose over the CLI (extend `stat`, or a `hist` command) so a field unit can be
interrogated without a debugger.

### ⚠ Prerequisite nobody has built yet

**`PS_RUNNING` does not currently watch for the Pi going down.** It only looks
for a button press or a shutdown request:

```c
case PS_RUNNING:
    if (s_req_shutdown || take_short_press()) { ... begin_shutdown(); }
    break;
```

So if the Pi halts on its own, the FSM sits in `RUNNING` indefinitely with the
rail up and never notices. **`pi_down_unrequested` cannot be counted until that
detection exists** — it has to be added before the counter means anything.

What the board should *do* about it is a separate decision, and it interacts with
Q4: if the header 3V3 does not drop at halt, detection rests entirely on
`RPI5_ON`. Log it and stay up is the safe default; dropping the rail is arguably
more correct (the Pi is down, there is nothing to power) but should not be
implemented until 8.2 says which signal is trustworthy.

### Pi side

Cheap, and worth doing regardless — persistent journald:

```bash
sudo mkdir -p /var/log/journal
sudo systemd-tmpfiles --create --prefix /var/log/journal
# then: journalctl --list-boots   to see halts across power cycles
```

That gives boot-to-boot history surviving power cycles, which is enough to spot a
pattern even before the RP2354 counters exist.

---

## Exit criteria for Phase 8

- [ ] 8.0 gate fully ticked before the Pi was seated
- [ ] **Q7 closed** — overlay params verified, 200 ms pulse confirmed against a real Pi
- [ ] **Q4 closed** — header 3V3 behaviour at halt recorded, primary indicator identified
- [ ] **Q11 closed** — +5V→3V3 latency measured, `PI_DETECT_WINDOW_MS` set from it
- [ ] **Reset guard verified** — `reset` and `bootsel` both refuse while the Pi is powered
- [ ] Shutdown duration measured; `PI_SHUTDOWN_MIN_HOLDOFF_MS` retuned to ~2×
- [ ] 20 clean power cycles, no filesystem errors
- [ ] Halt-telemetry design decided and recorded (8.6)

Record everything in `PROGRESS.md` §6.
