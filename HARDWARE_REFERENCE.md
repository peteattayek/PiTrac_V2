<!-- GENERATED FILE - DO NOT EDIT BY HAND -->
<!-- Regenerate: python Hardware/firmware/tools/netlist_report.py -->

# PiTrac board — hardware reference

**Generated from the KiCad source, not written by hand.** Regenerate with:

```bash
python Hardware/firmware/tools/netlist_report.py          # rewrite this file
python Hardware/firmware/tools/netlist_report.py --check  # fail if stale
```

Source: `Hardware/THE_SECOND_BOARD_TO_RULE_THEM_ALL/`, netlist dated 2026-07-28.

## How to use this file, and when not to

This exists because parsing the netlist costs real effort and it was done three
separate times during Phase 3. It is a **cache of the source, not a replacement**.

Every significant bug found in the Phase 3 review came from a hand-written summary
of this board that had drifted from the board: the bench doc named the wrong DNP
resistor, `board.h` carried three wrong PWM slice numbers, and `ARCHITECTURE.md`
assigned a PIO state machine to a block that cannot physically reach the pin. A
derived document is only safe when it can be re-derived and diffed — which is why
this one is generated.

**Claims are tagged by how much they can be trusted:**

| tag | meaning | how to treat it |
|---|---|---|
| **[E]** | Extracted verbatim from the netlist or schematic | Trust it. It is the source. |
| **[D]** | Derived — arithmetic on extracted values | Trust the arithmetic; check the topology assumption stated beside it. |
| **[I]** | Interpreted — a judgement about what the circuit *means* | **Verify against the schematic** before relying on it. |

⚠ **For anything damage-class or safety-critical, go to the source anyway.**
A generated file can still encode a wrong *assumption*; only the [E] rows are
immune to that.

---

## 1. RP2350 GPIO map  [E]

All 48 GPIOs, straight from the netlist. `Net-(U3-GPIOnn)` means KiCad
auto-named it — the net exists and is connected, it just has no label.

| GPIO | QFN pin | net |
|---:|---:|---|
| 0 | 77 | `Net-(U3-GPIO0)` |
| 1 | 78 | `Net-(U3-GPIO1)` |
| 2 | 79 | `Net-(U3-GPIO2)` |
| 3 | 80 | `Net-(U3-GPIO3)` |
| 4 | 1 | `D_SCK` |
| 5 | 2 | `D_WS` |
| 6 | 3 | `D_DATA` |
| 7 | 4 | `Net-(U3-GPIO7)` |
| 8 | 6 | `Cam_Strobe_0` |
| 9 | 7 | `Cam_Strobe_1` |
| 10 | 8 | `D_Cam_Trigger` |
| 11 | 9 | `Power_LED` |
| 12 | 11 | `System_Ready_LED` |
| 13 | 12 | `unconnected-(U3-GPIO13-Pad12)` |
| 14 | 13 | `Net-(U3-GPIO14)` |
| 15 | 14 | `LATCH_CONTROL` |
| 16 | 16 | `unconnected-(U3-GPIO16-Pad16)` |
| 17 | 17 | `unconnected-(U3-GPIO17-Pad17)` |
| 18 | 18 | `StatusLED_R` |
| 19 | 19 | `StatusLED_Y` |
| 20 | 20 | `unconnected-(U3-GPIO20-Pad20)` |
| 21 | 21 | `unconnected-(U3-GPIO21-Pad21)` |
| 22 | 22 | `unconnected-(U3-GPIO22-Pad22)` |
| 23 | 23 | `unconnected-(U3-GPIO23-Pad23)` |
| 24 | 25 | `Net-(U3-GPIO24)` |
| 25 | 26 | `Net-(U3-GPIO25)` |
| 26 | 27 | `unconnected-(U3-GPIO26-Pad27)` |
| 27 | 28 | `Pulse_Limit_Disable` |
| 28 | 36 | `Gate_PWM` |
| 29 | 37 | `unconnected-(U3-GPIO29-Pad37)` |
| 30 | 38 | `unconnected-(U3-GPIO30-Pad38)` |
| 31 | 39 | `Net-(U3-GPIO31)` |
| 32 | 40 | `USB_ENABLE` |
| 33 | 42 | `HPF_Toggle` |
| 34 | 43 | `unconnected-(U3-GPIO34-Pad43)` |
| 35 | 44 | `unconnected-(U3-GPIO35-Pad44)` |
| 36 | 45 | `Net-(U3-GPIO36)` |
| 37 | 46 | `Net-(U3-GPIO37)` |
| 38 | 47 | `unconnected-(U3-GPIO38-Pad47)` |
| 39 | 48 | `Net-(U3-GPIO39)` |
| 40 | 49 | `Net-(U3-GPIO40_ADC0)` |
| 41 | 52 | `Net-(U3-GPIO41_ADC1)` |
| 42 | 53 | `TIA_Out_ADC` |
| 43 | 54 | `Net-(U3-GPIO43_ADC3)` |
| 44 | 55 | `Threshold_PWM` |
| 45 | 56 | `Comparator_ADC` |
| 46 | 57 | `D_Comparator` |
| 47 | 58 | `AnalogMic_ADC` |

