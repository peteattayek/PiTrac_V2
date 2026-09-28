<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Raw-camera / light-sheet composite

## Direct 60 fps Mira220-first presentation

For a Mira220-only recording, use `conversion.beam_mira60`. Its panel order is
**Mira220, trailing 1-second detail, full-video overview**. The selected native
frame's timestamp is at the detail plot's right edge; the overview's moving
dotted boundaries show that same window. Before the first full second, the
left boundary is outside the video overview and is clipped, while the detail
can still show actual preceding analyzer data.

This renderer selects the nearest real camera frame to each 60 Hz sampling
time. No camera images are blended or repeated. It writes and verifies exactly
60 encoded frames per second, rather than converting a higher-rate composite
afterward. A sampling phase changes which native frames are retained, not the
playback speed or analyzer alignment. The plots follow the selected raw frame
time, including its small deviation from the nominal 60 Hz sampling time.
Frame numbers are zero-based. Brightness remains native and linear; the
lossy, resized MP4 is a viewing copy, not lossless measurement data.

The approved **SecondHits** alignment uses the later September 24 take,
`mira220-2ms-20260924-154552/raw-30s`, with 2 ms requested exposure:

| Mira frame | Mira elapsed seconds | Analyzer peak-bin seconds |
|---:|---:|---:|
| 817 | 9.171171 | 12.059241922 |
| 1432 | 16.074811 | 18.959841922 |
| 2046 | 22.967230 | 25.854391922 |

The median analyzer-minus-Mira offset is **+2.887161922 s**; three later
object/club crossings corroborate it. No clock-drift correction is justified
by these frame-quantized observations. Residuals below one frame do not
establish millisecond optical synchronization. This offset must not be
replaced by the older FirstHits value below.

The 1 MS/s SecondHits CSV covers the full 30-second video and preceding
1-second trace window. Use the prepared SecondHits min/max cache:

```bash
python3 -m conversion.beam_mira60 \
  --run /path/to/mira220-2ms-20260924-154552/raw-30s \
  --cache /path/to/secondhits-plot-cache.npz \
  --output /path/to/new-composite-directory \
  --offset-seconds 2.887161922 \
  --seconds 30 --width 1280 --sampling-phase-ms 3
```

The 3 ms sampling phase retains all six reviewed crossing frames in this take.
The default is zero; neither this phase nor the alignment offset should be
assumed correct for a different recording.

The new output directory contains `beam-composite-60fps.mp4`,
`composite-60fps.json`, `frame-map.tsv`, a first-frame layout preview, and
`status` (`VERIFIED_COMPOSITE` only after validation). The map records each
output time, nominal sampling time, selected native index/timestamp, exact
trace window, and the selected raw-buffer SHA-256. Source raw files, CSV and
SAL archives are not modified.

### Fixed voltage axis

`conversion.beam_mira60` accepts `--voltage-max 5` to put **5 V at the top**
of both the detail and overview plots. Without this option, the upper limit
continues to follow the trace maximum. The lower limit remains -0.1 V.
Values outside fixed plot bounds are clipped only when drawn; the trace cache
and actual voltages are unchanged. In particular, a separately prepared `x2`
voltage cache remains doubled, even if values above 5 V are not visible.

The shared `Layout(..., voltage_max=5)` option provides the same bounds for
the full-frame slow-motion renderers. It does not affect camera frames, plot
timestamps, playback rates, or the moving overview window.

## Optional display-only deflickering

Both `conversion.beam_mira60` and `conversion.beam_slow_motion` accept
`--deflicker-model /path/to/illumination-model.npz`. The default remains
uncorrected native brightness. Use a new output directory for corrected copies.

SecondHits has a strong approximately 30.92 Hz sampled lighting component,
consistent with 120 Hz illumination at the approximately 89.08 fps camera rate.
Its model uses exact native 8x8 block means, a stationary right-wall reference,
and robust nonnegative spatial illumination-response fitting. Calibration
intervals exclude all three slow-motion hit windows.
The response field is spatially smoothed so it does not imprint fine outlines
of moving objects. Fixed spatial dithering below one native level avoids
quantization contours in the dark image; it adds no temporal random noise and
leaves pixels unchanged whenever the correction is zero.

