# The stand: where the parts go, and why

The stand on the bench (`FullStand`, SolidWorks, 2026-11-18) is built around component
positions chosen by a numerical optimizer, which lives outside this repo.

- **`Github-Personal\Faceplate\Faceplate_Design`** (2026-11-19) holds a cleaned-up, verified
  copy of the optimizer, the chosen design's record, and the stand's CAD. Use it (§5).
- **`Github-Personal\Faceplate\PiTrac_Ambidextrous_Printable_Product_CODEX_Handoff_20260901`**,
  called **Faceplate** below, is the original working folder. The evidence paths in §2 point
  into it, and it still holds the full 36 GB record.

The optimizer's last output, `reference_003` (2026-10-02), placed the two
cameras, the strobe and the trigger. The printed parts were then drawn in SolidWorks around
those positions.

This folder records:

- the geometry, relative to the ball (§1);
- why the optimizer chose it (§2);
- how the built stand compares, checked from its assembly STEP (§3);
- what the geometry means for the firmware and the bench (§4);
- how to produce a new design from changed assumptions (§5);
- which files are where (§6).

An illustrated, single-page version, with to-scale drawings, the decision timeline and an
interactive timing model, is [`PITRAC_V2_STAND_GEOMETRY.html`](../../PITRAC_V2_STAND_GEOMETRY.html)
at the repo root.

Status: a provisional prototype. The optimizer's scores are modelled capture, not measured
accuracy. Nothing here is a fabrication or production release.

---

## 1. The geometry

### 1.1 Frames

| Frame | Origin | Axes | Used by |
|---|---|---|---|
| **RH ball frame** (this document) | the floor, under the ball | +X downrange, +Y from the stand towards the ball, +Z up | `reference_003` RH files, the printed parts, [`built/STAND_GEOMETRY_RH.csv`](built/STAND_GEOMETRY_RH.csv) |
| Optimizer world | the mat top, under the ball | as RH | the workbench and its scene files. RH Z = world Z + 36 |
| Chassis | on the stand | +Y towards the ball, +Z up | `SCENE.json` component poses. RH = chassis + (65, −408.1, 33.5) |
| SolidWorks assembly | arbitrary | as RH | `FullStand`. RH = assembly − (597.67, 1296.56, 1251.95), the origin of the printed parts |

- **The mat is assumed 36 mm thick, with the stand on the floor beside it.** The floor kit of
  2026-09-17 made this assumption, and the printed base's underside is at RH Z = 0. If the
  stand stands on the mat instead, every height relative to the ball is 36 mm too high.
- **Left-handed play uses the same stand, turned 180° about Z** and placed at Y = +408.1
  instead of −408.1. The trigger, cameras and focus do not change between hands.

### 1.2 Positions (RH ball frame, mm)

Ball centre: (0, 0, 57.3) resting on the mat; (0, 0, 76.0) on a tee with a 40 mm centre
height.

| Part | X | Y | Z | Aim | Source |
|---|---:|---:|---:|---|---|
| CAM0 entrance pupil (lower camera) | 65.0 | −368.8 | 97.6 | level, at the ball line; focus 390 mm | optimizer |
| CAM1 entrance pupil (upper camera) | 65.0 | −368.5 | 333.5 | pitched −22°; focus 388.2 mm | optimizer |
| [IR LED strobe](../IR_LED_Strobe/README.md) (20 positions; 10 LEDs fitted in the middle rows) | 65.0 | −372.3 | 231.8 | pitched −16° | assembly; LED centroid |
| Trigger anchor ("rod"); the lens holder's rear face | 65.0 | −404.7 | 143.5 | towards the ball | optimizer |
| V2 board, component face | — | −419.1 | — | faces the ball | assembly + KiCad |
| D11 beam LED (V2 board) | 65.0 | −419.1 | 143.5 | behind `Lens_LED` | assembly + KiCad |
| D12 photodiode, 36 V (V2 board) | 65.0 | −419.1 | 183.5 | behind `Lens_Photodiode` | assembly + KiCad |
| U16 microphone (CMM-2718AT) | 100.6 | −419.1 | 201.9 | faces the ball | assembly + KiCad |

- The camera pupils are entrance pupils, not board centres. Each lies 14 mm in front of the
  camera PCB's component side: a single hand measurement (2026-09-30), "provisional until final
  focus".