## 2. ADC channels  [E]

On RP2350B, ADC channel *n* is GPIO 40+*n*.

| ch | GPIO | net | firmware use |
|---:|---:|---|---|
| 0 | 40 | `Net-(U3-GPIO40_ADC0)` | strobe current (BURST) |
| 1 | 41 | `Net-(U3-GPIO41_ADC1)` | +5V_IN divider |
| 2 | 42 | `TIA_Out_ADC` | TIA_Out monitor |
| 3 | 43 | `Net-(U3-GPIO43_ADC3)` | **NEVER SAMPLE** — RPI5_SHUTDOWN is a digital output |
| 4 | 44 | `Threshold_PWM` | **NEVER SAMPLE** — Threshold_PWM is a digital output |
| 5 | 45 | `Comparator_ADC` | detect signal |
| 6 | 46 | `D_Comparator` | **NEVER SAMPLE** — D_Comparator is a digital input |
| 7 | 47 | `AnalogMic_ADC` | analog mic |

## 3. Test points  [E]

All are bare 1.0 mm THT pads, listed "No Solder" in the BOM — probe holes with
nothing fitted.

| TP | net |
|---|---|
| TP1 | `GND` |
| TP2 | `+12V` |
| TP3 | `Net-(U7-E1)` |
| TP4 | `CurrentSense_ADC` |
| TP5 | `Strobe_GND` |
| TP6 | `+2V5` |
| TP7 | `TIA_Out` |
| TP8 | `Threshold_DC` |
| TP9 | `Net-(U12C-OUT3)` |
| TP10 | `Demodulated_Signal` |

⚠ **[I] TP5 is `Strobe_GND`, the shared low-side return** — it is where Phase 2
measured the U9 beam one-shot clamp, and Phase 6 strobe pulses appear there too.

## 4. Unpopulated parts  [E]

Complete across every sheet. **Only R98 is an electrical tuning option.**

| sheet | refs |
|---|---|
| `IRLED.kicad_sch` | H5, H6, H7, H8 |
| `Power.kicad_sch` | C9, R11 |
| `photodiode_demodulation.kicad_sch` | R98 |

H* are mounting holes. C9 (100 pF on VIR) and R11 (5R1) are optional hand-solder
parts on the Power sheet. **R98 is the detect-chain gain option — see §6.**

---

## 5. Detection signal chain  [E] values, [D] corners

```
D12 --> U11A TIA --> U13 chopper demod --> 4th-order LPF --> TP9
    --> C81 + U14 gated HPF --> R99 --> U12B x14.5 --> U15 comparator --> GPIO46
                                                    \-> R102 --> D14 --> ADC5
```