The correction adds the estimated missing illumination, or subtracts excess
illumination, relative to the reference's mean brightness. This is a small
spatially varying additive correction, not a whole-image gain adjustment that
would scale genuine light-sheet flashes. It does not average, blend, drop, or
retime camera frames. The waveform panels and their time mappings are unchanged.

The model is bound to the source timestamps, dimensions, raw size and mtime.
Incompatible, nonfinite, or excessive corrections are rejected. Corrected
videos are labelled `deflickered`; their metadata records the model hash and
correction policy. The raw buffers and original videos are not modified.

Corrected pixels are **viewing data, not quantitative raw measurements**.
Some residual flicker can remain on moving surfaces, and information lost
in dark or clipped frames cannot be recovered by brightness correction.

The four corrected SecondHits copies are in `SecondHits/deflickered`.
Decoded background flicker RMS in the held-out hit intervals decreased by
approximately 76-98% across the checked regions (median approximately 97%).
All 2,601 corrected frames were decoded and checked; each frame map is
byte-identical to its original counterpart. The per-video and combined
`verification.json` files retain the measurements and output hashes.

## Original paired-camera presentation

The composite renderer combines:

1. **IMX296 raw camera image at the top**
2. **Analog light-sheet waveform in the middle**, scrolling around a fixed red cursor
3. **Mira220 raw camera image at the bottom**

This is simultaneous vertical stacking, not end-to-end concatenation of clips.
The source MP4s are not decoded or re-encoded: images are read from the original
verified raw recordings with their original frame timestamps.

## Frame clock and interpolation

The default master clock is **Mira220's original approximately 89.08 fps
timestamps**, not a 30/60 fps output grid. All Mira220 frame updates in the
selected interval are retained. The MP4 uses a microsecond time base and the
renderer verifies every output PTS and duration.

IMX296's original approximately 60.38 fps frames are mapped to the same clock.
`--imx-resample blend` performs timestamp-weighted **linear temporal blending**
of the two neighboring raw-derived images. This is explicitly labelled as
display-only in the video and can produce ghosting around moving objects.
It is not optical-flow motion synthesis or an additional camera measurement.
The frame map records both IMX296 source indices and the interpolation weight.

Use `--imx-resample hold` to display only the most recent actual IMX296 frame,
repeating it as needed, without synthesizing intermediate pixels. A separate
`--clock union` option can preserve every change from both camera timestamp
sequences, but produces a higher variable output rate; that is not the requested
Mira-rate presentation.

The rendered panels are aspect-preserving viewing resizes to a common width.
Mira GREY maps directly to 8-bit grayscale; IMX296 RAW10 maps `native >> 2`.
There is no per-frame brightness normalization. MP4 compression, viewing resize,
and IMX interpolation mean the composite is **not quantitative raw pixel data**.
Use the original frames for image-quality or NIR sensitivity measurements.

## Analyzer data

The inspected source is `analog.csv` in the user's `BeamTiming/FirstHits`
directory:

- one analog channel, **Channel 3**, in volts;
- **30,003,188 samples** at **1 MS/s**;
- timestamps from **0.000066922 to 30.003253922 seconds**;
- source voltage range observed: approximately **-0.020 to 5.186 V**.

The neighboring Saleae `.sal` archive's metadata identifies a **50 MS/s analog
acquisition**. This rendering uses the supplied 1 MS/s CSV, not a re-export or
interpretation of Saleae's proprietary binary waveform format. The archive is
left untouched.

Preparation aggregates every CSV sample into **50-sample / 50 us min/max
bins**. Rendering combines all overlapping bins at each display column.
This preserves narrow peaks that simple point decimation might miss; exact
sample positions inside a bin are not retained by the visual envelope.
The plot window is **1 second** and the current time is always at its center.

## Event-based synchronization

There is no shared hardware trigger between the analyzer and cameras. The
initial rough estimate of a 4.8-second offset did not match the actual data.
The operator approved matching repeated events instead:

| Mira220 time since its first retained frame | Mira source frame index | Corresponding analyzer peak bin |
|---:|---:|---:|
| 5.051888 s | 450 | 7.815716922 s |
| 13.797278 s | 1229 | 16.562416922 s |
| 20.544366 s | 1830 | 23.310016922 s |

The median event offset is **2.765138922 seconds**:

```text
analyzer_time =
    (camera_timestamp_us - earliest_camera_first_timestamp_us) / 1,000,000
    + 2.765138922
```