- **CAM0 looks level along +Y.** Its focus plane is 390 mm out, at Y = +21 mm: the far side of
  the ball line.
- **CAM1 looks down at 22°.** Its focus point is (65, −8.5, 188.1): 152 mm above the mat, for
  rising shots.
- Pupil to the resting ball: **CAM0 376.6 mm, CAM1 465.1 mm, mic 454.6 mm**. The mic is 449.0 mm
  from a teed driver ball.
- **The light sheet is the vertical plane X = 65 mm**, 65 mm downrange of the ball centre. The
  ball's leading edge starts 43.7 mm from that plane.
- **Printed parts, from their STEP exports:**
  - footprint X 5.5 to 125.5 mm, Y −539.7 to −366.3 mm (120 × 173 mm);
  - top at Z 367.1 mm: 331 mm above the mat top;
  - the stand's face is about 366 mm from the ball line.
  - Some part STEPs predate their SolidWorks saves (§6), so treat these to ±a few mm.

Every row marked "assembly" comes from [`tools/stand_geometry.py`](tools/stand_geometry.py)
run on `FullStand.STEP`; §3 lists the checks it makes.

---

## 2. Why this layout

### 2.1 The requirements it was optimised against

The owner confirmed these in Faceplate's 2026-09-05 and 2026-09-06 handoffs.

- **One fixed internal assembly for both hands.** To switch hands, move the intact unit to the
  marked position for that hand. No recalibration and no refocusing. Cameras and strobe never
  move.
- **Shots:** drivers, irons, wedges and chips, both hands. Lob/flop is "highly desired".
  Putting needs at least one camera to see the ball.
- **Hitting area:** a shared 150 × 100 mm area is strongly desired, not required. Different
  recommended ball positions per shot type are allowed.
- **Finished enclosure:** ≤ 350 mm wide, ≤ 350 mm high, face ≤ 70,000 mm². No larger fallback.
- **Trigger and strobe model:**
  - a Pico 2 (now the RP2354B board) triggers both Mira220s in hardware;
  - speed comes from the time the ball takes to cross one light sheet, modelled as **30 mm
    thick** (owner-declared, not measured);
  - exposure and strobing start after the ball's trailing edge leaves the sheet, with **25 µs**
    assumed readiness;
  - if speed is unusable, a fixed fallback burst runs instead.

### 2.2 The decision chain

Evidence paths are relative to Faceplate's `work\d_optimization_v3\` unless shown otherwise.