| stage | parts | value |
|---|---|---|
| Photodiode | D12 | VBPW34FAS |
| Bias | R77 / C67 | 10K from VIR / 0.1uF |
| TIA feedback | R80 | 470K |
| TIA fb cap | C68 **series** C70 | 1pF + 1pF |
| Isolation | R81 / C72 | 22R / 2.2nF |
| Servo integrator | R83 / C73 | 1M / 330nF |
| Servo inverter | R84 / R85 | 10K / 10K |
| Servo injection | R78 | 100K |
| Demod difference amp | R95 / R97 | 10K / 10K |
| LPF section 1 | R87/R88, C75/C77 | 4.7k, 2.2nF |
| LPF section 2 | R90/R92, C78/C79 | 4.7k, 2.2nF |
| LPF gain | R93 / R94 | 4.7k / 4.7k |
| Gated HPF | C81 / R96 | 330nF / 2M |
| Output amp | R99, R101, R100, R98(DNP) | 1K, 27K, 2K, 2K |
| ADC5 protection | R102 / D14 | 1K / BAT54S |

### Derived corner frequencies  [D]

| quantity | value | assumption |
|---|---|---|
| **TIA feedback pole** | **677 kHz** | C68 and C70 in *series* = 0.5 pF across R80 |
| Servo integrator | 0.482 Hz | R83 x C73 |
| **DC servo corner** | **2.27 Hz** | integrator x R80/R78 = 4.7. **Matches the schematic annotation of 2.27 Hz** |
| **LPF f0** | **15.39 kHz** | Sallen-Key, equal R and C. Schematic annotation says 15.9 — that is stale |
| **Gated HPF** | **tau 0.66 s, f_c 0.241 Hz** | C81 x R96, U14 selecting S1 |

### TIA phase lag vs carrier  [D]

**This is why one demod-phase number cannot cover the carrier scan range**, and
why `cal demod model` fits phase against frequency instead of assuming a pure
delay. It is a testable prediction: the fitted model should show this curvature.

| carrier | lag |
|---:|---:|
| 104.2 kHz | 8.7° |
| 150.0 kHz | 12.5° |
| 200.0 kHz | 16.5° |
| 250.0 kHz | 20.3° |

## 6. R98 — the gain option  [E] values, [D] table

R98 and R100 are a **parallel pair** from GND to U12B's inverting input.
**R100 (2K) is FITTED; R98 is the unpopulated one** — and it was left
unpopulated so its *value* could be chosen after measuring, which makes gain a
continuous knob rather than an on/off option.

```
G = 1 + R101 / (R100 || R98)
R98 = R100 * R_gnd / (R100 - R_gnd),  R_gnd = R101 / (G - 1)
```

| R98 | R100 ∥ R98 | gain | ΔTP9 at ADC5 full scale |
|---|---|---|---|
| absent | 2000 Ω | **14.5** | 228 mV |
| 10 k | 1667 Ω | 17.2 | 192 mV |
| 4.7 k | 1403 Ω | 20.2 | 163 mV |
| 2 k | 1000 Ω | 28.0 | 118 mV |
| 1 k | 667 Ω | 41.5 | 80 mV |

⚠ **[I] Full scale is the ADC's 3.3 V, not D14's ~3.6 V** — ADC5 saturates at code
4095 before the BAT54S conducts, so clipping appears as ADC full scale rather than a
diode knee. And the LM393's input common-mode ceiling (~3.5 V) is *lower still*.

## 7. +2V5 virtual ground  [E] values, [D] corners

**It is not a regulated reference.** R75/R76 divide +5VA in half, C66 filters the
midpoint, a U11 section buffers it, and R79 feeds the +2V5 net. So the whole analog
chain's reference **moves with the +5V rail** — this is Q8 / `NEXT_BOARD_REV.md` CR-02.

| part | value |
|---|---|
| R75 | 10K |
| R76 | 10K |
| C66 | 1uF |
| R79 | 10R |
| C71 | 10uF |
| C80 | 0.1uF |

**[D] Divider pole:** R75∥R76 = 5 kΩ × C66 = τ **5.0 ms**, f_c **31.8 Hz**.

**[D] Node pole after the buffer:** R79 10 Ω × (C71+C80) = f_c **1576 Hz**.

**[I] Consequence, and it cuts both ways.** The virtual ground tracks the rail
only below ~32 Hz:

- The Q8 bench test (§3.6b) steps the rail with `beam on`/`beam off`, which is
  effectively DC against this corner — so it measures the **fully coupled worst
  case**, which is the right thing to measure.
