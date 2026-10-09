# Point and image coordinate contract

[Entrypoint](README.md) | [Procedure](DISTORTION_PROCEDURE.md)

## Native sensor plane

- Native image size is **1600 columns x 1400 rows**; arrays are indexed
  `image[y, x]`, whereas point pairs are ordered `(x, y)`.
- Origin is the center of the top-left pixel, `(0, 0)`. x increases rightward and
  y downward. Integer centers span x = 0..1599 and y = 0..1399.
- Detection/refinement coordinates can be fractional. Intrinsics, residuals,
  pupil points, and coverage use this native sensor plane, not the display plane.
- Both session and result use `coordinate_orientation = "native"`. An
  upside-down scene does not change this contract.
- RAW12 FourCC is `Y12P`. Archived PNG samples are right-aligned 0..4095 in a
  16-bit container. The fixed 8-bit detection/viewing derivative is
  `uint8 = uint16 >> 4`; retain originals for provenance and float32 refinement.

## Display transforms are not calibration transforms

A UI may rotate or scale the image. Store native points, or invert every display
transform before storing a point. For a 180-degree display rotation:

```text
x_native = 1599 - x_display
y_native = 1399 - y_display
```

This assumes unscaled integer-center coordinates and no display crop; apply
inverse scaling/crop transforms first when present. Do not rotate a stored image
and still use native K, or fit K to displayed coordinates and call it native.
Spatial zones in the pose CSV also refer to native coordinates.

## Model and units

Standard OpenCV pinhole intrinsics:

```text
K = [ fx   0  cx ]
    [  0  fy  cy ]
    [  0   0   1 ]
distortion = [k1, k2, p1, p2, k3]
```

`fx`, `fy`, `cx`, and `cy` are in native pixels, with positive `fx`/`fy`.
The five Brown-Conrady coefficients are dimensionless. Board object coordinates
and board-pose translations use millimeters; angles and rotations must have their
method-specific convention recorded in evidence. The board's x/y pitch
measurements refer to its printed square-grid axes, not the camera sensor axes.
OpenCV ChArUco corner IDs/object coordinates come from the exact generated
definition and non-legacy layout; do not reconstruct IDs from apparent position.

For ideal normalized coordinates `(xn, yn)`, define
`r2 = xn*xn + yn*yn` and `radial = 1 + k1*r2 + k2*r2^2 + k3*r2^3`:

```text
xd = xn*radial + 2*p1*xn*yn + p2*(r2 + 2*xn*xn)
yd = yn*radial + p1*(r2 + 2*yn*yn) + 2*p2*xn*yn
u_distorted = fx*xd + cx
v_distorted = fy*yd + cy
```

This is the forward ideal-to-distorted model, not an instruction to subtract the
same expressions from measured pixels. Use OpenCV's inverse point-undistortion
operation with the ordered coefficients for measured points.

## Point-space state and undistortion

| State | Meaning | Units |
| --- | --- | --- |
| `native-distorted-pixels` | Original sensor/detected coordinates; result input convention | native px |
| Ideal pixels, P = K | Point-undistorted coordinates in the ideal camera plane using native K | native px |
| Normalized rays, no P | Undistorted `(xn, yn)`; ray proportional to `(xn, yn, 1)` | dimensionless |
| Display/resampled pixels | Coordinates after display rotation, scaling, new matrix, or crop | display-specific |

Specify which state each downstream dataset occupies, along with the calibration
ID and hash. Never pass ideal points to point undistortion a second time.
Normalized coordinates are not native pixel rows; map through K if an analysis
requires an ideal-pixel common row.

Correct both coordinates first:

```text
native pupil points (x, y)
    -> point-undistort both x and y
    -> re-fit centerlines in one declared ideal point space
    -> evaluate the common row in that same space
    -> perform separately qualified geometry/extrinsic analysis
```

Do not correct x alone while retaining the old y/row assignment, apply an x
offset to an already fitted pupil centerline, or reuse the old roughly 14 mm
scan result as qualified after calibration.

## Resampled images and errors

Point correction is preferred. Full-image remapping is only a display derivative
in this workflow. Retain the original and record the new camera matrix P,
destination size, crop offset/origin transform, interpolation, and valid-pixel
mask with the derivative. A crop changes the effective principal point; black
padding or interpolation output outside the valid mask is not measurement data.

Reprojection residuals compare prediction and observation in the **native
distorted image plane** with K/distortion held fixed on validation poses.
Report Euclidean RMS as `sqrt(mean(dx*dx + dy*dy))` in native px, maximum residual,
and per-pose/spatial results. Straight-line before/after checks use native input
and ideal pixels with P = K so their errors retain comparable native-pixel units;
record their line-fitting method separately.

These image coordinates, intrinsics, and distortion do not encode PCB/pupil
offsets, handed dock poses, or enclosure extrinsics. Measure mounting extrinsics
later without exchanging physical CAM0/CAM1 identities.
