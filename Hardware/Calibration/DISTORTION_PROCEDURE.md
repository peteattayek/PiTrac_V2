# Camera distortion procedure

[Entrypoint](README.md) | [Coordinate conventions](POINT_COORDINATES.md) |
[Pose plan](templates/capture-poses.csv)

## 1. Scope and non-claims

Calibrate each physical CAM0/CAM1 Mira220 + Spinel ND3630 optical stack
independently at its final operating focus, aperture, filter stack, and approved
diffuse 850 nm illumination. Distortion is an intrinsic optical mapping, not a
PCB/pupil offset, camera-to-enclosure transform, or final mounting extrinsic.

The starting operating-focus model references are CAM0 **390 mm** and CAM1
**388.245268774 mm**. These are model references, not measured focus authority,
distance setpoints, or proof of actual focus. Set and verify focus using the
physical stack, then lock it. Do not refocus between board poses or fit/validation
captures. Moving the board is allowed while it remains adequately sharp.

This procedure supplies no measured coefficients and makes no physical,
launch-monitor, or system-accuracy claim. The provisional heldout image-error
target is RMS <= 0.5 native pixels, preferably lower; it is not a millimeter
accuracy specification. Fitting and point correction will be performed separately
on future measured images using OpenCV.

## 2. Physical identity, optics, and safety

1. Label the module, lens, holder, and filter stack for the physical CAM0 or CAM1
   assembly. Record stable identifiers, the actual fixed aperture description,
   and the approved illumination identifier in its own session.
2. Manually confirm which physical role is connected. The current single-Mira
   preview alias is `mira220`; neither this alias, a preview slot, a CSI socket,
   nor an upside-down display establishes CAM0/CAM1 identity.
3. Use the final approved diffuse 850 nm illumination and final filter stack.
   Check sharpness and target contrast at 850 nm, not merely under visible light.
   Keep that optical configuration unchanged during the session.
4. Lock focus and set `identity_confirmed` and `focus_locked` only after the
   physical checks. Record the actual existing exposure in microseconds
   (30..500000), with gain 1. Choose an adequately exposed, sharp, unsaturated
   configuration before capture; the helper never changes it.
5. Do not hot-swap CSI hardware. If changing physical cameras requires a cable
   change, shut down and remove power before making it. Re-establish and confirm
   identity after restarting.

Do not introduce strobe/high-current operation, illumination-driver changes,
camera-control changes, or any hardware modifications as part of this work.
Final extrinsics are measured separately after mounting. Preserve physical CAM0
and CAM1 identities across handed dock poses; docking does not exchange labels.

## 3. Target authority and print metrology

Use the verified generated [definition](targets/board.json) for:

| Property | Required value |
| --- | --- |
| Board ID | `pitrac-charuco-11x8-20mm-v1` |
| Squares | 11 x 8 |
| OpenCV 4.13 layout counts | 44 markers and 70 ChArUco corners |
| Square pitch | 20 mm |
| Marker side | 15 mm |
| Dictionary | `DICT_5X5_250` |
| OpenCV layout | `legacyPattern = false` |
| Marker border | 1 bit |
| Active board size | 220 x 160 mm |
| White quiet border | At least 15 mm on every side |
| Paper and scale | A4 or Letter, landscape, 100% actual size |