The first/last event residuals are about -1.31/+0.512 ms using this reference.
That agreement does **not** establish sub-millisecond synchronization:
the cameras have roughly 10 ms integration, different frame/readout timing,
and the waveform peak locations are represented by 50 us bins.
The on-screen label and metadata identify the alignment as approximate.

The original **104.402 ms camera start offset** is accounted for using the
shared recorded timestamp domain. Output begins at the first actual Mira frame
for which both cameras have coverage. Individual camera times shown in labels
remain relative to their own recording starts.

`--stop-at-trace-end` cuts the composite when the CSV ends, so this run is
approximately **27.13 seconds**, including every identified event but excluding
the empty tail. The right half of the scrolling window can still extend beyond
the available trace near the end; that future-data region is shaded, never
invented as zeros.

## Commands

The optional dependencies are declared in
[conversion/requirements-beam.txt](conversion/requirements-beam.txt). They are
not needed by raw capture. The inspected Pi already had PyAV, NumPy and Pillow;
the project development environment installed the optional requirements.

Prepare the compact waveform cache on a machine with access to the CSV:

```text
python -m conversion.beam_composite prepare \
  --csv /path/to/analog.csv \
  --cache /path/to/firsthits-plot-cache.npz
```

Run from the camera-comparison component directory. Copy the cache and its JSON
sidecar to the rendering machine; there is no need to transfer the large raw
camera files away from the Pi.

The inspected run was rendered on the Pi with:

```bash
cd "$HOME/PiTrac_V2/Software/camera-comparison"
python3 -m conversion.beam_composite render \
  --run /mnt/pitrac-nvme/capture-20260918-132610/video-30s \
  --cache "$HOME/.cache/beam-composite-20260918/firsthits-plot-cache.npz" \
  --output /mnt/pitrac-nvme/capture-20260918-132610/video-30s/beam-composite-firsthits-89fps \
  --offset-seconds 2.765138922 \
  --seconds 30 --window-seconds 1 --width 1280 \
  --clock mira220 --imx-resample blend --stop-at-trace-end
```

Use a **new output directory** for another attempt; existing results are not
overwritten. The output contains:

- `beam-composite.mp4`: the stacked presentation;
- `frame-map.tsv`: every output timestamp, analyzer time, source-frame indices
  and IMX interpolation weight;
- `composite.json`: alignment, source provenance, frame clock, interpolation
  policy, coverage and output checksums;
- `layout-first-frame.jpg`: a layout preview;
- `status`: must be `VERIFIED_COMPOSITE` before treating the result as complete.

### Mira220 with full-capture overview

Add `--overview --mira-only` to render three panels, from top to bottom:
the full synchronized capture, the centered 1-second detail plot, and Mira220.
The overview shows the complete waveform throughout playback, with a moving red
time cursor and dotted boundaries at the exact start/end of the detail window.
The selected interval is lightly shaded. Boundaries outside the overview range
are clipped at the beginning/end of playback.

The overview axis is elapsed video time; the detail axis is analyzer time.
Both use the same saved event alignment. `--mira-only` hides the IMX296 panel;
it keeps the existing common camera interval, so the 27.125857-second video and
all 2,417 original Mira220 frame timestamps match the earlier composite.
The paired capture metadata is still required. IMX296 columns in the frame map
are empty because no IMX296 images are displayed or interpolated.

The real-time overview version uses the command above with those two flags and
output directory `beam-composite-firsthits-mira-overview-89fps`. Its dimensions
are 1280 x 1802. The local deliverable directory is
`C:\Users\ATTAYEKP\Downloads\BeamTiming\FirstHits\composite-mira-overview-89fps`.

Add `--hide-detail-cursor` to omit the center line from the 1-second plot while
keeping the overview cursor and dotted boundaries. The current time remains
centered in the detail window. This variant is saved locally in
`composite-mira-overview-89fps-no-center-line`, alongside the earlier version.

The original raw files, analyzer CSV, Saleae archive and earlier MP4s are never
modified. A superseded first render used the union clock before the final
Mira-rate requirement; it is explicitly marked superseded and is not the
deliverable.

## Tests

```bash
python3 -m unittest conversion.test_beam_composite
```

Tests cover master/union timestamp selection, interpolation weights, raw packing
and padding, peak-preserving aggregation, missing-data regions, and rejection of
sample gaps or malformed caches. The renderer additionally verifies the encoded
MP4's exact packet PTS/durations, dimensions, count and total duration.