- But extrapolating that to "the strobe will false-trigger the detector" is
  **too pessimistic**: a strobe transient is far faster and is attenuated by this
  pole. **Measure it in Phase 6 rather than assuming either extreme.**

## 8. Threshold DAC  [E] values, [D] settling

GPIO44 → R86 → C74 → R89 → C76 → `Threshold_DC` (TP8) → U15 pin 2.

| part | value |
|---|---|
| R86 | 10K |
| C74 | 0.1uF |
| R89 | 10K |
| C76 | 0.1uF |

**[D]** Each section is 1.0 ms, but they are a **cascaded loaded ladder**,
not two independent poles: the second section loads the first, so the real poles
are at RC/0.382 = **2.62 ms** and RC/2.618 = **0.382 ms**.

5τ of the dominant pole is **13.1 ms**. The firmware uses 20 ms
(`DAC_SETTLE_MS`). **The bench doc's original "settle 10 ms" is only ~4τ**, leaving
~2 % of a step — about 20 threshold codes on a 3.3 V scale.

⚠ **[E] `Threshold_DC` has NO clamp and no protection diode.** Its only nodes are
C76.2, R89.2, TP8.1 and U15.2. The R102/D14 clamp belongs to **ADC5**, not to this net.

## 9. Comparator U15  [E]

`U15` = LM393, output open-collector, R103 = 10K pull-up to +3V3.
**Detection is ACTIVE HIGH on GPIO46.**

- `Net-(U12B-OUT2)` = R101.2, R102.2, U12.7, U15.3
- `Threshold_DC` = C76.2, R89.2, TP8.1, U15.2
- `D_Comparator` = R103.1, U15.1, U3.57

⚠ **[E] There is NO hysteresis.** No resistor connects `D_Comparator` back to U15
pin 3 anywhere on the board. Chatter on a slow edge is a board property, not a
firmware bug — `NEXT_BOARD_REV.md` **CR-13**.

⚠ **[I] U15 is powered from `+5V` (the DIGITAL rail), not `+5VA`.** An LM393's input
common-mode ceiling is roughly V+ − 1.5 V ≈ 3.5 V, while U12B is rail-to-rail on
+5VA and can drive pin 3 to ~5.2 V — outside the valid range on large signals, where
some LM393 parts invert. `NEXT_BOARD_REV.md` **CR-14**. **Measure the real ceiling.**

## 10. Gated HPF mux U14  [E]

`U14` = TMUX1219DBVR, an SPDT analog switch. **SEL = `HPF_Toggle` = GPIO33.**

- `Net-(U14-D)` = C81.2, R99.2, U14.5
- `Net-(U14-S1)` = R96.1, U14.4
- `HPF_Toggle` = U14.1, U3.42

⚠ **[E] Only ONE throw is connected.** S1 goes to R96 → GND (the 0.66 s HPF);
**S2 is unconnected**. So "HOLD" is a genuine open circuit — C81 holds its charge and
the U12B input DC level is undefined and drifts. It is not a second filter corner.

🔴 **[I] WHICH SEL LEVEL SELECTS WHICH THROW IS NOT DETERMINABLE FROM THESE FILES.**
The netlist encodes only the pin name `SEL`. Establish it empirically with the
firmware's `hpf test` before trusting any TRACK/HOLD measurement.

---

## 11. PWM slice collisions  [D]

From the SDK's mapping: `gpio < 32 -> slice = (gpio>>1)&7`, otherwise
`slice = 8 + ((gpio>>1)&3)`. Same slice **and** channel means one compare register
and one waveform on both pins.

⚠ **The low and high banks have DIFFERENT collision spacing.** Below GPIO32 the
slice index has period 16; at or above GPIO32 it has period **8**. `board.h`'s
original "never put two PWM functions 16 apart" rule is therefore wrong above 32.

