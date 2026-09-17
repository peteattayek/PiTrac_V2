<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Tested Pi 5 camera setup

See [README.md](README.md) for installation, rollback, raw-data interpretation
and NIR comparison limitations. These instructions describe the inspected Pi;
do not apply its partition identifiers to a different machine.

## Verified result

On 2026-09-17, kernel `6.18.50+rpt-rpi-2712`:

| Camera | Port | Format | Measured rate | Retained frames |
|---|---|---|---:|---:|
| Mira220 mono | CAM/DISP1 | 1600 x 1400, GREY / RAW8 | 89.079939 fps | 5435 |
| IMX296 mono | CAM/DISP0 | 1456 x 1088, Y10P / RAW10 | 60.374742 fps | 3684 |

The streams overlap for **60.758522 seconds**, with no sequence gaps,
error-marked buffers, or out-of-range frame intervals. File sizes match the
retained frames and negotiated strides, and data was flushed. The extra frames
are the recorder's one-second guard, not cropped to exactly 60 seconds.

Both use nominal unity analogue gain and a 1000 us exposure request. Calculated
integration is 999.761667 us for Mira220 and 1006.852593 us for IMX296; this
7.090926 us difference is quantization/modelled timing, not an optical measurement.

The SSD measured **397.022302 MB/s** against **319.355995 MB/s** of camera data.
The operator explicitly approved `--min-headroom-percent 20`; actual headroom
is 24.319665%, below the default 25%. Accordingly the result is labelled
`VERIFIED_RECORDING_REDUCED_HEADROOM`, not default-headroom qualification.

## Current Pi state

- Boot camera entries are `camera_auto_detect=0`, `dtoverlay=imx296,cam0`,
  and `dtoverlay=mira220-nir`. Cables were not moved.
- The old `pitrac-web.service` is disabled. Do not restart the old camera stack
  against this comparison configuration without reviewing its assumptions.
- PipeWire/WirePlumber and their sockets are temporarily runtime-masked, at the
  operator's request, to leave the cameras available. These masks clear on reboot.
- The existing NVMe ext4 partition is mounted at `/mnt/pitrac-nvme` with
  `nosuid,nodev,noexec`. It was not formatted. No persistent mount was added.
- SD and NVMe filesystem UUIDs are duplicates. Use the NVMe's distinct
  **PARTUUID `0bdd0a02-02`**, not its filesystem UUID. SD root is `9bb5aaca-02`.

The accepted recording is on the Pi at:

```text
/mnt/pitrac-nvme/camera-comparison-20260917-8e2d5881/dual-disk-60s-buffered-01/
```

It contains `mira220.raw` (12,174,400,000 bytes), `imx296.raw`
(7,310,942,208 bytes), per-frame TSV files, controls, logs and verification
results. They are native raw streams, not MP4 files. Earlier failed trial
directories remain explicitly invalid; do not use them as accepted results.

## Record another run in the current boot

Run in a Bash terminal on the Pi as the normal `pitrac` user:

```bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd "$HOME/PiTrac_V2/Software/camera-comparison"
findmnt /mnt/pitrac-nvme

OUTPUT="/mnt/pitrac-nvme/camera-comparison-20260917-8e2d5881/run-$(date +%Y%m%d-%H%M%S)"
bash scripts/record-dual.sh \
  --config "$HOME/camera-comparison-evidence-20260917-agent-8e2d5881/config-both-01" \
  --output "$OUTPUT" --seconds 60 --buffers 32 \
  --storage-report /mnt/pitrac-nvme/camera-comparison-20260917-8e2d5881/storage-02/storage.tsv \
  --min-headroom-percent 20
```

Check that `findmnt` identifies `/dev/nvme0n1p2` before proceeding. The output
directory must not already exist. The configuration/report above are tied to
the recorded boot; do not reuse them after a reboot.

For a different exposure, configure both cameras into a new directory, changing
the example's `1000` to a supported microsecond value:

```bash
NEW_CONFIG="$HOME/camera-config-$(date +%Y%m%d-%H%M%S)"
bash scripts/configure-cameras.sh --output "$NEW_CONFIG" \
  --camera both --exposure-us 1000 --gain 1 --apply
```

Pass that directory to the recorder. Do not edit a saved configuration TSV
to bypass timing or identity checks.

## After a reboot

1. Confirm the running kernel and `install-mira220.sh --check`.
2. Confirm the SSD device identity. If it is unmounted, mount its existing
   partition, without formatting:

   ```bash
   sudo mkdir -p /mnt/pitrac-nvme
   sudo mount -t ext4 -o rw,nosuid,nodev,noexec \
     /dev/disk/by-partuuid/0bdd0a02-02 /mnt/pitrac-nvme
   ```

   Do not mount over an existing different filesystem. The initial filesystem
   journal recovery was separately approved; inspect new filesystem errors
   rather than forcing a repair.
3. Pause desktop camera discovery if it has resumed:

   ```bash
   export XDG_RUNTIME_DIR="/run/user/$(id -u)"
   export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
   systemctl --user mask --runtime --now \
     wireplumber.service pipewire.service pipewire.socket \
     pipewire-pulse.service pipewire-pulse.socket
   ```

   This can interrupt Pi audio/screen sharing, but not SSH.
4. From the camera-comparison checkout, create fresh evidence and output
   directories under the existing user-owned capture parent:

   ```bash
   export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
   cd "$HOME/PiTrac_V2/Software/camera-comparison"
   STAMP="$(date +%Y%m%d-%H%M%S)"
   EVIDENCE="$HOME/camera-comparison-evidence-$STAMP"
   NVME_RUNS="/mnt/pitrac-nvme/camera-comparison-20260917-8e2d5881/$STAMP"
   mkdir -m 0700 -- "$EVIDENCE" "$NVME_RUNS"

   bash scripts/configure-cameras.sh --output "$EVIDENCE/config-both" \
     --camera both --exposure-us 1000 --gain 1 --apply
   bash scripts/record-dual.sh --config "$EVIDENCE/config-both" \
     --output "$EVIDENCE/sink-both" --seconds 10 --sink
   bash scripts/benchmark-storage.sh --output "$NVME_RUNS/storage-test" --gib 24
   bash scripts/record-dual.sh --config "$EVIDENCE/config-both" \
     --output "$NVME_RUNS/dual-10s" --seconds 10 --buffers 32 \
     --storage-report "$NVME_RUNS/storage-test/storage.tsv" \
     --min-headroom-percent 20
   ```

   Stop at any failed command. Use these new same-boot reports, not the
   historical paths in the earlier example, and do not change boot IDs manually.
   After the ten-second trial passes, use a new output directory and
   `--seconds 60` for a full-duration run. See [the full guide](README.md) if the
   existing capture parent is unavailable or a different filesystem is being used.

## Restore desktop multimedia

When no raw capture is running:

```bash
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
systemctl --user unmask --runtime \
  wireplumber.service pipewire.service pipewire.socket \
  pipewire-pulse.service pipewire-pulse.socket
systemctl --user start \
  pipewire.socket pipewire-pulse.socket pipewire.service \
  pipewire-pulse.service wireplumber.service
```

This restores the services that were enabled and active before testing. They
may claim camera subdevices again, in which case pause them before raw capture.
The original camera boot configuration is backed up at
`/boot/firmware/config.txt.before-pitrac-camera-8e2d5881`. Restoring the old
PiTrac application/configuration is a separate rollback, not this audio restore.