### Oscilloscope-style window and 60 fps delivery

Use `--window-position right --hide-detail-cursor --overview --mira-only`
with the same source run and alignment to show `[frame time - 1 s, frame time]`.
The current camera frame aligns with the right edge. Both dotted overview
boundaries use those same limits; the small red current-time marker sits outside
the waveform area so it does not obscure the right dotted boundary.

Render the synchronized native-rate composite first, then convert complete
composite frames with FFmpeg `-vf fps=60 -an -c:v libx264 -threads 3
-preset veryfast -crf 18 -pix_fmt yuv420p -movflags +faststart`.
This drops/repeats entire composite frames without blending, preserving the
camera-to-trace relationship within each frame. It keeps real-time playback;
the final duration is rounded to a whole 60 fps frame. Native source-frame
mapping and metadata describe the intermediate, not the resampled delivery.
The final MP4 and its verification metadata are saved locally in
`C:\Users\ATTAYEKP\Downloads\BeamTiming\FirstHits\composite-mira-scope-60fps`.

### SecondHits: every native frame at 10 fps

The three ball-hit clips each cover three seconds of original camera time,
centered on the approved crossing frame. Each contains **267 consecutive raw
frames**, shown once each at **10 fps** for **26.7 seconds** of playback
(approximately **8.908x slow motion**).

| Hit | Original camera interval [begin, end) | Raw frame indices |
|---|---|---|
| 1 | 7.671171 to 10.671171 s | 684 through 950 |
| 2 | 14.574811 to 17.574811 s | 1299 through 1565 |
| 3 | 21.467230 to 24.467230 s | 1913 through 2179 |

Use the SecondHits `composite-60fps.json` as the timing reference, not its
resampled MP4 as the image source. The raw recording supplies every original
frame, including frames omitted from that MP4. The reference's 3 ms sampling
phase is not applied to these consecutive-frame clips.

```bash
python3 -m conversion.beam_slow_motion \
  --run /path/to/mira220-2ms-20260924-154552/raw-30s \
  --cache /path/to/secondhits-plot-cache.npz \
  --reference /path/to/composite-mira-scope-60fps/composite-60fps.json \
  --output /path/to/new-hit-1-directory \
  --begin 7.671171 --end 10.671171 \
  --raw-playback-fps 10 --output-fps 10 --window-seconds 0.1 --mira-first
```

Repeat with the other intervals and new output directories. Mira220 stays on
top with native brightness. The middle plot shows the preceding **100 ms of
original capture time**, ending at the displayed raw frame. The bottom plot
covers only that clip's three-second source interval and uses original camera
seconds, not slowed playback seconds. Both plots advance with each native
frame. Dotted overview boundaries identify the detail window.

The result is `beam-slow-motion-10fps.mp4`, with source-frame hashes, frame map,
and `composite.json`. There is no frame omission, repetition, blending, or
intermediate plot clock in this mode. Existing 30 fps repeated-frame behavior
remains the default unless `--output-fps 10` is specified.

### FirstHits slow motion: original video 13?14.5 seconds

`python3 -m conversion.beam_slow_motion --run RUN --cache CACHE --reference
REFERENCE_COMPOSITE_JSON --output NEW_DIRECTORY` selects every Mira220 raw frame
in original composite playback seconds `[13, 14.5)`. `--begin` and `--end` can
select another interval. The reference JSON supplies the original video origin
and saved analyzer alignment.

For FirstHits this selects all 134 raw frames, indices 1168?1301. Each raw camera
image appears for three output frames: 10 raw frames per playback second, 30 fps
MP4, 402 output frames and 13.4 seconds total. There is no camera interpolation.
Both plots update on **every output frame**, using recorded analyzer data at
one-third intervals between consecutive raw timestamps. During the two repeated
camera images the plot therefore advances past that held image's timestamp.
The last interval ends at the requested clip boundary.

The overview stays fixed at original video seconds 13?14.5, with moving dotted
boundaries for the trailing 100 ms detail plot. The center line stays hidden.
`frame-map.tsv` records each output frame, held raw-frame index, original raw
video time, advancing plot time, and exact analyzer window bounds. Metadata
includes raw frame hashes, output hashes, alignment, and clock policies.
The local result is in `FirstHits/slow-motion-13-14p5-30fps`.
