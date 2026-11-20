# The one board to rule them all

This is the Rev V1 controller for the PiTrac V2 golf launch monitor. It detects the
ball crossing the infrared beam, times the cameras and strobe, and interfaces with
the Raspberry Pi. **To edit the board, open the [KiCad 10 project](The_One_Board_To_Rule_Them_All.kicad_pro).**
To read the circuit without KiCad, open the [schematic PDF](exports/The_One_Board_To_Rule_Them_All.pdf).

| Where | What's in it |
|---|---|
| Project root | The editable four-layer [PCB](The_One_Board_To_Rule_Them_All.kicad_pcb), design rules (`.kicad_dru`), [root schematic](The_One_Board_To_Rule_Them_All.kicad_sch) and seven child sheets. |
| Libraries at the root | Two symbol libraries (`.kicad_sym`), two footprint libraries (`.pretty/`) and `fp-lib-table`. Keep these beside the project. |
| [exports/](exports/) | [Netlist](exports/The_One_Board_To_Rule_Them_All.net) used by the firmware hardware-reference report, schematic PDF, parts list (BOM), and 22 SVG layer plots in plain and color versions. These are outputs, not the editable design. |
| [jlcpcb/](jlcpcb/) | Manufacturing record: copper, mask, paste, silkscreen, outline and drill files; plugin database; and the [production package](jlcpcb/production_files/) with the Gerber ZIP, assembly BOM and placement list. Its original filenames and contents are preserved as sent to the fab. |
| [legacy/](legacy/) | Two unused schematics (`IRLED` and the empty `MCU` sheet) and a second text netlist that differs only in its export timestamp. The original project ZIP, if present, is a local backup not tracked by Git. |

### Which schematic should I edit?

| Circuit | Active sheet |
|---|---|
| Power input, rails and switching | [Power.kicad_sch](Power.kicad_sch) |
| MCU, USB, Pi and camera connectors | [RP2350_80QFN_minimal.kicad_sch](RP2350_80QFN_minimal.kicad_sch) |
| Beam LED driver (not the camera strobe) | [strobe.kicad_sch](strobe.kicad_sch) |
| Camera strobe driver | [Strobe Generation.kicad_sch](Strobe%20Generation.kicad_sch) |
| Photodiode amplifier | [photodiode.kicad_sch](photodiode.kicad_sch) |
| Photodiode demodulator and comparator | [photodiode_demodulation.kicad_sch](photodiode_demodulation.kicad_sch) |
| Impact microphone | [microphone.kicad_sch](microphone.kicad_sch) |

The rename is not a new hardware revision. Historical exports and manufacturing
files can still say “The Second Board To Rule Them All.” For the circuit's theory
of operation, see the [original design document](../The_One_Board_To_Rule_Them_All.md).