| Date (2026) | Step | Outcome | Evidence |
|---|---|---|---|
| Jul 21–22 | First placement optimizer, `pitrac_placement_optimizer.py`: the "optimizer" of the board design doc §18 (`--pose-mc`, "focus report") | Its `joint_v12` successor is the historical kernel inside Faceplate that the workbench still calls | `Downloads\PitracPositoinOptimizerV2\`; Faceplate `03_CRITICAL_CURRENT_SOURCE\pitrac_placement_optimizer_joint_v12(4)(1).py` |
| Jul–Aug | Lens and camera campaigns | Mira220 sensor; the IMX462 and lens campaigns are in `Downloads\PiTrac_*` | outside Faceplate |
| Sep 3 | Six lens pairs compared at matched geometry | **Spinel ND3630, 3.60 mm F/3.0**: highest modelled capture (G 0.894), compact, $20 | `lens_expansion_20260903\final\` |
| Sep 5–6 | Four layout families: incumbent, vertical spine, low/wide, diagonal | 3,221 evaluations. Fixed vertical spine: worst-focus capture 0.822 over an 80 × 100 mm zone. The sliding variant scored higher but over a 20 × 90 mm zone, and has a moving part. Low/wide failed mid-irons (5 %). Fixed vertical failed lob/flop (0.01 %) | `architecture_recovery_20260905\`, `architecture_qualification_20260906\SHOT_CATEGORY_FINDINGS_20260906.md` |
| Sep 6 | Controller study, geometry fixed | The trigger/strobe model of §2.1 | `architecture_controller_20260906\` |
| Sep 8 | Bounded follow-ups | Keep CAM0 level: tilting it up helps lob/flop but costs focus margin. A 6° counter-yaw is rejected. Normal dark gaps +2 %. The 47.33 mm spacing used in §4.1 is the workbench controller's setting, carried unchanged through the later searches | `vertical_pitch_focus_20260908\`, `camera_axis_counteryaw_20260908\`, `pulse_spacing_retune_20260908\` |
| Sep 9 | **Design workbench** built | Interactive editor and evaluator, with STEP export (§5) | Faceplate `tools\design_workbench\` |
| Sep 9–10 | Search with every component ≤ 250 mm high | 1,672 trials. Candidate B improved all categories but had a slightly worse robustness penalty | `compact250_20260909\SESSION_HANDOFF_20260910.md` |
| Sep 14 | Cap raised to 300 mm; movable-zone objective | Saved candidates scored on capture maps | `region150x100_20260914\` |
| **Sep 15** | **The selection objective:** per club and per hand, the best 100 × 50 mm zone; weighted driver 0.30, mid-iron 0.35, wedge 0.20, pitch/chip 0.12, lob/flop 0.03; ranked by the worse hand | **`region150x100 campaign_v2 trial_020` ("weighted020"): 97.22 %**, the best of 335 saved candidates. Not a global optimum | `weighted_club100x50_20260915\FINAL_REPORT.md` |
| Sep 17 | Floor kit for H2S/H2D printers | The 36 mm mat convention: the whole dock is raised 36 mm | `floor_comparison_20260917\README.md` |
| Sep 30 | Pupil measured | PCB to pupil 14 mm, sensor to pupil 13.706 mm. The camera boards move 5.29 mm towards the ball along their axes; the optical centres do not move | `fixed_plates_pcb14_20260930\README.md` |
| **Oct 1** | **Strobe repair:** a CAD keep-clear screen found the strobe board (Z 180 mm, −8°) cutting 364 mm³ into the clear light path of the trigger's upper half-rod lens | Bounded grid, Z 180–220 mm by pitch −16 to 0°. **Z 200 mm (chassis), −16° is the smallest passing rise**: 97.20 % nominal, 79.30 % worst five-focus | `strobe_repair_pcb14_20261001\index.html`, `run_001\CAD_SCREEN_RESULT.json` |
| Oct 1 | Two-piece, four-piece and integrated-trigger faceplates | Printable references, superseded by the owner's own design | `four_piece_faceplate_20261001\` etc. |
| **Oct 2** | **`reference_003`:** the components alone at their operating poses, in CHASSIS, RH and LH frames | **The basis of the built stand**: [`reference_003/`](reference_003/) | `floating_components_20261002\` |
| Oct 5 | First SolidWorks stand: `BaseStand` and the V2 camera mounts | — | Faceplate `work\Enclosure_Cad\` |
| Nov 17–18 | `FullStand`: TILT camera mounts, `Base_V2`, lens holder, photodiode shroud, the V2 board, the Pi 5, an LRS-75 supply and a switch | The built stand: §3 | `Downloads\PitracStand\` |

### 2.3 What weighted020 scores, and what that hides

These are the per-club zones, RH. LH mirrors Y. X is along the target line, with negative X
farther from the sheet. Negative Y is towards the stand.

| Club | Weight | Zone X, mm | Zone Y, mm | Mean capture | Lowest sample |
|---|---:|---|---|---:|---:|
| driver | 0.30 | −75 to +25 | −25 to +25 | 98.7 % | 98.0 % |
| mid-iron | 0.35 | −75 to +25 | −75 to −25 | 99.3 % | 98.8 % |
| wedge | 0.20 | −75 to +25 | −50 to 0 | 98.4 % | 97.4 % |
| pitch/chip | 0.12 | −75 to +25 | −50 to 0 | 92.4 % | 92.1 % |
| lob/flop | 0.03 | −75 to +25 | 0 to +50 | 69.0 % | 50.4 % |

- **Focus sensitivity is the largest weakness.** With ±10 µm image-plane error in either
  camera, the weighted score falls from 97.2 % to **79.3 %**.
  - Moving the mid-iron zone trades nominal score for focus margin, on the same hardware:
    - X −125 to −25, Y −50 to 0: 97.0 % nominal, 85.4 % worst-focus;
    - X −150 to −50, Y −25 to +25: 96.4 % nominal, 88.1 % worst-focus.
- **Lob/flop is weak**, hidden by its 3 % weight.
- **The optics are idealised:** a pinhole projection, no measured distortion, and a provisional
  pupil. There is no exact lens body.
- **The timing is assumed:** a 25 µs readiness and a 30 mm sheet, neither measured.
- Mechanics ran as warnings, with a legacy projection warning on the strobe (9.09 mm).
- **None of this is measured golf accuracy.**

---

## 3. The built stand against the design

`tools/stand_geometry.py` reads `FullStand.STEP` (2026-11-18), the V2 board's `.kicad_pcb` and
`reference_003/COMPONENT_DATUMS_MM.csv`. Results:

| Check | Result |
|---|---|
| Printed parts share one placement: `Base_V2`, `BottomCam_v2`, `LensHolderBase`, `PhotodiodeShroud`, `TopCam_v2` | yes. Their origin is the RH origin |
| Both camera pupils, from one shared module offset fitted to both cameras (6 equations, 3 unknowns) | **0.000 mm** from the reference, offset (19, 19, 9) mm |
| Camera axes against the reference | **0.000°**. CAM0 level, CAM1 −22.00° |
| Strobe LED centroid against the reference strobe axis | **0.04 mm off the axis**, 6.0 mm along it. Board −16.00° |
| V2 board M2 holes H1, H2, H7 and H8 against the M2 standoffs | **0.00 mm** in the board plane, which confirms the board placement |
| D11 against the reference trigger anchor | the same X and Z. D11 is 14.35 mm behind the anchor, which is the lens holder's rear face. The lens parts' origins sit 3.2–3.4 mm in front of it |

**The same as the design:**

- the camera pupils, axes and focus distances;
- the strobe height and pitch (the Oct 1 repair);
- the trigger anchor's position (X = 65 mm plane, Z 143.5 mm);
- the handed dock pose, and the 36 mm floor convention.

**Different from the design:**

- **The trigger is the V2 board itself.** D11 shines through `Lens_LED`, and D12 looks through
  `Lens_Photodiode` and its shroud. The optimizer modelled the original separate trigger and
  an ideal 30 mm slab. The Oct 1 keep-clear screen, which moved the strobe, ran on that
  original trigger's lenses, not on these.
- **Added:**
  - the Pi 5, behind the V2 board on M2.5 standoffs;
  - an LRS-75 supply on the base;
  - a PV-series switch near the base.
  None of them was in the optimizer's obstruction model.
- **Nothing measures the mat.** The 36 mm convention is still an assumption (§1.1).

---

## 4. What the geometry means for the firmware and the bench

### 4.1 Two timing models that disagree

| | Optimizer (the stand was scored with this) | Board design doc, `The_One_Board_To_Rule_Them_All.md` §13.1 |
|---|---|---|
| Sheet | 30 mm thick, centred on X = 65 mm | `beam_width_mm = 3.0` ("calibrate!"). **The owner: it should have been 3 cm** (2026-11-19) |
| First flash | 25 µs after the trailing edge: ball centre at X ≈ 101 mm | `beam_to_fov_mm = 150`: ball centre at X ≈ 215 mm |
| Spacing | 47.33 mm; 10-pulse fallback | `freeze_spacing_mm = 42.67` |

**Where the design doc's numbers came from.** Neither rests on a measurement.

- **3 mm.** §13.1 gives no source, only "calibrate!". The July 2026 copy of the doc has the
  same line.
  - The optimizer written alongside it (`Downloads\PitracPositoinOptimizerV2\`) models the
    sheet as 25 mm, "2-3 cm". Faceplate used 25 mm, then 30 mm from 2026-09-06.
  - **The owner confirms about 3 cm (2026-11-19).**
  - The firmware is unaffected: it computes no speed yet, and its transit length
    (`detect path`) is unset. That length is a per-board calibration (Phase 3), not D + w.
- **150 mm.** §15's only justification is that it clears `(D+w)/2 + v·(t_fixed + t_cam)`
  "to ~90 m/s+", with `t_cam` assumed at 100–300 µs. §16 step 8 says to start with a
  generous margin and tighten it from the images.
  - With 300 µs, the formula needs only about 53 mm at 90 m/s. 150 mm is a deliberately loose
    starting value.
  - Neither optimizer used it. The July one put the first freeze at its own latency floor:
    x ≈ 109 mm with the sheet at 50 mm, or 59 mm past the sheet. Faceplate put it 25 µs after
    the trailing edge.

**How early the first freeze can come.** 7c.1 measured `t_cam` at about 0.2 µs, so the
cameras no longer force a margin. The firmware still sets one.

- **The design formula's floor**, with a 30 mm sheet and the design's 32 µs detection
  latency, is 36.3 mm + v × 32 µs past the sheet centre: 39.5 mm at 100 m/s.
- **The live current readback sets a larger floor.**
  - A live firing waits until ADC0 has written 200 µs of baseline after the switch to BURST.
    On board 1 that took 198–200 µs on every live firing (6c, 2026-10-12 and 13).
  - A9 puts that switch after the speed analysis. The owner's direction (2026-11-20) moves it
    to the trailing edge, ahead of the analysis (`SHOT_PATH_PLAN.md` §2a).
  - With the design's 32 µs of detection on top, about 0.23 ms in all: at 90 m/s that is
    about 18 mm later than the optimizer's 25 µs. Four whole-ball freezes still fit in CAM0's
    view (7c.3).
  - *(Corrected 2026-11-20: first written here as about 0.4 ms and 3 freezes.)*
- **The shot path needs firmware work first:**
  - a shot-mode reference event instead of the 2 ms bench coalescer;
  - a trailing-edge time that the superloop does not distort;
  - a per-pulse burst encoder;
  - a non-blocking live readback.
  The plan is [`../firmware/SHOT_PATH_PLAN.md`](../firmware/SHOT_PATH_PLAN.md).
- **An earlier burst leaves the mic less time** (§4.2).

- The firmware does not implement `beam_to_fov_mm` yet. It is the 7c.3 desk check in
  `../firmware/BENCH_P5_P7_MIC_CAMERA.md`: PASS 2026-11-20.
- **First-order field of view.** Assume a pinhole, 3.60 mm focal length, 1600 × 2.79 µm across
  and 368.8 mm to the ball line. CAM0 then spans X −164 to +294 mm at the ball line. The whole
  ball is in view for centres up to X ≈ 272 mm.
  - **150 mm:** about **2** whole-ball freezes, at 215 and 258 mm.
  - **The optimizer's model at 70 m/s:** about **4**, at 103, 150, 198 and 245 mm.
  - If the 1600-pixel side is vertical, the field is 12 % narrower: 1 and 3 freezes.
- **7c.3 passed on 2026-11-20.** What it leaves open:
  - the first-flash latency (`SHOT_PATH_PLAN.md` D1, D2). Four freezes at 100 m/s need the
    first flash within 289 µs of the trailing edge at 47.33 mm spacing, or 429 µs at
    42.67 mm;
  - the spacing. Under the owner's D7 (2026-11-20) the detected shot uses the workbench
    table's 47.33 mm. The firmware's 42.67 mm (`STROBE_FREEZE_SPACING_UM`) stays for
    `cam shot` test bursts.

### 4.2 The impact sound at the mic

Assumptions: sound at 343 m/s (20 °C); mic to ball 454.6 mm on the mat, 449.0 mm teed. The
sound arrives **1.31–1.33 ms** after first contact. FIRE times assume the ball leaves X = 0 at
full speed at first contact.

| Ball speed | FIRE, 150 mm model | FIRE, optimizer model | Sound before FIRE? |
|---:|---:|---:|---|
| 40 m/s | 5.38 ms | 2.56 ms | yes, in both models |
| 60 m/s | 3.58 ms | 1.71 ms | yes, in both models |
| 70 m/s | 3.07 ms | 1.47 ms | yes, in both models |
| 80 m/s | 2.69 ms | 1.29 ms | 150 mm: yes. Optimizer: about level |
| 100 m/s | 2.15 ms | 1.04 ms | 150 mm: yes. Optimizer: **no, 0.27–0.29 ms after** |

- In the optimizer's model, the crossover is **≈ 79 m/s** (177 mph).
- About 0.45 ms of club contact delays the ball. Allowing for it moves the crossover to about
  96 m/s.
- **With the 150 mm model, the sound always arrives before FIRE.**

---

## 5. Making a new design from changed assumptions

Today the pipeline is: the optimizer (Faceplate) produces component poses and reference STEPs;
**you** draw the printed parts in SolidWorks around them; this repo's tool then checks the
result.

The optimizer and the CAD are in `Github-Personal\Faceplate\Faceplate_Design`. Its README has
the quick start and the proof that the copy reproduces the record. The paths below are relative
to that folder.

### 5.1 Where each assumption lives

| Assumption | Current value | Change it in | What must rerun |
|---|---|---|---|
| Component poses and focus | `reference_003/SCENE.json` | workbench editor: drag, or type in the inspector | Evaluate, Focus check, Capture map, Active-LUT64 |
| Hand positions (docks) | RH (65, −408.1), LH (65, +408.1), yaw 0/180° | workbench, select the dock | as above |
| Ball starts, tee heights | driver 40 mm, others resting | workbench, select the ball | as above |
| Readiness, spacing, delay, fallback | 25 µs, 47.33 mm, 0 µs, `flat15` × 10 | workbench timing panel ("Rebuild strobe timing", then activate) | timing rebuild, then evaluations |
| Sheet thickness | 30 mm | **hard-coded**: `.07267` (D + 30 mm) in `optimizer\tools\design_workbench\backend\clock_model.py` line 79 | every score. Editable before the workbench starts. It changes the engine's source identity, so cached results recompute; record the change (§5.3) |
| Mat thickness | 36 mm | CAD only: the floor lift in the CAD builders, and your SolidWorks base | the CAD, then `tools/stand_geometry.py --mat-mm` |
| Pupil (PCB to pupil) | 14 mm | the workbench's per-camera pupil field. The printed-part CAD builders read `fixed_plates_pcb14_20260930\measurement.json`; they are in the original Faceplate folder only | CAD; the body positions only |
| Height cap | component datum Z ≤ 300 mm (chassis) | `BOUNDS` in a copied search script | the search |
| Club weights and zone size | 0.30/0.35/0.20/0.12/0.03; 100 × 50 mm | a copied `weighted_club100x50_20260915\objective.py` | rescoring the saved maps (fast) |
| Lens | ND3630, 3.60 mm F/3.0 | the historical optical kernels | a new campaign. Not a workbench edit |

### 5.2 Steps

All of these run in `Faceplate_Design`.

1. **Open the workbench.** Run `Import_Weighted020.cmd` once, then `Launch_Workbench.cmd`. Your
   browser opens `http://127.0.0.1:8765`.
   - Pick the configuration nearest yours. Edit poses, focus, docks, ball starts or timing.
   - Run Evaluate, Focus check, Capture map and Active-LUT64. Compare against weighted020
     pinned as the reference.
   - Its own README is `optimizer\tools\design_workbench\README.md`; read it first.
