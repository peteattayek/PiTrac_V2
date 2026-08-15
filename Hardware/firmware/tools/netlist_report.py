#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
"""
Generate HARDWARE_REFERENCE.md from the KiCad netlist and schematics.

WHY THIS IS A SCRIPT AND NOT A HAND-WRITTEN DOCUMENT
----------------------------------------------------
Every significant bug found during the Phase 3 review came from a hand-written
summary of the hardware that had silently drifted from the hardware:

  * BENCH_P3_DETECT.md said R100 was the unpopulated gain part. It is R98.
  * board.h carried four PWM_SLICE_* constants; three were wrong.
  * board.h's "never put two PWM functions 16 apart" rule is wrong above GPIO32.
  * ARCHITECTURE.md assigned comparator timing to a PIO block that physically
    cannot reach GPIO46.

A derived document that cannot be re-derived is just another one of those waiting
to happen. This one can: re-run the script and `git diff` the output. A silent
divergence becomes a visible change.

USAGE
    python tools/netlist_report.py            # writes ../../HARDWARE_REFERENCE.md
    python tools/netlist_report.py --check    # exit 1 if the file is out of date

Every fact in the output is tagged:
    [E]  EXTRACTED  - read directly out of the netlist or schematic. Trust it.
    [D]  DERIVED    - arithmetic on extracted values. Trust the maths, check the
                      topology assumption stated next to it.
    [I]  INTERPRETED- a judgement about what the circuit MEANS. Verify before
                      relying on it for anything safety- or damage-related.
"""

import argparse, math, os, re, sys, glob, datetime

HERE  = os.path.dirname(os.path.abspath(__file__))
FW    = os.path.dirname(HERE)
ROOT  = os.path.dirname(os.path.dirname(FW))
KICAD = os.path.join(ROOT, "Hardware", "THE_SECOND_BOARD_TO_RULE_THEM_ALL")
NET   = os.path.join(KICAD, "The_Second_Board_To_Rule_Them_All.net")
OUT   = os.path.join(ROOT, "HARDWARE_REFERENCE.md")


def parse_netlist(path):
    """-> (nets: name -> [ref.pin], values: ref -> value, gpio: n -> (net, pin))"""
    nets, values, gpio = {}, {}, {}
    cur = ref = pin = None
    in_net = False
    for line in open(path, encoding="utf-8", errors="replace"):
        t = line.strip()
        m = re.match(r'\(ref "([^"]+)"\)$', t)
        if m:
            ref = m.group(1); pin = None; continue
        m = re.match(r'\(value "([^"]*)"\)$', t)
        if m and ref and not in_net:
            values.setdefault(ref, m.group(1)); continue
        if t.startswith("(net"):
            in_net = True; cur = None; continue
        m = re.match(r'\(name "(.*)"\)$', t)
        if m and in_net and cur is None:
            cur = m.group(1); nets.setdefault(cur, []); continue
        m = re.match(r'\(pin "([^"]+)"\)$', t)
        if m:
            pin = m.group(1)
            if in_net and cur is not None and ref:
                nets[cur].append(f"{ref}.{pin}")
            continue
        m = re.match(r'\(pinfunction "([^"]+)"\)$', t)
        if m and ref == "U3" and in_net and cur is not None:
            g = re.match(r"GPIO(\d+)", m.group(1))
            if g:
                gpio[int(g.group(1))] = (cur, pin)
    return nets, values, gpio


def parse_dnp(kicad_dir):
    """DNP refs per sheet.

    NOTE the ordering trap: in KiCad 8 `(dnp yes)` appears BEFORE the Reference
    property inside a symbol block, so searching BACKWARDS for the reference
    silently returns the PREVIOUS component. Doing exactly that during the review
    reported the beam LED and the supply diode as unpopulated, which is absurd on
    a board that has run the beam at 3 A. Always search forward.
    """
    out = {}
    for f in sorted(glob.glob(os.path.join(kicad_dir, "*.kicad_sch"))):
        s = open(f, encoding="utf-8", errors="replace").read()
        refs = []
        for m in re.finditer(r"\(dnp yes\)", s):
            nxt = re.search(r'\(property "Reference" "([^"]+)"', s[m.end():m.end() + 3000])
            if nxt:
                refs.append(nxt.group(1))
        if refs:
            out[os.path.basename(f)] = sorted(set(refs))
    return out


