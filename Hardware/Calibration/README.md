# Camera distortion calibration

This package provides printable targets, target regeneration, capture and record
validation tools, and the offline procedure and contracts for separately
calibrating the physical CAM0 and CAM1 Mira220 + Spinel ND3630 stacks. It contains
no measured calibration, fitting engine, live undistortion, or hardware-control
changes. A schema-valid record is not proof of physical calibration quality.

## Start here

1. Read the [full procedure](DISTORTION_PROCEDURE.md), including the safety,
   target-metrology, holdout, and acceptance gates.
2. Use the [native point conventions](POINT_COORDINATES.md) throughout capture,
   fitting, and subsequent pupil analysis.
3. Print the supplied [A4 PDF](targets/charuco-a4.pdf) or
   [Letter PDF](targets/charuco-letter.pdf) at 100% actual size. Printing requires
   no Python setup. Inspect the definition and physically verify the print before
   using it.
4. Copy the appropriate session template into a new `runs/CAM0/` or `runs/CAM1/`
   directory. Fill the identity, actual exposure, illumination, print
   measurements, and verification fields before changing `status` to `ready`.
5. Capture the [pose plan](templates/capture-poses.csv) for each physical stack:
   30 distinct fit poses and 10 new validation poses, three stationary frames
   each. Keep repeats together in their pose's split.
6. Analyze measured images separately using OpenCV. Start from the
   [unmeasured result template](templates/calibration-result.json), and populate
   the measured result and evidence only when available.

## Records

| File | Purpose |
| --- | --- |
| [CAM0 session](templates/camera-session-CAM0.json) | Draft capture manifest for the physical CAM0 stack |
| [CAM1 session](templates/camera-session-CAM1.json) | Draft capture manifest for the physical CAM1 stack |
| [Result template](templates/calibration-result.json) | Unmeasured result; copy and adjust identity for CAM1 |
| [Session schema](schemas/session.schema.json) | Draft 2020-12 capture-helper contract |
| [Result schema](schemas/result.schema.json) | Draft 2020-12 result, method, provenance, and evidence contract |

The session schema name is `pitrac-camera-distortion-session-v1`; its statuses are
`draft`, `ready`, `capturing`, and `complete`. The result schema name is
`pitrac-camera-distortion-result-v1`; its statuses are `unmeasured`, `fitted`, and
`validated`. Unknown object properties are rejected. Do not add manifest fields
ad hoc: coordinate a versioned contract change with the tool owner.

`runs/` is ignored locally because it will contain measurement records, original
image archives, and analyst evidence. Retain and back up these records outside
Git; ignoring them does not constitute archival storage.

## Printable target

Open the [printable index](index.html), [A4 PDF](targets/charuco-a4.pdf), or
[Letter PDF](targets/charuco-letter.pdf) without any Python setup. The verified
OpenCV 4.13 board has 44 markers and 70 ChArUco corners. Its
[definition](targets/board.json), [A4 SVG](targets/charuco-a4.svg),
[Letter SVG](targets/charuco-letter.svg), and [raster](targets/board-raster.png)
are also supplied. Follow the procedure's print-scale, flatness, and 850 nm
contrast checks; generated files alone do not physically qualify a target.

## Tools and optional PC setup

Regeneration, capture, and record validation use the existing repository PC
virtual environment. If its dependencies are not already installed, optionally
install [requirements.txt](requirements.txt) into that environment from the
repository root in Windows PowerShell:

```powershell
& '.\.venv\Scripts\python.exe' -m pip install -r 'Hardware\Calibration\requirements.txt'
```

Then run these examples from `Hardware\Calibration`. Each command names the
existing environment's interpreter explicitly rather than relying on PATH:

```powershell
& '..\..\.venv\Scripts\python.exe' 'tools\generate_targets.py'
& '..\..\.venv\Scripts\python.exe' 'tools\capture_pose.py' --session 'runs\CAM0\session.json' --pose T01
& '..\..\.venv\Scripts\python.exe' 'tools\capture_pose.py' --session 'runs\CAM1\session.json' --pose V01
& '..\..\.venv\Scripts\python.exe' 'tools\validate_record.py' 'runs\CAM0\session.json'
& '..\..\.venv\Scripts\python.exe' 'tools\validate_record.py' 'templates\calibration-result.json'
```

The capture command accepts `--count 1..3`, defaulting to three frames. Re-running
a partial pose resumes its missing repeat numbers; a complete pose raises an
error and is never overwritten. Keep the same stationary placement when
completing repeats. After holdout-informed changes, use fresh validation pose IDs
such as V11..V20; never reuse the consumed validation images.

The record validator takes a positional record path. Substitute the actual
measured result path when available; validating the unmeasured template does not
validate a physical camera. Record checks include date-time formats and the
matching schema's structural requirements.

Run the package's unit tests from the repository root:

```powershell
& '.\.venv\Scripts\python.exe' -m unittest discover -s 'Hardware\Calibration\tests'
```

These tools do not change hardware controls. No `calibrate.py`, fitting engine,
point-correction engine, or live-camera undistortion is provided; measured images
must be analyzed and physically qualified separately.