| slice | GPIOs | names |
|---|---|---|
| **1A** | 2, 18 | PIN_SYSTEM_READY (2), PIN_LED_RED (18) |
| **1B** | 3, 19 | PIN_IRQ_OUT (3), PIN_LED_YELLOW (19) |
| **4A** | 8, 24 | PIN_CAM_STROBE_0 (8), PIN_PI_3V3_SENSE (24) |
| **4B** | 9, 25 | PIN_CAM_STROBE_1 (9), PIN_STROBE_PULSE (25) |
| **5B** | 11, 27 | PIN_PWR_BTN_LED (11), PIN_PULSE_LIMIT_DIS (27) |
| **6A** | 12, 28 | PIN_READY_LED (12), PIN_GATE_PWM (28) |
| **7B** | 15, 31 | PIN_LATCH_CONTROL (15), PIN_MOD_PWM (31) |
| **10A** | 36, 44 | PIN_UART_TX (36), PIN_THRESHOLD_PWM (44) |

**[I]** A pair is only a hazard if *both* pins are actually driven by PWM, so most of
these are latent rather than live. Ranked by how likely someone is to trip them:

| pair | slice | status |
|---|---|---|
| GPIO12 READY_LED / GPIO28 GATE_PWM | 6A | 🔴 **live conflict** — `panel.c` owns 6A now, Phase 6b wants it for the 9 A setpoint. `NEXT_BOARD_REV.md` CR-01 |
| GPIO36 UART_TX / GPIO44 THRESHOLD_PWM | 10A | 🔴 **half-live** — the DAC is real PWM as of Phase 3. Safe only while GPIO36 stays SIO/UART. **Never put GPIO36 on PWM** |
| GPIO2 SYSTEM_READY / GPIO18 LED_RED | 1A | 🟠 **latent trap** — putting a status LED on PWM to dim it would toggle a Pi-facing signal |
| GPIO3 IRQ_OUT / GPIO19 LED_YELLOW | 1B | 🟠 **latent trap** — same, and IRQ_OUT goes to the Pi |
| GPIO15 LATCH_CONTROL / GPIO31 MOD_PWM | 7B | 🟠 GPIO15 **must stay SIO** — it is the +5 V latch |
| GPIO11 PWR_BTN_LED / GPIO27 PULSE_LIMIT_DIS | 5B | 🟠 GPIO27 must stay SIO — it defeats the strobe watchdog |
| GPIO9 CAM_STROBE_1 / GPIO25 STROBE_PULSE | 4B | 🟢 safe — GPIO9 is an input and GPIO25 is PIO, not PWM |
| GPIO8 CAM_STROBE_0 / GPIO24 PI_3V3_SENSE | 4A | 🟢 safe — both inputs |

⚠ **The two 🟠 status-LED pairs are not in `board.h`'s hand-written list.** They were
found by generating this table. "Dim the status LEDs with PWM" is an ordinary-sounding
change that would silently toggle `SYSTEM_READY` or `IRQ_OUT` — both Pi-facing.

## 12. Selected net membership  [E]

Nets worth having to hand when probing.

- **`+2V5`** = C71.1, C80.2, R79.2, R93.1, TP6.1, U11.12, U11.3, U11.5, U13.6
- **`+5VA`** = C22.2, C25.2, C69.1, C82.1, FB2.2, R75.1, U11.4, U12.4, U13.2, U14.2
- **`TIA_Out`** = C72.1, R81.2, R82.2, R95.1, TP7.1, U13.4
- **`Demodulated_Signal`** = R87.1, R97.2, TP10.1, U12.1
- **`Net-(U12C-OUT3)`** = C79.2, C81.1, R94.2, TP9.1, U12.8
- **`Comparator_ADC`** = D14.3, R102.1, U3.56
- **`TIA_Out_ADC`** = D13.3, R82.1, U3.53
- **`Threshold_PWM`** = R86.1, U3.55
- **`Demodulation_PWM`** = R40.1, R91.1, U13.1
- **`Net-(U3-GPIO31)`** = R38.2, U3.39
- **`VIR`** = C11.1, C13.1, C15.1, C16.1, C17.1, C18.1, C19.1, C20.1, C21.1, C23.1, C26.1, C9.2, D10.1, D2.1, J3.1, R1.1, R15.1, R77.1

---

*Generated by `Hardware/firmware/tools/netlist_report.py`. If this file disagrees
with the KiCad source, the source is right and this file is stale — regenerate it.*