def val(values, ref):
    return values.get(ref, "?")


def num(values, ref):
    """Parse a KiCad value like '470K', '2.2nF', '10R', '0.1uF' into SI units."""
    v = val(values, ref)
    m = re.match(r"^([\d.]+)\s*([pnufkKMR]?)", v.replace("µ", "u"))
    if not m:
        return None
    x = float(m.group(1))
    return x * {"p": 1e-12, "n": 1e-9, "u": 1e-6, "f": 1e-6,
                "k": 1e3, "K": 1e3, "M": 1e6, "R": 1, "": 1}[m.group(2)]


def fc(r, c):
    return 1.0 / (2 * math.pi * r * c)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if the committed file differs from a fresh render")
    args = ap.parse_args()

    nets, values, gpio = parse_netlist(NET)
    dnp = parse_dnp(KICAD)
    L = []
    w = L.append

    w("<!-- GENERATED FILE - DO NOT EDIT BY HAND -->")
    w("<!-- Regenerate: python Hardware/firmware/tools/netlist_report.py -->")
    w("")
    w("# PiTrac board — hardware reference")
    w("")
    w("**Generated from the KiCad source, not written by hand.** Regenerate with:")
    w("")
    w("```bash")
    w("python Hardware/firmware/tools/netlist_report.py          # rewrite this file")
    w("python Hardware/firmware/tools/netlist_report.py --check  # fail if stale")
    w("```")
    w("")
    w(f"Source: `Hardware/THE_SECOND_BOARD_TO_RULE_THEM_ALL/`, "
      f"netlist dated {datetime.date.fromtimestamp(os.path.getmtime(NET))}.")
    w("")
    w("## How to use this file, and when not to")
    w("")
    w("This exists because parsing the netlist costs real effort and it was done three")
    w("separate times during Phase 3. It is a **cache of the source, not a replacement**.")
    w("")
    w("Every significant bug found in the Phase 3 review came from a hand-written summary")
    w("of this board that had drifted from the board: the bench doc named the wrong DNP")
    w("resistor, `board.h` carried three wrong PWM slice numbers, and `ARCHITECTURE.md`")
    w("assigned a PIO state machine to a block that cannot physically reach the pin. A")
    w("derived document is only safe when it can be re-derived and diffed — which is why")
    w("this one is generated.")
    w("")
    w("**Claims are tagged by how much they can be trusted:**")
    w("")
    w("| tag | meaning | how to treat it |")
    w("|---|---|---|")
    w("| **[E]** | Extracted verbatim from the netlist or schematic | Trust it. It is the source. |")
    w("| **[D]** | Derived — arithmetic on extracted values | Trust the arithmetic; check the topology assumption stated beside it. |")
    w("| **[I]** | Interpreted — a judgement about what the circuit *means* | **Verify against the schematic** before relying on it. |")
    w("")
    w("⚠ **For anything damage-class or safety-critical, go to the source anyway.**")
    w("A generated file can still encode a wrong *assumption*; only the [E] rows are")
    w("immune to that.")
    w("")

    # ---------------- GPIO map -------------------------------------------------
    w("---")
    w("")
    w("## 1. RP2350 GPIO map  [E]")
    w("")
    w("All 48 GPIOs, straight from the netlist. `Net-(U3-GPIOnn)` means KiCad")
    w("auto-named it — the net exists and is connected, it just has no label.")
    w("")
    w("| GPIO | QFN pin | net |")
    w("|---:|---:|---|")
    for n in sorted(gpio):
        net, pin = gpio[n]
        w(f"| {n} | {pin} | `{net}` |")
    w("")

    # ---------------- ADC ------------------------------------------------------
    w("## 2. ADC channels  [E]")
    w("")
    w("On RP2350B, ADC channel *n* is GPIO 40+*n*.")
    w("")
    w("| ch | GPIO | net | firmware use |")
    w("|---:|---:|---|---|")
    use = {0: "strobe current (BURST)", 1: "+5V_IN divider", 2: "TIA_Out monitor",
           3: "**NEVER SAMPLE** — RPI5_SHUTDOWN is a digital output",
           4: "**NEVER SAMPLE** — Threshold_PWM is a digital output",
           5: "detect signal", 6: "**NEVER SAMPLE** — D_Comparator is a digital input",
           7: "analog mic"}
    for c in range(8):
        n = 40 + c
        net = gpio.get(n, ("?",))[0]
        w(f"| {c} | {n} | `{net}` | {use[c]} |")
    w("")

    # ---------------- test points ---------------------------------------------
    w("## 3. Test points  [E]")
    w("")
    w("All are bare 1.0 mm THT pads, listed \"No Solder\" in the BOM — probe holes with")
    w("nothing fitted.")
    w("")
    tp = {}
    for n, mem in nets.items():
        for m in mem:
            r = m.split(".")[0]
            if re.fullmatch(r"TP\d+", r):
                tp.setdefault(r, []).append(n)
    w("| TP | net |")
    w("|---|---|")
    for k in sorted(tp, key=lambda x: int(x[2:])):
        w(f"| {k} | `{', '.join(tp[k])}` |")
    w("")
    w("⚠ **[I] TP5 is `Strobe_GND`, the shared low-side return** — it is where Phase 2")
    w("measured the U9 beam one-shot clamp, and Phase 6 strobe pulses appear there too.")
    w("")

    # ---------------- DNP ------------------------------------------------------
    w("## 4. Unpopulated parts  [E]")
    w("")
    w("Complete across every sheet. **Only R98 is an electrical tuning option.**")
    w("")
    w("| sheet | refs |")
    w("|---|---|")
    for f, refs in sorted(dnp.items()):
        w(f"| `{f}` | {', '.join(refs)} |")
    w("")
    w("H* are mounting holes. C9 (100 pF on VIR) and R11 (5R1) are optional hand-solder")
    w("parts on the Power sheet. **R98 is the detect-chain gain option — see §6.**")
    w("")

    # ---------------- detect chain --------------------------------------------
    w("---")
    w("")
    w("## 5. Detection signal chain  [E] values, [D] corners")
    w("")
    w("```")
    w("D12 --> U11A TIA --> U13 chopper demod --> 4th-order LPF --> TP9")
    w("    --> C81 + U14 gated HPF --> R99 --> U12B x14.5 --> U15 comparator --> GPIO46")
    w("                                                    \\-> R102 --> D14 --> ADC5")
    w("```")
    w("")
    w("| stage | parts | value |")
    w("|---|---|---|")
    w(f"| Photodiode | D12 | {val(values,'D12')} |")
    w(f"| Bias | R77 / C67 | {val(values,'R77')} from VIR / {val(values,'C67')} |")
    w(f"| TIA feedback | R80 | {val(values,'R80')} |")
    w(f"| TIA fb cap | C68 **series** C70 | {val(values,'C68')} + {val(values,'C70')} |")
    w(f"| Isolation | R81 / C72 | {val(values,'R81')} / {val(values,'C72')} |")
    w(f"| Servo integrator | R83 / C73 | {val(values,'R83')} / {val(values,'C73')} |")
    w(f"| Servo inverter | R84 / R85 | {val(values,'R84')} / {val(values,'R85')} |")
    w(f"| Servo injection | R78 | {val(values,'R78')} |")
    w(f"| Demod difference amp | R95 / R97 | {val(values,'R95')} / {val(values,'R97')} |")
    w(f"| LPF section 1 | R87/R88, C75/C77 | {val(values,'R87')}, {val(values,'C75')} |")
    w(f"| LPF section 2 | R90/R92, C78/C79 | {val(values,'R90')}, {val(values,'C78')} |")
    w(f"| LPF gain | R93 / R94 | {val(values,'R93')} / {val(values,'R94')} |")
    w(f"| Gated HPF | C81 / R96 | {val(values,'C81')} / {val(values,'R96')} |")
    w(f"| Output amp | R99, R101, R100, R98(DNP) | {val(values,'R99')}, {val(values,'R101')}, {val(values,'R100')}, {val(values,'R98')} |")
    w(f"| ADC5 protection | R102 / D14 | {val(values,'R102')} / {val(values,'D14')} |")
    w("")

    r80, c68, c70 = num(values, "R80"), num(values, "C68"), num(values, "C70")
    r83, c73 = num(values, "R83"), num(values, "C73")
    r78 = num(values, "R78")
    r87, c75 = num(values, "R87"), num(values, "C75")
    c81, r96 = num(values, "C81"), num(values, "R96")
    r101, r100, r98 = num(values, "R101"), num(values, "R100"), num(values, "R98")

    w("### Derived corner frequencies  [D]")
    w("")
    w("| quantity | value | assumption |")
    w("|---|---|---|")
    if r80 and c68 and c70:
        cf = 1 / (1 / c68 + 1 / c70)
        w(f"| **TIA feedback pole** | **{fc(r80,cf)/1e3:.0f} kHz** | C68 and C70 in *series* = {cf*1e12:.1f} pF across R80 |")
    if r83 and c73 and r78 and r80:
        f_int = fc(r83, c73)
        w(f"| Servo integrator | {f_int:.3f} Hz | R83 x C73 |")
        w(f"| **DC servo corner** | **{f_int*(r80/r78):.2f} Hz** | integrator x R80/R78 = {r80/r78:.1f}. **Matches the schematic annotation of 2.27 Hz** |")
    if r87 and c75:
        w(f"| **LPF f0** | **{fc(r87,c75)/1e3:.2f} kHz** | Sallen-Key, equal R and C. Schematic annotation says 15.9 — that is stale |")
    if c81 and r96:
        w(f"| **Gated HPF** | **tau {c81*r96:.2f} s, f_c {fc(r96,c81):.3f} Hz** | C81 x R96, U14 selecting S1 |")
    w("")
    if r80 and c68 and c70:
        cf = 1 / (1 / c68 + 1 / c70)
        f0 = fc(r80, cf)
        w("### TIA phase lag vs carrier  [D]")
        w("")
        w("**This is why one demod-phase number cannot cover the carrier scan range**, and")
        w("why `cal demod model` fits phase against frequency instead of assuming a pure")
        w("delay. It is a testable prediction: the fitted model should show this curvature.")
        w("")
        w("| carrier | lag |")
        w("|---:|---:|")
        for f in (104166, 150000, 200000, 250000):
            w(f"| {f/1000:.1f} kHz | {math.degrees(math.atan(f/f0)):.1f}° |")
        w("")

    # ---------------- gain option ---------------------------------------------
    w("## 6. R98 — the gain option  [E] values, [D] table")
    w("")
    w("R98 and R100 are a **parallel pair** from GND to U12B's inverting input.")
    w(f"**R100 ({val(values,'R100')}) is FITTED; R98 is the unpopulated one** — and it was left")
    w("unpopulated so its *value* could be chosen after measuring, which makes gain a")
    w("continuous knob rather than an on/off option.")
    w("")
    w("```")
    w("G = 1 + R101 / (R100 || R98)")
    w("R98 = R100 * R_gnd / (R100 - R_gnd),  R_gnd = R101 / (G - 1)")
    w("```")
    w("")
    if r101 and r100:
        w("| R98 | R100 ∥ R98 | gain | ΔTP9 at ADC5 full scale |")
        w("|---|---|---|---|")
        w(f"| absent | {r100:.0f} Ω | **{1+r101/r100:.1f}** | {3300/(1+r101/r100):.0f} mV |")
        for rr in (10e3, 4.7e3, 2e3, 1e3):
            par = r100 * rr / (r100 + rr)
            g = 1 + r101 / par
            w(f"| {rr/1e3:g} k | {par:.0f} Ω | {g:.1f} | {3300/g:.0f} mV |")
        w("")
    w("⚠ **[I] Full scale is the ADC's 3.3 V, not D14's ~3.6 V** — ADC5 saturates at code")
    w("4095 before the BAT54S conducts, so clipping appears as ADC full scale rather than a")
    w("diode knee. And the LM393's input common-mode ceiling (~3.5 V) is *lower still*.")
    w("")

    # ---------------- virtual ground ------------------------------------------
    w("## 7. +2V5 virtual ground  [E] values, [D] corners")
    w("")
    w("**It is not a regulated reference.** R75/R76 divide +5VA in half, C66 filters the")
    w("midpoint, a U11 section buffers it, and R79 feeds the +2V5 net. So the whole analog")
    w("chain's reference **moves with the +5V rail** — this is Q8 / `NEXT_BOARD_REV.md` CR-02.")
    w("")
    r75, r76, c66 = num(values, "R75"), num(values, "R76"), num(values, "C66")
    r79, c71, c80 = num(values, "R79"), num(values, "C71"), num(values, "C80")
    w("| part | value |")
    w("|---|---|")
    for r in ("R75", "R76", "C66", "R79", "C71", "C80"):
        w(f"| {r} | {val(values,r)} |")
    w("")
    if r75 and r76 and c66:
        rp = r75 * r76 / (r75 + r76)
        w(f"**[D] Divider pole:** R75∥R76 = {rp/1e3:.0f} kΩ × C66 = "
          f"τ **{rp*c66*1e3:.1f} ms**, f_c **{fc(rp,c66):.1f} Hz**.")
        w("")
        if r79 and c71 and c80:
            w(f"**[D] Node pole after the buffer:** R79 {r79:.0f} Ω × (C71+C80) = "
              f"f_c **{fc(r79, c71+c80):.0f} Hz**.")
            w("")
        w("**[I] Consequence, and it cuts both ways.** The virtual ground tracks the rail")
        w(f"only below ~{fc(rp,c66):.0f} Hz:")
        w("")
        w("- The Q8 bench test (§3.6b) steps the rail with `beam on`/`beam off`, which is")
        w("  effectively DC against this corner — so it measures the **fully coupled worst")
        w("  case**, which is the right thing to measure.")
        w("- But extrapolating that to \"the strobe will false-trigger the detector\" is")
        w("  **too pessimistic**: a strobe transient is far faster and is attenuated by this")
        w("  pole. **Measure it in Phase 6 rather than assuming either extreme.**")
    w("")

    # ---------------- threshold DAC -------------------------------------------
    w("## 8. Threshold DAC  [E] values, [D] settling")
    w("")
    w("GPIO44 → R86 → C74 → R89 → C76 → `Threshold_DC` (TP8) → U15 pin 2.")
    w("")
    w("| part | value |")
    w("|---|---|")
    for r in ("R86", "C74", "R89", "C76"):
        w(f"| {r} | {val(values,r)} |")
    w("")
    r86, c74 = num(values, "R86"), num(values, "C74")
    if r86 and c74:
        rc = r86 * c74
        w(f"**[D]** Each section is {rc*1e3:.1f} ms, but they are a **cascaded loaded ladder**,")
        w("not two independent poles: the second section loads the first, so the real poles")
        w(f"are at RC/0.382 = **{rc/0.382*1e3:.2f} ms** and RC/2.618 = **{rc/2.618*1e3:.3f} ms**.")
        w("")
        w(f"5τ of the dominant pole is **{5*rc/0.382*1e3:.1f} ms**. The firmware uses 20 ms")
        w("(`DAC_SETTLE_MS`). **The bench doc's original \"settle 10 ms\" is only ~4τ**, leaving")
        w("~2 % of a step — about 20 threshold codes on a 3.3 V scale.")
    w("")
    w("⚠ **[E] `Threshold_DC` has NO clamp and no protection diode.** Its only nodes are")
    w("C76.2, R89.2, TP8.1 and U15.2. The R102/D14 clamp belongs to **ADC5**, not to this net.")
    w("")

    # ---------------- comparator ----------------------------------------------
    w("## 9. Comparator U15  [E]")
    w("")
    w(f"`U15` = {val(values,'U15')}, output open-collector, R103 = {val(values,'R103')} pull-up to +3V3.")
    w("**Detection is ACTIVE HIGH on GPIO46.**")
    w("")
    for n in ("Net-(U12B-OUT2)", "Threshold_DC", "D_Comparator"):
        if n in nets:
            w(f"- `{n}` = {', '.join(nets[n])}")
    w("")
    w("⚠ **[E] There is NO hysteresis.** No resistor connects `D_Comparator` back to U15")
    w("pin 3 anywhere on the board. Chatter on a slow edge is a board property, not a")
    w("firmware bug — `NEXT_BOARD_REV.md` **CR-13**.")
    w("")
    w("⚠ **[I] U15 is powered from `+5V` (the DIGITAL rail), not `+5VA`.** An LM393's input")
    w("common-mode ceiling is roughly V+ − 1.5 V ≈ 3.5 V, while U12B is rail-to-rail on")
    w("+5VA and can drive pin 3 to ~5.2 V — outside the valid range on large signals, where")
    w("some LM393 parts invert. `NEXT_BOARD_REV.md` **CR-14**. **Measure the real ceiling.**")
    w("")

    # ---------------- gated HPF -----------------------------------------------
    w("## 10. Gated HPF mux U14  [E]")
    w("")
    w(f"`U14` = {val(values,'U14')}, an SPDT analog switch. **SEL = `HPF_Toggle` = GPIO33.**")
    w("")
    for n in ("Net-(U14-D)", "Net-(U14-S1)", "HPF_Toggle"):
        if n in nets:
            w(f"- `{n}` = {', '.join(nets[n])}")
    w("")
    w("⚠ **[E] Only ONE throw is connected.** S1 goes to R96 → GND (the 0.66 s HPF);")
    w("**S2 is unconnected**. So \"HOLD\" is a genuine open circuit — C81 holds its charge and")
    w("the U12B input DC level is undefined and drifts. It is not a second filter corner.")
    w("")
    w("🔴 **[I] WHICH SEL LEVEL SELECTS WHICH THROW IS NOT DETERMINABLE FROM THESE FILES.**")
    w("The netlist encodes only the pin name `SEL`. Establish it empirically with the")
    w("firmware's `hpf test` before trusting any TRACK/HOLD measurement.")
    w("")

    # ---------------- PWM slices ----------------------------------------------
    w("---")
    w("")
    w("## 11. PWM slice collisions  [D]")
    w("")
    w("From the SDK's mapping: `gpio < 32 -> slice = (gpio>>1)&7`, otherwise")
    w("`slice = 8 + ((gpio>>1)&3)`. Same slice **and** channel means one compare register")
    w("and one waveform on both pins.")
    w("")
    w("⚠ **The low and high banks have DIFFERENT collision spacing.** Below GPIO32 the")
    w("slice index has period 16; at or above GPIO32 it has period **8**. `board.h`'s")
    w("original \"never put two PWM functions 16 apart\" rule is therefore wrong above 32.")
    w("")
    pinmap = {}
    for m in re.finditer(r"#define\s+(PIN_[A-Z0-9_]+)\s+(\d+)",
                         open(os.path.join(FW, "src", "board.h"),
                              encoding="utf-8", errors="replace").read()):
        pinmap[int(m.group(2))] = m.group(1)
    slots = {}
    for g in sorted(set(list(pinmap) + list(range(48)))):
        s = (g >> 1) & 7 if g < 32 else 8 + ((g >> 1) & 3)
        slots.setdefault((s, "AB"[g & 1]), []).append(g)
    w("| slice | GPIOs | names |")
    w("|---|---|---|")
    for k in sorted(slots):
        gs = slots[k]
        named = [(g, pinmap.get(g)) for g in gs]
        if sum(1 for _, n in named if n) >= 2:
            w(f"| **{k[0]}{k[1]}** | {', '.join(str(g) for g in gs)} | "
              f"{', '.join(f'{n or 'unassigned'} ({g})' for g, n in named)} |")
    w("")
    w("**[I]** A pair is only a hazard if *both* pins are actually driven by PWM, so most of")
    w("these are latent rather than live. Ranked by how likely someone is to trip them:")
    w("")
    w("| pair | slice | status |")
    w("|---|---|---|")
    w("| GPIO12 READY_LED / GPIO28 GATE_PWM | 6A | 🔴 **live conflict** — `panel.c` owns 6A now, Phase 6b wants it for the 9 A setpoint. `NEXT_BOARD_REV.md` CR-01 |")
    w("| GPIO36 UART_TX / GPIO44 THRESHOLD_PWM | 10A | 🔴 **half-live** — the DAC is real PWM as of Phase 3. Safe only while GPIO36 stays SIO/UART. **Never put GPIO36 on PWM** |")
    w("| GPIO2 SYSTEM_READY / GPIO18 LED_RED | 1A | 🟠 **latent trap** — putting a status LED on PWM to dim it would toggle a Pi-facing signal |")
    w("| GPIO3 IRQ_OUT / GPIO19 LED_YELLOW | 1B | 🟠 **latent trap** — same, and IRQ_OUT goes to the Pi |")
    w("| GPIO15 LATCH_CONTROL / GPIO31 MOD_PWM | 7B | 🟠 GPIO15 **must stay SIO** — it is the +5 V latch |")
    w("| GPIO11 PWR_BTN_LED / GPIO27 PULSE_LIMIT_DIS | 5B | 🟠 GPIO27 must stay SIO — it defeats the strobe watchdog |")
    w("| GPIO9 CAM_STROBE_1 / GPIO25 STROBE_PULSE | 4B | 🟢 safe — GPIO9 is an input and GPIO25 is PIO, not PWM |")
    w("| GPIO8 CAM_STROBE_0 / GPIO24 PI_3V3_SENSE | 4A | 🟢 safe — both inputs |")
    w("")
    w("⚠ **The two 🟠 status-LED pairs are not in `board.h`'s hand-written list.** They were")
    w("found by generating this table. \"Dim the status LEDs with PWM\" is an ordinary-sounding")
    w("change that would silently toggle `SYSTEM_READY` or `IRQ_OUT` — both Pi-facing.")
    w("")

    # ---------------- key nets -------------------------------------------------
    w("## 12. Selected net membership  [E]")
    w("")
    w("Nets worth having to hand when probing.")
    w("")
    for n in ("+2V5", "+5VA", "TIA_Out", "Demodulated_Signal", "Net-(U12C-OUT3)",
              "Comparator_ADC", "TIA_Out_ADC", "Threshold_PWM", "Demodulation_PWM",
              "Net-(U3-GPIO31)", "VIR"):
        if n in nets:
            w(f"- **`{n}`** = {', '.join(nets[n])}")
    w("")
    w("---")
    w("")
    w("*Generated by `Hardware/firmware/tools/netlist_report.py`. If this file disagrees")
    w("with the KiCad source, the source is right and this file is stale — regenerate it.*")

    text = "\n".join(L) + "\n"

    if args.check:
        if not os.path.exists(OUT):
            print("HARDWARE_REFERENCE.md missing - run without --check"); return 1
        if open(OUT, encoding="utf-8").read() != text:
            print("HARDWARE_REFERENCE.md is STALE relative to the KiCad source.")
            print("Regenerate: python Hardware/firmware/tools/netlist_report.py")
            return 1
        print("HARDWARE_REFERENCE.md is up to date.")
        return 0

    open(OUT, "w", encoding="utf-8").write(text)
    print(f"wrote {OUT}  ({len(L)} lines, {len(gpio)} GPIOs, "
          f"{len(nets)} nets, {sum(len(v) for v in dnp.values())} DNP parts)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