The supplied PDFs need no Python setup to print. For optional regeneration using
verified OpenCV, follow the [PC setup](README.md#tools-and-optional-pc-setup) and
run from `Hardware\Calibration` in Windows PowerShell:

```powershell
& '..\..\.venv\Scripts\python.exe' 'tools\generate_targets.py'
```

Open the [printable index](index.html) or choose the correct paper-size
[A4 PDF](targets/charuco-a4.pdf) / [Letter PDF](targets/charuco-letter.pdf).
The package also supplies the [A4 SVG](targets/charuco-a4.svg),
[Letter SVG](targets/charuco-letter.svg), and [raster](targets/board-raster.png).
Disable fit-to-page, shrink, browser headers/footers, and other printer scaling.
A screen display or raster pixel count is not evidence of printed physical scale.

Before capture:

- Mount the print on a flat, rigid, matte backing. Check flatness across the
  usable area and retain the inspection evidence. A monitor or warped 3D print
  is not the target authority.
- Measure both independent 100 mm print bars: horizontal and vertical. Enter
  the actual measured lengths, in millimeters, in `print_scale_x_100_mm` and
  `print_scale_y_100_mm`, not a multiplier or the intended length.
- Measure several square pitches in both printed board axes, preferably using
  multi-square spans divided by their square count. Record actual mean pitches
  in `measured_square_x_mm` and `measured_square_y_mm`; retain instrument,
  locations, spread, and repeat measurements in the analyst evidence.
- Compare the bars, pitches, and their x/y agreement. Printer anisotropy,
  localized scaling, or board warp can masquerade as lens distortion. Stop and
  correct an unacceptable print/backing before fitting. Record metrology
  tolerances and decisions; mere positive numbers in a manifest are insufficient.
- Actually view the black/white target through the final filter and 850 nm
  optical stack. Confirm marker separation, contrast, lack of glare, and
  saturation using native samples. Set `ir_contrast_verified` only after this
  check; visible-light appearance alone is insufficient.

Use the 20 mm generated geometry only when physical measurements justify it.
If metrology identifies material x/y pitch differences, do not silently treat a
distorted print as a nominal board or absorb its shape into lens coefficients.
Prefer reprinting. Any analysis using justified measured object geometry must
record it explicitly in the result's `method.object_geometry_description` and
evidence, preserve the generated layout/hash, and demonstrate that board warp is
not the apparent distortion.

## 4. Session preparation

From the repository root in PowerShell, create a separate run directory and copy
the correct draft:

```powershell
Set-Location 'Hardware\Calibration'
New-Item -ItemType Directory -Force 'runs\CAM0' | Out-Null
Copy-Item 'templates\camera-session-CAM0.json' 'runs\CAM0\session.json'
```

Use CAM1's directory and template for its physical stack. Never overwrite an
existing measurement run; choose a new directory/session ID for another run.
Replace `CAM0_REPLACE_WITH_RUN_ID` or `CAM1_REPLACE_WITH_RUN_ID` with a unique,
role-prefixed ID, and set `created_utc` to the actual UTC creation time.

Fill all optical identities, actual exposure, illumination identity, and four
print measurements. Verify the four physical gates:
`identity_confirmed`, `focus_locked`, `flatness_verified`, and
`ir_contrast_verified`. Then change `status` to `ready` and validate the record.
The schema rejects incomplete readiness for `ready`, `capturing`, or `complete`.

`preview_session_id` is initially null. The helper may record the hardware
preview session identifier when selecting frames; do not invent it or interpret
it as a physical camera identity. Inspect any preview restart/identity mismatch
before continuing. `definition_file` is relative to this calibration workspace;
capture `archive` paths are relative to the directory containing `session.json`.

The schema keeps exactly the capture-helper fields. Do not add unrecognized
focus, controls, transformed-coordinate, or per-frame analysis fields to the
session. Keep richer observations in analyst evidence and the result contract.

## 5. Native capture and pose diversity

The native image is unrotated **1600 x 1400 RAW12**, FourCC **Y12P**.
The PNG in an archive is a 16-bit container holding right-aligned sensor samples
in **0..4095**. Do not left-shift samples, treat them as full-range 16-bit
intensities, demosaic them, or replace the originals with an 8-bit preview.
An image can appear upside-down. A display may rotate it, but stored points must
remain native x-right/y-down coordinates.

Use [capture-poses.csv](templates/capture-poses.csv) as the initial plan:

- T01..T30: 30 distinct **fit** plane poses, spanning center, sides, and all four
  corners.
- V01..V10: 10 **new validation** poses, reserved by whole pose.
- Three separate stationary frames at each pose: 90 fit plus 30 validation
  frames per physical camera, 120 total.

The CSV's tilts/rolls are placement hints, not measured pose authority. Pitch and
yaw magnitudes of 15..30 degrees and several rolls provide out-of-plane
diversity. Image zones refer to locations of useful detected corners in the
native image, not the displayed orientation. Large/medium/small suggest roughly
70..90%, 50..70%, and 35..50% of native image width, respectively, when achievable
without defocus or lost coverage. These are not distance/focus setpoints.

At corner and edge poses, move useful corners toward those regions. Partial
views are allowed if marker identities and enough spatially well-distributed
ChArUco corners remain reliable. A high count of tightly clustered or nearly
collinear corners is not adequate coverage. Keep points distributed in both
axes and inspect their detections. Reject blur, clipping, glare, unstable target
motion, suspect IDs, and poorly conditioned partial views; recapture them.

Do not substitute all-frontoparallel images, three pupil-style yaw images,
repeated identical board placements, or display rotations for plane-pose
diversity. Limited frontoparallel views are useful but must not dominate.

Use the existing local SSH tunnel and selected-camera capture API:

```powershell
& '..\..\.venv\Scripts\python.exe' 'tools\capture_pose.py' --session 'runs\CAM0\session.json' --pose T01
```

The default is three frames; `--count` accepts 1..3. Re-running a partial pose
resumes the missing repeat numbers explicitly. A complete pose raises an error
and never overwrites its records or archives. Keep the same stationary board
placement to complete repeats; a changed placement needs a new pose ID.
Confirm the selected frames have distinct frame/capture identities; re-saving one
frame three times is not three repeats. The ready-session helper accesses
`http://127.0.0.1:8765`, uses alias `mira220`, never alters controls, writes
per-frame ZIP archives in `images/`, retains RAW/PNG/metadata, hashes the ZIPs,
and appends capture records.

Each capture record has exactly `pose_id`, `repeat` (1..3), `split`,
`archive`, `archive_sha256`, `frame_id`, `capture_id`, and `selected_utc`.
T-prefixed poses belong to `fit`; V-prefixed poses belong to `validation`.
Paths use relative POSIX notation in JSON even on Windows. Preserve original
archives and hashes; analysis derivatives do not replace them.

Keep all three repeats of a pose in the same split. Fit only T poses; do not use
V images to initialize, fit, choose parameters, or tune acceptance criteria.
If validation motivates any model or threshold change, that validation set has
been consumed: capture at least 10 fresh holdout poses, with new V IDs such as
V11..V20. The helper accepts fresh V11-or-higher IDs as `validation`; never reuse
the consumed validation images. Preserve earlier records rather than relabeling
them. The schema permits additional T/V IDs; the CSV is the initial plan, not an
upper bound.

Mark the session `complete` only after checking the planned pose diversity,
three distinct repeats per pose, successful archival, and validation reservation.
Any excluded images and recaptures must remain auditable. Structural schema
validation alone cannot check these grouping and physical facts.

## 6. Detection and fitting, performed separately

1. Verify archive hashes and native sizes/ranges before decoding for analysis.
   Verify the board definition, dictionary, `legacyPattern = false`, marker
   border, print metrology, physical camera identity, and unchanged optical stack.
2. Make the fixed detection/viewing derivative
   `uint8 = uint16 >> 4`. No per-image normalization, clipping/stretching,
   histogram equalization, gamma, or adaptive intensity remapping substitutes
   for this defined derivative. Preserve the 12-bit originals.
3. Detect marker IDs and interpolate/refine native ChArUco corners using the
   exact generated layout. If refinement is performed in float32 native
   intensities, converting the original right-aligned samples directly to
   float32 retains all 12 bits. Document actual detector/refinement settings;
   never imply this future analysis engine already exists here.
4. Overlay detections in the correct native coordinates; inspect IDs and
   corner quality, including partial views and boundary coverage. Apply the
   predeclared rejection rules independently of residual-driven cherry-picking.
5. Fit initial `fx`, `fy`, `cx`, `cy` and the standard OpenCV five-coefficient
   Brown-Conrady model in order **[k1, k2, p1, p2, k3]**. Do not assume the image
   center is the measured principal point or manufacture coefficients from
   focus references. Record the actual initial matrix, OpenCV version/build,
   solver, calibration flags, object geometry, and residual definition.
6. Compute per-pose residuals as well as aggregate residuals; the three
   stationary repeats are not three independent board poses. Preserve rejected
   images and record the decision evidence.
7. Record a `fitted` result only after a genuine fit, including K, coefficients,
   fit metrics, source hashes, target hash, method metadata, and the fit report.

The v1 result contract intentionally fixes this initial model and coefficient
order. More complex distortion models require convincing fresh-holdout evidence,
not merely smaller fit residuals, and a separately agreed versioned result
contract. Do not squeeze rational/fisheye parameters into this five-element array.

## 7. Holdout evaluation and acceptance

Lock the model, detector/rejection rules, and acceptance criteria before
evaluating the reserved poses. For each heldout image, keep K/distortion fixed;
estimate only its nuisance board pose when computing reprojection errors. This
is not a refit of intrinsics to the holdout. Report the pose-estimation method and
limitations: reprojection error is an image-space consistency check, not external
metrology.

Evaluate, separately for CAM0 and CAM1:

- Heldout RMS and maximum Euclidean residual in native pixels, with per-pose
  values. The aggregate RMS is `sqrt(mean(dx*dx + dy*dy))` over accepted corners,
  not separate-axis RMS, displayed pixels, or an undocumented mean of pose RMS.
- Center and edge residual results, and a spatial residual map with its region
  definitions. Define center as the central half of image width and height;
  edge is its complement. State how boundary points are classified.
- Corner/edge/center coverage and a 4 x 4 native-image grid occupancy report.
  `coverage_fraction` is occupied cells / 16 for accepted heldout corners;
  occupancy by itself does not prove a well-conditioned calibration.
- Straight-line checks on heldout board rows/columns or another independently
  verified straight reference. Compare before/after ideal-pixel deviation from
  a best-fit line, at both center and edge, with source archive hashes. This
  check must not adjust the distortion model.
- Stability when refitting subsets of T poses only. Use genuinely different,
  adequately diverse subsets. Report maximum relative `fx`/`fy` change,
  principal-point displacement, and displacement of corrected test points on
  a fixed native grid. Record subset definitions and predeclared limits.

The provisional heldout RMS target is <= 0.5 native px, preferably lower.
Require acceptable spatial behavior, edge/center results, straight-line
behavior, coverage, and subset stability as well; do not declare success solely
from a low training or aggregate RMS. Explicitly record limits and the analyst's
acceptance rationale in the reports. The result schema enforces the provisional
RMS ceiling for `validated`, but does not invent unstated physical tolerances or
prove that other tests passed.

If results fail, investigate print scale/warp, contrast, clipping, focus/motion,
wrong board layout, coordinate rotation, pose diversity, and fit conditioning.
Retain failed evidence. After changes informed by holdout, capture fresh holdout
poses before making another validation claim.

## 8. Result record and provenance

Copy [calibration-result.json](templates/calibration-result.json). Adjust its
physical role/session/calibration identifiers; the template defaults to CAM0,
not to a measured result for either camera.

The template's `unmeasured` status has null measured K elements, distortion, and
errors. K's fixed zeros and final one are mathematical structure, not measured
defaults. Fixed model/layout/units/thresholds likewise are not measurements.

For `fitted`, populate measured intrinsics and fit fields, actual camera identity,
creation time, verified target measurements/hash, method metadata, source
archives/hashes, session-manifest path/hash, and a fit report. Populate
`fit.pose_ids` with at least 30 distinct T poses and source counts reflecting the
actual images. Initial K in method metadata is the solver's real seed, not an
assertion that it was measured.

For `validated`, additionally populate at least 10 distinct new V pose IDs,
heldout/center/edge metrics, coverage, per-pose residuals, straight-line checks,
subset-stability results, and all evidence paths. Confirm
`method.thresholds_locked_before_validation = true` and
`validation.holdout_is_fresh = true` only for the final independent holdout.
The result includes the session-manifest hash, generated target-definition hash,
and the original capture records as `provenance.source_images`. Freeze/hash the
session manifest after capture, rather than pointing at an actively changing
manifest.

Result evidence and session-manifest paths are relative POSIX paths from the
result file's directory. `provenance.source_images[].archive` retains its
session-manifest-relative meaning. `target.definition_file` remains relative
to the calibration workspace. Keep the result beside its session, or document
the relocation without altering these meanings or original archive hashes.

From `Hardware\Calibration`, validate a record using its positional path:

```powershell
& '..\..\.venv\Scripts\python.exe' 'tools\validate_record.py' 'runs\CAM0\session.json'
```

Use the actual result path in the same command when validating a result.
The schema checks shape, fixed conventions, metadata presence, and declared
status preconditions. The analyst must additionally verify files/hashes,
capture uniqueness, counts against arrays, complete repeat groups, pose-disjoint
splits, subset methods, actual evidence contents, target quality, and acceptance.
`validated` means documented analyst acceptance under this image-space procedure,
not certification of system accuracy.

## 9. Apply only in later, explicitly qualified analysis

Prefer point undistortion into ideal pixels (P = K) or normalized rays (no P),
using the same native K and ordered coefficients. Correct **both x and y** before
re-fitting pupil centerlines and evaluating the common row. Record point-space
state and never double-correct already ideal points.

After distortion calibration, reprocess the original pupil scan. The old roughly
14 mm result is not a qualified distortion-corrected measurement. Do not reuse
old fitted centerlines or silently treat distortion as a PCB/pupil offset.

Full-image resampling is for display only in this workflow. Record its new
camera matrix, crop/origin transform, interpolation, and valid-pixel mask; invalid
resampled borders are not measurements. Final enclosure/dock extrinsics follow
separately after mounting, with the physical camera identities unchanged.
