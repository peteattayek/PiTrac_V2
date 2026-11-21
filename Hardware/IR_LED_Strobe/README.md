# IR LED strobe array

This two-layer emitter board connects to J3 on the [controller](../The_One_Board_To_Rule_Them_All/README.md).
The controller contains the driver; this board carries the LEDs and their series resistors.
Open [IR_LED_Strobe.kicad_pro](IR_LED_Strobe.kicad_pro) to edit it, or read the
[two-sheet schematic PDF](exports/IR_LED_Strobe.pdf). See the [wiring guide, section 5](../../WIRING_DIAGRAM.md#5-irled--irled2-strobe-board-wiring-j3)
for the connector pinout. `VIR-` is the switched return, not ground.

## Current assembly

The PCB has four parallel strings of five LED positions. **Only the two middle rows
are fitted: 10 LEDs, not 20** (owner-confirmed 2026-11-20). The schematic and layout
show the full design, not just the fitted parts.

| Row in PCB editor (top to bottom) | LED references | Current assembly |
|---|---|---|
| 1 | D1–D5 | Empty |
| 2 | D6–D10 | Fitted |
| 3 | D11–D15 | Fitted |
| 4 | D16–D20 | Empty |

## Files

- [PCB layout](IR_LED_Strobe.kicad_pcb) and [root schematic](IR_LED_Strobe.kicad_sch).
  The only child sheet is [IRLED.kicad_sch](IRLED.kicad_sch).
- [VSMA1085250.pretty](VSMA1085250.pretty/) contains the LED footprint recovered from
  the original PCB. The library name matches the layout; the LED specified in the
  schematic is `VSMA1085400x02`.
- [exports/](exports/) contains the schematic PDF and the original [mechanical STEP](exports/IR_LED_Strobe.step).
- [jlcpcb/production_files/](jlcpcb/production_files/) contains the original Gerber ZIP,
  assembly BOM and placement list. Their historical filenames are intentional.
  The BOM and placement list contain R1–R4 only; neither is a list of the fitted LEDs.
