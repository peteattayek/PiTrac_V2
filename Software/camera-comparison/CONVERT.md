<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Timestamp-faithful MP4 viewing copies

For an IMX296 / scrolling light-sheet waveform / Mira220 stacked presentation
from raw frames, see [BEAM_COMPOSITE.md](BEAM_COMPOSITE.md).

The converter reads the recording's **per-frame V4L2 timestamps**, not a
rounded or average FPS. It assigns each retained source frame an exact MP4
presentation timestamp with a **1/1,000,000-second time base** and checks every
encoded timestamp and duration with `ffprobe`. Frames are not duplicated or
dropped to make a constant-rate video.

The raw files, capture logs, frame tables and configuration remain unchanged.
Native 12-bit Mira220 pupil captures are still-image inputs, not supported
recording inputs to this converter. Use the default 8-bit Mira220 profile for
video comparison; see [the pupil profile](FOCUS.md#optional-native-12-bit-mira220-pupil-profile).
Input must be a completed disk recording marked `VERIFIED_RECORDING` or
`VERIFIED_RECORDING_REDUCED_HEADROOM`. The converter checks the input manifest,
native file sizes, frame sequences, timestamps and original capture-log evidence
before creating a new output directory. Existing outputs are never overwritten.

## Prerequisites

Run on the Pi with Python 3.11+, PyAV and FFmpeg/ffprobe. The inspected Pi already
has `python3-av` 14.2.0 and FFmpeg 7.1.5, so no installation was necessary.
On another Pi OS installation, review the transaction before installing:

```bash
apt-get --simulate install --no-upgrade python3-av ffmpeg
# Only after reviewing the proposed changes:
sudo apt-get install --no-upgrade python3-av ffmpeg
```

PyAV uses the installed FFmpeg libraries and the software `libx264` encoder.
Conversion reads large raw files and uses CPU; run it after recording, not
alongside a bandwidth-critical capture. Two encoder threads are used by default.

## First 10 seconds of both cameras

In a **Bash terminal on the Pi**:

```bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd "$HOME/PiTrac_V2/Software/camera-comparison"

RUN="/mnt/pitrac-nvme/capture-20260918-132610/video-30s"
python3 -m conversion.convert --run "$RUN" --seconds 10
```

This exact recording was converted and verified. The resulting files are:

```text
/mnt/pitrac-nvme/capture-20260918-132610/video-30s/mp4-first-10s/
    mira220.mp4
    imx296.mp4
    mira220.timing.json
    imx296.timing.json
    conversion.json
    status
```

Both MP4s have **10.000000-second duration**:

| Camera | Dimensions | Frames | File bytes |
|---|---|---:|---:|
| Mira220 | 1600 x 1400 | 891 | 4,649,717 |
| IMX296 | 1456 x 1088 | 604 | 1,478,214 |

`status` must be `VERIFIED_TIMESTAMPED_MP4`. Interrupted/failed attempts retain
an incomplete or invalid status and diagnostic information; choose a different
new `--output` directory when retrying. Do not delete source recordings to free
space for the conversion.

To convert one camera, or another duration:

```bash
python3 -m conversion.convert --run "$RUN" --camera mira220 \
  --seconds 10 --output "$RUN/mira-timed-new"

python3 -m conversion.convert --run "$RUN" --camera imx296 \
  --seconds 10 --output "$RUN/imx-timed-new"
```

Omit `--seconds` to include all retained frames. `--crf 18` and
`--preset veryfast` are defaults; they affect viewing quality/encoding effort,
not source timestamps. `--threads` accepts 1..4.

## What “actual timing” means

- Each clip begins at that camera's first retained frame, after the recording's
  warm-up discard. Each frame's PTS is its recorded timestamp minus that first
  timestamp, computed with exact integer microseconds rather than floating-point
  averaging.
- Inter-frame display duration is the difference between adjacent source
  timestamps. A 10-second trim keeps frames whose timestamps fall within the
  first 10 seconds and shortens the final displayed frame to end exactly at
  10 seconds. It does not slow down, speed up, or interpolate the source.
- The original relative start time is retained as `original_start_offset_us`
  in each timing JSON. In the inspected run, IMX296 started **104,402 us
  (104.402 ms)** after Mira220. Since each standalone clip starts at zero,
  starting both players together does **not** reproduce that cross-camera
  alignment automatically; apply this offset in analysis or an editor.
- V4L2 timestamps in this recording are labelled `ts-src-eof`. They describe
  driver/capture timing, not a separately measured optical exposure-start time.
  The cameras were free-running, not hardware-synchronized.
- Without a requested trim there is no timestamp after the final source frame.
  Its display duration is estimated with the lower median of the recorded
  intervals and is explicitly labelled as an estimate in the JSON.
- The generated files are **variable-frame-rate** MP4s. Some players, editors
  and screens may display a rounded FPS or not render every high-rate frame.
  The converter verifies the stored timing, not a monitor's refresh behavior.

## Pixel handling and NIR measurements

Mira220 `GREY` rows are copied as 8-bit monochrome, excluding any row padding.
IMX296 `Y10P` rows are handled as MIPI RAW10, **four pixels per five bytes**.
For an 8-bit viewing copy, each pixel is its native 10-bit value shifted right
by two. The high-byte mapping is equivalent to unpacking then shifting; the low
two bits and row-padding bytes are not displayed.

There is no per-image auto-brightening, normalization, debayering, sharpening,
or denoising. However, H.264/YUV420 encoding is a **lossy 8-bit viewing
conversion** at the default CRF, not a replacement for the raw measurement data.
Retain native frames and metadata for noise, intensity, precision or NIR
sensitivity comparisons.

## Verification and tests

```bash
python3 -m unittest conversion.test_convert
```

Tests include exact decimal timestamp parsing, original-log cross-checks,
malformed-input rejection, RAW10 packing/padding, and a real irregular-timestamp
MP4 encode/ffprobe round trip. PyAV and ffprobe are required for the encode test.

To decode-check an output without accidentally rounding its VFR timestamps:

```bash
ffmpeg -v error -xerror -i "$RUN/mp4-first-10s/imx296.mp4" \
  -map 0:v:0 -fps_mode passthrough -enc_time_base 1:1000000 -f null -
```

The converter's timing reports include selected raw-prefix, source-metadata and
output-file SHA-256 hashes. These support reproducibility; they do not turn
lossy MP4 pixels into quantitative raw data.

## Individual raw frames from slow-motion playback

Raw recordings contain concatenated fixed-size frames without image headers.
Use the slow-motion frame map to find source indices, rather than multiplying
slow-motion seconds by capture FPS. Indices are zero-based.

On the Pi:

```bash
cd "$HOME/PiTrac_V2/Software/camera-comparison"
RUN=/mnt/pitrac-nvme/capture-20260918-132610/video-30s
python3 -m conversion.extract_slow_frames \
  --run "$RUN" \
  --slow-dir "$RUN/beam-slow-motion-13-14p5-30fps-final" \
  --start 6 --end 7 \
  --output "$RUN/raw-frames-slow-6-7s"
```

Use a new output directory when rerunning. The inclusive 6-7 second interval
selects Mira220 frames 1228-1238 (11) and nearest-timestamp IMX296 frames 826-833
(8). The cameras are free-running; mapping.tsv records timestamp differences.
Each camera frame is exported once, even if repeated in slow-motion playback.

Open PNG files for full-resolution viewing. Mira PNGs preserve 8-bit pixels;
IMX PNGs show native10bit >> 2. IMX native10bit.tiff preserves values 0-1023 in
16-bit grayscale, so a viewer may need a 0-1023 display range. Raw files retain
every source byte, including row padding.

For this run, frame byte offsets are index * 2240000 for Mira220 (1600 x 1400,
GREY, stride 1600) and index * 1984512 for IMX296 (1456 x 1088, Y10P, stride
1824). For other runs, read sizeimage and stride from the camera profile.

From Windows PowerShell:

```powershell
scp -r pitrac@pitrac.local:/mnt/pitrac-nvme/capture-20260918-132610/video-30s/raw-frames-slow-6-7s "C:\Users\ATTAYEKP\Downloads\BeamTiming\FirstHits\"
```

manifest.json records image layouts, selected indices, and SHA-256 checksums.
The extractor verifies PNG pixels and the lossless unpacking of RAW10 pixels.