2. **Optional: search.** Copy `optimizer\work\d_optimization_v3\region150x100_20260914\campaign_v2.py`
   (or `compact250_20260909\search.py`) into a new dated folder. Change `BOUNDS` or the
   objective.
   - The originals are templates. They import cleanly, but they check historical run folders
     that were not carried into `Faceplate_Design`.
   - From `optimizer\`, run its `init`, then its `search` phase, with
     `tools\design_workbench\.venv\Scripts\python.exe -B -m <module> init`.
   - Rescore with a copied `weighted_club100x50_20260915\rescore.py`.
3. **Export the components.** In the workbench, choose Export STEP. You get chassis and world
   STEPs, RH and LH, with optical references (camera FOV, strobe and sheet), from **any** scene.
   - The world frame has the mat top at Z = 0. Add the mat thickness to match the RH floor frame
     of §1.1.
   - Export needs FreeCAD 1.1.3 at
     `%LOCALAPPDATA%\PiTracTools\FreeCAD-1.1.3\...\bin\python.exe` (installed on this PC), or set
     `PITRAC_FREECAD_PYTHON`.
4. **Draw the parts** in SolidWorks around the imported components, in that frame. That is how
   `FullStand` was made. Export the assembly STEP, and a STEP per printed part.
5. **Check the build.** Run
   `python Hardware/Stand/tools/stand_geometry.py --step <assembly.STEP> --csv Hardware/Stand/built/STAND_GEOMETRY_RH.csv`.
   - Replace `reference_003/` with the new scene and datums first.
   - The script fails loudly if the cameras, the board or the frame part are missing.

### 5.3 Traps

- **The engine hashes its sources.**
  - **At start** it hashes every module it loads, including the workbench's own. That set is its
    source identity, which keys cached results and the studies' bindings.
  - **It refuses to start** if the historical coaxial source folder differs from its saved
    snapshot, or if a pinned input fails its hash: the stress bank, the controller profiles, the
    CAD source bodies.
  - **During a session**, a changed file stops it with "Historical source changed".
  - **Never let line endings change** in `optimizer/`.
  - Faceplate's convention: copy into a new dated folder, change the copy, never edit old
    evidence.
- **Builders and searches are create-once.**
  - They refuse an existing output folder.
  - `floating_components_20261002\build.py` rebuilds only weighted020: it hash-checks the
    four-piece delivery it reads. It and the printed-part builders stay in the original
    folder. For a new scene, use the workbench export (step 3).
- **Use the bundled Python:** `optimizer\tools\design_workbench\.venv\Scripts\python.exe`.
  Its `pip.exe`-style launchers were removed from the copy, so use `python.exe -m pip`.
- **Neither folder is under version control.** `Faceplate_Design` is git-ready
  (`.gitattributes`, `.gitignore`); its CAD wants Git LFS. The original Faceplate folder is 36 GB,
  20 GB of it JSON evidence, and only this PC and the 2026-09-03 zips hold it.

---

## 6. Files

**In this folder (in git):**

| Path | What |
|---|---|
| [`reference_003/SCENE.json`](reference_003/SCENE.json) | Byte-exact copy. SHA256 `cae02cdb…3a41cb` |
| [`reference_003/COMPONENT_DATUMS_MM.csv`](reference_003/COMPONENT_DATUMS_MM.csv) | Byte-exact copy, datums in CHASSIS, RH and LH. SHA256 `d927fe3b…c83177d` |
| [`built/STAND_GEOMETRY_RH.csv`](built/STAND_GEOMETRY_RH.csv) | The §1.2 positions, written by the tool |
| [`tools/stand_geometry.py`](tools/stand_geometry.py) | Reads the assembly STEP and the board; prints the §3 checks and the §1.2/§4.2 numbers |

`reference_003/.gitattributes` keeps the two copies byte-exact.

**Outside git, in `Github-Personal\Faceplate\Faceplate_Design\CAD\`** (2026-11-19). Each folder
there has a `MANIFEST_*.csv` with every file's SHA-256. The large files cannot go into this GitHub
repo as they are.

- **`CAD\PitracStand\` is a snapshot** of `Downloads\PitracStand` (2026-11-19). Your working
  files are still in Downloads.
- **SolidWorks stores absolute part paths.** Use Pack and Go before working from the snapshot.

| File | Size | SHA-256 | Note |
|---|---:|---|---|
| `PitracStand\FullStand_STEP\FullStand.STEP` | 171.7 MB | `a13ef27a…1dab5aa` | the assembly, 2026-11-18 09:18. Over GitHub's 100 MB limit |
| `PitracStand\FullStand.SLDASM` | 141.0 MB | in the manifest | 130 MB of it is the Pi 5 model |
| `PitracStand\*.SLDPRT`, `*.STEP`, `FOV references\` | 0.03–0.4 MB each | in the manifest | `Base_V2`, `LensHolderBase` and `PhotodiodeShroud` STEPs predate their SLDPRT saves of 2026-11-17. `Lens_LED` and `Lens_Photodiode` have no STEP. Re-export before relying on them |
| `reference_003\FLOATING_COMPONENTS_{CHASSIS,RH,LH}.step` | 33 MB each | in the manifest, and its own `OUTPUT_MANIFEST.json` | the components at their operating poses |
| the same folder, `FLOATING_COMPONENTS_RH*.SLDPRT`, `*Mount_*.SLDPRT`, `BaseStand.SLDPRT` | 0.06–8.3 MB | in the manifest | the owner's SolidWorks work, kept beside the reference |
| `PitracStand\Images\NextGenPiTrac.{png,gif,mp4}` | 0.4–1.2 MB | in the manifest | renders, 2026-11-19 |
| `Enclosure_Cad_2026-10-05\` | 5.0 MB in all | in the manifest | the first SolidWorks stand parts, as STEP and 3MF |
