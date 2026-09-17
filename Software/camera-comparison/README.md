<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 PiTrac contributors -->

# Pi 5: Mira220 and IMX296 raw NIR comparison

This guide adds a standalone camera-comparison setup to an **existing 64-bit
Raspberry Pi OS Trixie installation on Raspberry Pi 5**. Keep that installation
and its current kernel until inspection establishes what is actually present.
The inspected target now boots `6.18.50+rpt-rpi-2712`; still check the running
kernel on each target rather than assuming it matches.

**Status: target validation pending.** The rates and exposure times below are
source-derived targets, not measurements. The pinned driver and overlay compiled
on the target's 6.18.50 kernel and produced a valid 23-character `srcversion`.
Installation stopped before copying driver files because an earlier installer
incorrectly required 24 characters. The corrected validator below still requires
a successful target installation. No successful Mira220 stream,
NVMe benchmark, or 60-second dual recording has been demonstrated here.
A [public Mira220 Pi 5/CFE streaming failure][mira-issue] remains open as of
2026-09-17. A compiled module, compiled overlay, successful probe, or
`CONFIGURED` message is not proof of capture.

## Contents

1. [What this setup records](#what-this-setup-records)
2. [Hardware and connection safety](#hardware-and-connection-safety)
3. [Inspect first; preserve the existing OS](#inspect-first-preserve-the-existing-os)
4. [Install only the required packages and matching headers](#install-only-the-required-packages-and-matching-headers)
5. [Install Mira220 and edit boot configuration manually](#install-mira220-and-edit-boot-configuration-manually)
6. [Choose an existing NVMe filesystem](#choose-an-existing-nvme-filesystem)
7. [Exposure, gain, and timing](#exposure-gain-and-timing)
8. [Staged bring-up and recording](#staged-bring-up-and-recording)
9. [Files, metadata, and verification](#files-metadata-and-verification)
10. [Interpreting native raw pixels](#interpreting-native-raw-pixels)
11. [A fair NIR comparison](#a-fair-nir-comparison)
12. [Troubleshooting and rollback](#troubleshooting-and-rollback)
13. [Validation status and sources](#validation-status-and-sources)

## What this setup records

| Camera | Physical Pi port | Full active image | Sensor bus format | Memory fourcc | Timing target | Unity analogue gain code |
|---|---|---|---|---|---|---|
| Mira220 EVM-SE mono | CAM/DISP0 | 1600 x 1400 | `Y8_1X8` | `GREY`, native RAW8 | about 89.080 fps, VBLANK 18 | `1` = fixed 1x |
| INNO-MAKER IMX296 mono | CAM/DISP1 | 1456 x 1088 | `Y10_1X10` | `Y10P`, native packed RAW10 | about 60.376 fps, VBLANK 30 | `0` = 0 dB = 1x |

Both cameras run continuously at their own rates. Their starts, frame numbers,
and exposure instants are **not synchronized**. Verification requires actual
retained timestamp overlap at least as long as the requested duration; it does
not establish simultaneous exposure.

Capture goes directly through V4L2 and the RP1 CFE raw channel, not through
libcamera or the PiSP image-processing pipeline. Do not install a vendor
libcamera fork or replace the system camera stack for this workflow. The
IMX296 driver does not supply native RAW8; do not silently substitute it to
make the byte counts or bit depths equal.

The combined tightly packed image payload is approximately **319 MB/s** or
**19.2 GB per minute** (decimal units), before negotiated row padding. Recording
also retains a one-second guard interval. Use the measured, negotiated buffer
sizes for actual storage requirements, not these rounded figures.

## Hardware and connection safety

- Raspberry Pi 5 with **active cooling** and an appropriate **27 W USB-C power
  supply**, such as the official Pi 5 supply. Include NVMe consumption in the
  power budget. Inspect installed RAM; a whole run is not buffered in RAM.
- A Pi 5-compatible PCIe NVMe adapter/HAT and an SSD with enough free space and
  demonstrated sustained write performance. Start with supported **PCIe Gen 2**.
  A label promising high peak SSD speed is not evidence of sustained throughput.
- The monochrome Mira220 EVM-SE and monochrome CAM-MIPI296RAW-TRIGGER.
  Verify the **exact PCB revision, module variant, connectors, and manual**
  for each physical board. Do not rely only on a product-family name.
- Two correctly wired **15-pin camera-board to 22-pin Pi 5 CSI cables** for the
  referenced board revisions. Check contact-side orientation at both ends using
  the board's own drawings and connector markings. Cable colour is not a wiring
  specification; do not force a connector or assume both sockets face alike.
- Suitable lenses and steady NIR illumination; note any IR-cut filters.

Shut down the Pi and **remove power from the Pi and any independently powered
camera/accessory before attaching or removing CSI cables**. No hot-plugging.
Connect Mira220 to physical **CAM/DISP0**, IMX296 to **CAM/DISP1**. These names
refer to the board sockets, not `/dev/video0` or an application's camera index.
Do not add extra camera power unless the exact module manual requires it.

This setup does **not** use a PiTrac controller, trigger cable, strobe controller,
or external synchronization. Leave trigger/strobe connections unused and
trigger mode disabled. The supplied Mira configuration utility is a register
configuration tool, not a replacement for the Pi driver, overlay, or media graph.
Do not run another register-control tool during comparison capture.

Keep source hardware PDFs outside the repository. Consult the manufacturer's
manual matching each board revision; the locally supplied manuals are not
redistributed here.

## Inspect first; preserve the existing OS

All command blocks below run **in a Bash terminal on the Pi**, not in Windows
PowerShell. Copy or check out this repository onto the existing Pi. The example
location is `$HOME/PiTrac_V2`; adjust it to your checkout.

```bash
cd "$HOME/PiTrac_V2/Software/camera-comparison"
EVIDENCE="$HOME/camera-comparison-evidence-$(date +%Y%m%d-%H%M%S)"
mkdir -- "$EVIDENCE"

uname -a
cat /etc/os-release
bash --version
bash scripts/inspect-platform.sh --output "$EVIDENCE/inspect-before"
```

Use **GNU Bash 5.1 or newer**, not `sh`. The helpers use Bash features and GNU
utilities. Keep the shell variables for later steps. After reboot or a new
terminal, set `EVIDENCE` to the **same existing evidence parent**, and return to
this working directory; do not create it again.

Every helper `--output` must name a **new, nonexistent child directory whose
parent already exists**. Do not create that child first. On failure, preserve it
and choose a different name for the next attempt. The inspection helper changes
no camera, package, or boot settings, but writes its requested report directory.

Initial inspection can return nonzero when packages, headers, modules, or media
devices are unavailable. Read `inspection.txt` from the
[inspection helper](scripts/inspect-platform.sh) rather than suppressing errors.
This is a list of missing evidence, not permission to continue to installation.

Record and review:

- Pi model, `aarch64` architecture, Trixie release, exact kernel release and
  package versions;
- the actual kernel's build headers and the installer's compatibility findings;
- installed RAM and available memory;
- detected camera entities, CFE receivers, existing module paths and versions;
- filesystems, free space, NVMe device/model and mount point;
- temperature and `vcgencmd get_throttled` flags. Historical flags need context;
  current undervoltage/throttling must be resolved before accepting a run.

**Hold point:** do not reflash, run `rpi-update`, run `apt full-upgrade` or
`dist-upgrade`, change boot order, force PCIe Gen 3, or replace the kernel to get
past a failed check. Inspect first. Any kernel migration or driver backport is
a separate decision with a recovery plan.

### Installed kernel differs from the running kernel

An image and matching headers can already be installed while `uname -r` still
reports an older kernel. For example, an inspection may list 6.18.50 packages
while the Pi is running `6.12.75+rpt-rpi-2712`. The Mira220 check correctly
rejects the **running** 6.12 kernel; installing more headers or removing that
check is not the solution.

A pending reboot is a common explanation, but incomplete package configuration,
an explicit boot override, or a different boot partition can also cause this.
Before changing packages or rebooting, inspect the candidate's package status
and the boot configuration. Substitute the candidate actually listed by your
inspection:

```bash
CANDIDATE=6.18.50+rpt-rpi-2712
dpkg-query -W -f='${binary:Package}\t${db:Status-Abbrev}\t${Version}\n' \
  "linux-image-$CANDIDATE" "linux-headers-$CANDIDATE"
cat /boot/firmware/config.txt
vcgencmd get_config str
ls -lh "/boot/vmlinuz-$CANDIDATE" "/boot/initrd.img-$CANDIDATE" \
  /boot/firmware/kernel_2712.img /boot/firmware/initramfs_2712
```

`ii` means a package is installed and configured. Follow any boot `include`
directives and review explicit `kernel`, `os_prefix` or initramfs overrides;
do not remove them blindly. Once the intended boot files and recovery path are
confirmed, a planned reboot may be all that is needed. Recheck `uname -r` and
run inspection into a new directory afterwards. If it still reports the old
kernel, investigate boot selection instead of repeatedly reinstalling packages.

Likewise, a sensor entity showing `Y10_1X10/1456x1088` while the receiver pads
still show `SRGGB10_1X10/640x480` is consistent with an unconfigured raw pipeline:
the receiver defaults do not mean the monochrome sensor has changed to Bayer.
A detected but unmounted NVMe is not yet a recording destination. Preserve its
existing partitions and data; do not run the storage benchmark on the microSD
root filesystem by mistake.

### Why Trixie is not enough

The [source pin](versions.env) selects vendor revision
`aeedd655d12f6698000416aa6fd94b6f4dfa7b16`. Its required APIs were found in the
inspected Raspberry Pi 6.18 headers but not the inspected 6.12 headers.
That is an API observation, **not** a declaration that every 6.18 kernel works,
and not permission to upgrade a 6.12 system. The installer currently admits
**only Raspberry Pi 6.18.x `rpi-2712` kernels as candidates**, subject to its
exact-header/API checks. It rejects other kernel series/flavours pending
separate review. [versions.env](versions.env) is data, not a shell script;
do not `source` it.

This restriction belongs to the Mira220 installer, not to the stock IMX296
alone. An early IMX296-only test may proceed once its OS, device permissions,
stock driver/overlay and raw media profile are checked. In that case, identify
inspection failures that are **only** missing/incompatible Mira220 prerequisites;
do not treat the overall nonzero inventory status as proof that IMX296 failed,
or ignore other missing evidence.

The [stock Raspberry Pi Mira220 implementation][stock-mira] inspected for this
work exposes Bayer modes; it is not interchangeable with the candidate vendor
monochrome implementation. The [pinned vendor driver][mira-driver] identifies
mono/RGB using OTP module identification. A `mono` overlay option remaining in
the vendor overlay is **not a guarantee** that an unknown module will be
identified as mono. Stop if the sensor does not negotiate `Y8_1X8` and `GREY`.

Use the **stock Raspberry Pi `imx296` driver and overlay** for the INNO-MAKER
camera, following its [current pinned guidance][inno-guide]. Do not install the
historical INNO-MAKER binary drivers for old kernels.

## Install only the required packages and matching headers

Only after saving and reviewing the initial inspection, refresh package metadata
and inspect the proposed dependency transaction:

```bash
sudo apt-get update
apt-get --simulate install --no-upgrade \
  bash v4l-utils util-linux coreutils psmisc fio gawk kmod \
  build-essential git device-tree-compiler sudo
```

If the proposal changes the kernel, firmware, libcamera stack, or removes
unrelated packages, **stop and review it**, rather than accepting blindly.
After approving the package list:

```bash
sudo apt-get install --no-upgrade \
  bash v4l-utils util-linux coreutils psmisc fio gawk kmod \
  build-essential git device-tree-compiler sudo
```

`v4l-utils` supplies `v4l2-ctl` and `media-ctl`; `util-linux` supplies `flock`,
`findmnt`, and `lsblk`; `psmisc` supplies `fuser`. The helpers need GNU coreutils
(including `timeout`, `stat`, `sha256sum`, and `sync -f`), `awk`, and the **fio
I/O benchmark**, not a different command with the same name. No `jq`, Python,
or CMake is required. Build dependencies above are for the external kernel
module and device-tree overlay. With the supported stock auto-initramfs layout,
the installer also needs `update-initramfs`, `lsinitramfs`, and `unmkinitramfs`
(normally supplied by `initramfs-tools`/`initramfs-tools-core`). If a check reports
them missing, review and install those packages separately with the same apt
simulation/no-upgrade procedure. Do not turn on a new initramfs scheme just to
silence a check.

Headers must match **the currently running kernel**, not merely an installed
headers metapackage or the newest kernel on disk:

```bash
KERNEL="$(uname -r)"
apt-cache policy "linux-headers-$KERNEL"
apt-get --simulate install --no-upgrade "linux-headers-$KERNEL"
```

If the exact package is available and the proposal is acceptable:

```bash
sudo apt-get install --no-upgrade "linux-headers-$KERNEL"
readlink -f "/lib/modules/$KERNEL/build"
make -s -C "/lib/modules/$KERNEL/build" kernelrelease
bash scripts/install-mira220.sh --check
```

The `kernelrelease` output must equal `uname -r` **exactly**, including its
suffix. The build tree must be complete/prepared and contain the interfaces
required by the pinned source. A headers metapackage being installed does not
prove this. If the matching package is unavailable or the API check fails, stop;
do not point the build symlink at another kernel, force module loading, or
download arbitrary headers.

Run inspection again into a new child directory after dependencies are ready:

```bash
bash scripts/inspect-platform.sh --output "$EVIDENCE/inspect-with-dependencies"
```

## Install Mira220 and edit boot configuration manually

Read the [installer](scripts/install-mira220.sh) and its help before using it:

```bash
bash scripts/install-mira220.sh --help
bash scripts/install-mira220.sh --check
```

The installer interface is `--check`, `--install`, `--remove`, and `--help`.
Run it as your **normal user**, including install/remove: it invokes `sudo` only
for scoped system writes and refuses to build as root. `--check` is read-only,
without network access, a build, or cache writes. Only explicitly requested
install/remove operations change driver files. **Boot configuration is always
a manual operator step.**

### 1. Audit boot and module configuration; save a backup

Read all of `/boot/firmware/config.txt`, not just its final lines:

```bash
sudo cat /boot/firmware/config.txt
sudo grep -RnsE 'mira220|imx296|trigger_mode' \
  /etc/modprobe.d /etc/modules-load.d
```

No matches from `grep` returns status 1; unreadable files are a different problem.
Follow every `include` directive in the boot configuration and inspect the
included files and all conditional scopes (`[all]`, `[pi5]`, and others).
Also inspect `/etc/modules`, `/boot/firmware/cmdline.txt`, and existing initramfs
configuration for module-loading or trigger overrides. Check `overlay_prefix`
and `os_prefix`: a custom boot layout may not load the installed overlay from
the expected path. Resolve conflicting camera overlays or settings only after
understanding what currently uses them.

Make a dated backup and record its path outside the boot partition as well:

```bash
BOOT_BACKUP="/boot/firmware/config.txt.before-camera-comparison.$(date +%Y%m%d-%H%M%S)"
sudo cp -a -- /boot/firmware/config.txt "$BOOT_BACKUP"
printf '%s\n' "$BOOT_BACKUP"
sudo cp -a -- /boot/firmware/config.txt "$EVIDENCE/config.txt.before"
```

Choose another backup name if either destination already exists. Separately
back up any included file that you will edit. Arrange console/SSH access and
access to the boot medium for recovery before rebooting.

### 2. Install the matched source-pinned driver and unique overlay

After the compatibility check passes, ensure **Mira220 is not loaded** and
`mira220-nir` is not enabled in any boot-config scope or include. If a Mira220
driver is loaded (including the stock driver), first disable its camera boot
settings manually, including auto-detection where needed, and reboot with it
disabled. Keep the IMX296 settings if they are needed. Do not force-unload a
live sensor module. After reboot, restore your working directory/variables and
repeat inspection and `--check`.

With those preconditions satisfied, run as the normal user:

```bash
bash scripts/install-mira220.sh --install
```

The installer fetches the exact pinned revision and builds it in a dedicated
user cache under `${XDG_CACHE_HOME:-$HOME/.cache}/pitrac-mira220-nir`. It prints
the retained per-attempt directory containing source, `build.log`, and
`overlay.log`. The vendor build requires that cache path have no spaces or shell
metacharacters; choose a suitable user-owned cache location if necessary.

The external-module build explicitly passes `CONFIG_MODULE_SRCVERSION_ALL=y`
so Kbuild generates the optional `srcversion` fingerprint used by the installer.
This is a **build-command setting for this module**: it does not edit the
running kernel's configuration or change `CONFIG_MODVERSIONS` ABI checks.
The pinned vendor source has no `MODULE_VERSION`, so a default build can
legitimately omit `srcversion` even when compilation succeeds.

Kbuild on the inspected 6.18.50 kernel emits **23 hexadecimal characters**:
modpost passes its 25-byte buffer with a size of 24 to `snprintf`, leaving
23 characters plus the terminator. This was confirmed with the actual Pi-built
module, not just a fixture. The installer accepts that form and the compatible
24-character form, using the same validator for the build and ownership record.
It preserves and compares the entire fingerprint without padding or truncation.

An older installer that requires exactly 24 characters will reject a valid
23-character fingerprint even with `CONFIG_MODULE_SRCVERSION_ALL=y` or a
`MAKEFLAGS` override. Inspect `modinfo -F srcversion` on the existing module named
in the failure output instead of repeatedly rebuilding. Update the installer's
build and ownership validation together; a generation-only change is insufficient.
Failures at `source-fetch-and-build` have not yet installed the module or overlay.
Do not remove packaged files, bypass identity checks, or rebuild the kernel.

The installer still verifies the fingerprint, vermagic, file checksums, module
precedence and initramfs. If it fails again, preserve the reported actual value
and length and stop before changing camera boot configuration.

The installed external module is
`/lib/modules/$(uname -r)/updates/pitrac-mira220-nir/mira220.ko`, with the paired
vendor overlay installed as
`/boot/firmware/overlays/mira220-nir.dtbo`. This unique overlay name avoids
overwriting the packaged `mira220.dtbo`. Keep the vendor pairing of clock,
two-lane connection, regulators, and startup sequencing; do not mix it with
unrelated stock-overlay fragments.

Module precedence and any active initramfs must be handled before reboot.
Do **not** blacklist `mira220`: stock and external modules have the same module
name, so blacklisting it can prevent the intended module from loading too.
Keep the installation output, cache location and pin/compatibility information
in [versions.env](versions.env) with the experiment evidence. The installer
records ownership and hashes in `/var/lib/pitrac-mira220-nir/ownership.env`;
do not edit or delete that record to bypass a failure.

Only the stock Trixie auto-initramfs layout is supported. Custom `initramfs` or
`ramfsfile` directives, mismatched boot images, pending-kernel image mismatches,
or stale embedded modules are stop conditions. If install reports a partial
failure, do not enable the overlay or reboot into it; keep the cache and
ownership record and follow the reported rollback instructions.

### 3. Make only the scoped camera boot edits

Open the file in an editor, for example:

```bash
sudo nano /boot/firmware/config.txt
```

Place the following in the appropriate existing scope for this Pi 5, after
resolving conflicting camera settings:

```ini
camera_auto_detect=0
dtoverlay=mira220-nir,cam0
dtoverlay=imx296
```

CAM1 is the default for the stock IMX296 overlay; do **not** add an unsupported
`cam1` parameter. Omit trigger-mode and `always-on` options. Confirm that no
included file or later conditional section re-enables conflicting overlays or
auto-detection. Do not append a second copy blindly or change unrelated settings.

For an **IMX296-only early test**, enable only the stock IMX296 camera overlay
with auto-detection disabled. Leave the Mira220 overlay disabled until its
installer and compatibility gates pass. IMX296 can be proven before Mira220.

Review the edits, then reboot deliberately:

```bash
sudo reboot
```

### 4. Check what actually loaded

After reconnecting, restore the working directory and `EVIDENCE` variable, then:

```bash
uname -r
modinfo -n mira220
modinfo -F vermagic mira220
modinfo -F srcversion mira220
cat /sys/module/mira220/srcversion
modinfo -n imx296
cat /sys/module/imx296/parameters/trigger_mode
sudo journalctl -k -b --no-pager
bash scripts/inspect-platform.sh --output "$EVIDENCE/inspect-after-reboot"
```

`modinfo` describes the module selected on disk; **it does not alone prove which
copy is already loaded**. Compare the running kernel, selected module path,
vermagic, loaded `srcversion` where available, install evidence, and boot log.
If the fingerprint is unavailable or inconsistent, resolve module/initramfs
precedence instead of declaring success. The IMX296 trigger parameter must be
`0`. Preserve the relevant boot log in the evidence directory.

Check that the sensor entities map to the intended physical ports, Mira220 uses
two CSI lanes and IMX296 one, and both are monochrome. The helpers discover
entities and their separate CFE graphs; they do not rely on stable
`/dev/mediaN`, `/dev/videoN`, or `/dev/v4l-subdevN` numbering. Verify the physical
wiring yourself.

## Choose an existing NVMe filesystem

Keep the existing boot arrangement; the Pi need not boot from NVMe to record
there. Use your adapter's instructions if PCIe needs enabling, after backing up
and inspecting boot settings. Do not change boot order or enable Gen 3 as an
automatic remedy. Raspberry Pi documents Gen 2 as supported and warns about
[Gen 3 stability][pi-pcie].

Identify an **already mounted, writable filesystem on the intended NVMe**:

```bash
lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINTS,MODEL
findmnt -o TARGET,SOURCE,FSTYPE,OPTIONS
```

Choose its real mount point; the following is an **example to replace**, not a
command to mount or format a drive:

```bash
NVME_MOUNT="/media/$USER/camera-nvme"
findmnt --mountpoint "$NVME_MOUNT" -o TARGET,SOURCE,FSTYPE,OPTIONS
df -hT -- "$NVME_MOUNT"
```

Compare `SOURCE` with `lsblk`. Stop if this is the SD card, a RAM filesystem, a
network mount, the wrong drive, a read-only filesystem, or no mount at all.
An empty directory named "nvme" is not evidence of an NVMe mount. If storage is
not prepared, make a separate, reviewed storage setup decision; this guide does
not provide destructive partitioning or formatting commands.

Once you have verified the source and available space, create a new parent on
that mounted filesystem:

```bash
NVME_RUNS="$NVME_MOUNT/pitrac-comparison-$(date +%Y%m%d-%H%M%S)"
findmnt --mountpoint "$NVME_MOUNT" -o TARGET,SOURCE,FSTYPE,OPTIONS &&
  mkdir -- "$NVME_RUNS"
```

Proceed only if both commands succeed. Run captures as your normal Pi user with
the required video-device permissions and a valid user-owned `XDG_RUNTIME_DIR`.
Do not use `sudo` for every helper or recursively loosen device/directory
permissions to hide access problems. Close other camera applications first.

## Exposure, gain, and timing

Preview the conversion without a Pi or camera:

```bash
bash scripts/configure-cameras.sh --calculate --exposure-us 1000
```

For the shared **1,000 us request**, the intended settings are:

| Sensor | Exposure control (lines) | Row time | Estimated physical integration |
|---|---|---|---|
| Mira220 | 124 | `304 / 38.4` = about 7.916667 us | 999.761667 us |
| IMX296 | 67 | `1100 / 74.25` = about 14.814815 us | 1006.852593 us |

Estimated difference: **7.090926 us**, not zero. Control readback is in the
driver's units and may be cached; it is not per-frame sensor register telemetry
or an optical timing measurement.

- Mira220's model uses `integration_us = lines * (304 / 38.4) + 18.095`.
  The offset accounts for the distinction between the EXPOSURE state and
  physical integration/shutter lag in [DS000642 v9-00][mira-datasheet],
  sections 7.6.4 and 8.2.1-8.2.2. Do not treat the line count alone as integration.
- IMX296's model uses `integration_us = lines * (1100 / 74.25) + 14.26`,
  based on the [Raspberry Pi exposure helper][imx-helper]. This is a timing
  reference only; using it does **not** require installing libcamera.
- The comparison helpers choose the nearest supported line setting, retain the
  requested value and calculated estimate, and reject unsupported exposure/gain
  requests rather than silently clamping, lengthening frames, or adding gain.
  The shared lower bound is about 29.074815 us; the Mira220 full-rate physical
  upper bound is approximately **11,188.5 us**. Not every boundary value is
  representable by a safe integer line setting. Use the calculator's accepted
  settings; do not round up the physical upper limit to the driver's integer
  maximum or bypass a rejected boundary request.
- Only `--gain 1` is supported. This means **physical 1x**, encoded as Mira220
  control `1` and IMX296 control `0`. A shared raw control number would be wrong.

The **11,188.5 us** figure is rounded: the safe 1411-line Mira220 endpoint
evaluates to approximately **11,188.511667 us** in this timing model. It does
not use the driver's integer-rounded 1412-line maximum. These are calculated
integration estimates, not optical measurements. For convenient requests safely
inside the shared range, use **30..11,188 us**. Both sensor ranges are checked
even when `--camera` selects only one camera, so single-camera bring-up cannot
silently bypass the matched comparison's limits.

Mira220 uses the datasheet ceiling
`ceil(1928 / 304) + 11 = 18` blanking lines, rather than the candidate driver's
integer-rounded minimum of 17. With a 38.4 MHz reference and 304-clock rows,
`38400000 / (304 * (1400 + 18))` gives about **89.080 fps**, not exactly 90.
IMX296 uses HMAX 1100 at 74.25 MHz and VBLANK 30:
`74250000 / (1100 * (1088 + 30))` gives about **60.376 fps**.
VBLANK 37 would produce 60 fps, but that is not this maximum-rate profile.

Keep the same exposure request for both cameras and record the residual
difference. The verifier permits no more than one slower-sensor exposure-line
interval (about 14.814815 us) of estimated mismatch. This is a quantization
tolerance, not a claim of measured shutter synchronization.

## Staged bring-up and recording

**Stop at the first failed stage.** Keep its logs, diagnose the cause, and retry
into new directories. A sink pass establishes camera-to-memory throughput only.
Do not skip directly to a one-minute disk run.

All examples run from this directory with `EVIDENCE` and `NVME_RUNS` set as above.
Use a fresh configuration after every reboot, kernel change, or camera change.
`--apply` changes the selected media graphs and controls, including resetting
their mutable links. It requires idle cameras. `--calculate` is offline and
cannot be combined with `--apply`.

For IMX296, configuration first restores and reads back the full active crop
`(0,0)/1456x1088`, then sets and checks the sensor/receiver formats. A smaller
crop left by another application can constrain `S_FMT`; simply requesting
1456 x 1088 on the video node does not restore that crop. Failure to restore the
full active rectangle is a stop condition, not permission to record a smaller
image.

### 1. Prove IMX296 alone

This may be done before the Mira220 installation, once the stock IMX296 overlay
is active and prerequisites are met:

```bash
bash scripts/configure-cameras.sh --apply \
  --output "$EVIDENCE/config-imx296" --camera imx296 \
  --exposure-us 1000 --gain 1
bash scripts/record-dual.sh --config "$EVIDENCE/config-imx296" \
  --output "$EVIDENCE/sink-imx296" --seconds 10 --sink
bash scripts/verify-recording.sh --run "$EVIDENCE/sink-imx296"
```

The recorder's name does not require two cameras: it records all sensors present
in the specified configuration. There is no recorder `--camera` option.

### 2. Prove Mira220 alone

Do this only after the vendor module/overlay installation and reboot checks:

```bash
bash scripts/configure-cameras.sh --apply \
  --output "$EVIDENCE/config-mira220" --camera mira220 \
  --exposure-us 1000 --gain 1
bash scripts/record-dual.sh --config "$EVIDENCE/config-mira220" \
  --output "$EVIDENCE/sink-mira220" --seconds 10 --sink
bash scripts/verify-recording.sh --run "$EVIDENCE/sink-mira220"
```

The exact sensor-pad format, receiver-pad formats, full image dimensions, and
capture fourcc must agree. If Mira220 falls back to Bayer or streaming returns
`Broken pipe`, `-32`, or `-22`, preserve topology and kernel logs. Merely trying
`GREY` or `Y16` on a video node does not prove that the upstream failure is fixed.

### 3. Prove both together, without storage

With both overlays active and the intended physical wiring checked:

```bash
bash scripts/configure-cameras.sh --apply \
  --output "$EVIDENCE/config-both" --camera both \
  --exposure-us 1000 --gain 1
bash scripts/record-dual.sh --config "$EVIDENCE/config-both" \
  --output "$EVIDENCE/sink-both" --seconds 10 --sink
bash scripts/verify-recording.sh --run "$EVIDENCE/sink-both"
```

Expect `VERIFIED_SINK_ONLY`, not a storage-success claim. Diagnose CSI, CPU,
available memory, power, or thermal limits here before adding SSD load.

### 4. Benchmark the selected NVMe filesystem

Recheck the mount, then:

```bash
findmnt --mountpoint "$NVME_MOUNT" -o TARGET,SOURCE,FSTYPE,OPTIONS
bash scripts/benchmark-storage.sh \
  --output "$NVME_RUNS/storage-test-01" --gib 24
```

This creates a new **24 GiB allocation file** named `write-test.bin` on that
filesystem, writes random/refilled data with fio direct I/O, and flushes it.
The helper runs fio inside the newly created output directory with the fixed
relative filename `write-test.bin`, so colons in the parent path are not
interpreted as fio's filename-list separators.
The access pattern is sequential writes (`--rw=write`) with randomized buffer
contents, **not random-offset IOPS**. Direct I/O avoids a page-cache-only result;
the elapsed measurement includes file allocation and final flush. It never
writes directly to a block device.

Allow at least the file size plus the helper's 1 GiB free-space reserve. A
successful test removes only its completed allocation file, retaining `fio.txt`,
`storage.tsv`, and `status`. If interrupted or failed, a partial test file may
remain: inspect that specific directory and remove only the confirmed disposable
test file if necessary.

Repeat into new directories after SSD cache/temperature stabilize. A single
24 GiB run may still fit an SSD's internal write cache. Do not select only a
fast cold-cache result; use representative sustained performance for the
immediately following captures.

The recording helper requires:

- measured throughput at least **1.25 times** the negotiated combined
  `sizeimage * fps` rate (roughly 400 MB/s before padding);
- benchmark byte count at least **1.25 times** the planned retained bytes,
  including the extra second;
- a report from the **same boot, kernel, and filesystem device** as the run;
- capture free space with 25% byte headroom plus the helper's small reserve.

The report is not transferable to a different mount/device or reboot. Re-run
the benchmark after a reboot or storage/power/thermal change and keep it fresh.
Same-boot validation is not a guarantee that an old report still represents
current SSD cache behaviour. If 24 GiB is too small for a negotiated layout,
choose a larger `--gib` value (1..256) only after checking free space.

### 5. Record 10 seconds, then 60 seconds to disk

Do not combine `--sink` with `--storage-report`. Use the report from your most
recent representative benchmark:

```bash
STORAGE_REPORT="$NVME_RUNS/storage-test-01/storage.tsv"

bash scripts/record-dual.sh --config "$EVIDENCE/config-both" \
  --output "$NVME_RUNS/dual-10s" --seconds 10 \
  --storage-report "$STORAGE_REPORT"
bash scripts/verify-recording.sh --run "$NVME_RUNS/dual-10s"
```

Inspect both summaries, controls, logs, status, temperature, and power flags.
Only after this passes:

```bash
bash scripts/record-dual.sh --config "$EVIDENCE/config-both" \
  --output "$NVME_RUNS/dual-60s" --seconds 60 \
  --storage-report "$STORAGE_REPORT"
bash scripts/verify-recording.sh --run "$NVME_RUNS/dual-60s"
vcgencmd get_throttled
vcgencmd measure_temp
```

The duration must be an integer **1..60 seconds**. Each sensor skips **five
warm-up frames**, then retains approximately `seconds + 1` seconds plus endpoint
rounding so asynchronous starts can still provide the requested overlap. Those
extra retained frames remain in the files; the helper does not trim to an exact
shared interval. For 60 seconds, budget about 61 seconds of raw data plus padding
and required headroom, not just 19.2 GB.

Capture uses eight mmap buffers per sensor, with RAM checks and bounded process
timeouts, rather than accumulating an entire run in memory. Disk completion
includes a filesystem flush. Interrupted, partial, or unverified runs are invalid;
do not rename them to imply success.

## Files, metadata, and verification

TSV files use tab-separated key/value fields; frame tables have a header and one
row per retained frame. They are readable as text without Python or `jq`.
Keep raw files **together with all logs and metadata**. Raw byte streams have no
self-describing image headers.

| Output | Meaning |
|---|---|
| Inspection `inspection.txt` | OS/kernel/package, module, topology, power/thermal, memory and storage inventory, including failed checks |
| Configuration `status` | Must be `CONFIGURED` before capture; this alone does not prove streaming |
| `<sensor>.tsv` | Sensor identity/nodes, native depth, exact dimensions/fourcc/stride/sizeimage, requested and estimated exposure, control codes, timing, kernel and boot identity |
| Configuration `<sensor>.before-topology.txt`, `.topology.txt`, `.controls.txt`, `.video-format.txt` | Discovery and negotiated media/format evidence |
| Run `<sensor>.raw` | Concatenated retained buffers, disk mode only; sink mode writes image bytes to `/dev/null` |
| `run.tsv` | Schema, camera selection, requested duration, five-frame warm-up, sink/disk mode, byte/rate budgets, frame targets, boot/kernel/filesystem identity and v4l2-utils version |
| `<sensor>.capture.log` | Required original verbose V4L2 buffer evidence, not optional progress output |
| `<sensor>.frames.tsv` | Parsed retained sequence, monotonic device timestamp, bytes used, and flags |
| `<sensor>.summary.tsv` | Retained count, measured FPS, first/last timestamp, span, `min_interval_us` and `max_interval_us` |
| `<sensor>.controls-before.txt`, `.controls-after.txt`, `.topology.txt`, `devices.txt`, `<sensor>-module.txt` | Control inventories, topology and selected module evidence around capture |
| `storage.tsv`, `fio.txt` | Benchmark identity/bytes/elapsed MB/s and fio details; the accepted TSV is copied into disk runs |
| `completion.tsv` | Completion/flush timing, durability mode and live control-readback result |
| `manifest.sha256` | Checksums of the run/configuration inputs and disk storage report; not a raw-image checksum or proof of provenance |
| `verification.tsv` | Measured common timestamp overlap and calculated inter-camera exposure difference, explicitly not optically measured |
| Run `status` | `VERIFIED_SINK_ONLY`, `VERIFIED_RECORDING`, or an invalid/failure state |

The recorder invokes [verification](scripts/verify-recording.sh) automatically;
the explicit verification commands above demonstrate repeatable checking.
Reverification is offline but **rewrites** derived frame/summary/verification
files and status. Preserve the original evidence when investigating a failure.

For acceptance, the verifier checks supported full-frame mono profiles, timing
and unity codes, required metadata, exact retained counts, monotonic timestamps,
sequence continuity, error flags, bytes used, and disk file sizes. Measured FPS
must be within **1%** of the target. As a **conservative cadence gate**, the
parser also checks every interval between retained frames and rejects values
outside **0.5..1.5 target frame periods**. This catches an isolated missing
sensor/CFE frame that could otherwise hide behind an acceptable average FPS,
even when delivered-buffer sequence numbers remain contiguous. Inspect
`min_interval_us` and `max_interval_us` in the sensor summary alongside the
original timestamps. A cadence failure can indicate lost frames or excessive
timestamp jitter; it does **not** establish a sensor hardware fault by itself.

The common interval is `min(last_timestamp) - max(first_timestamp)` across
retained streams and must be **at least the requested duration**. For a
single-camera test this is its own retained span. It is not an exposure-start
alignment measurement. Disk verification additionally needs successful
completion/flush evidence and a compatible storage report.

Disk runs archive the accepted `storage.tsv` and include it in
`manifest.sha256`. Offline verification requires that archived report and
rechecks its boot, kernel and filesystem identity against the **recorded run**,
plus its throughput and tested-byte headroom against the retained-byte budget.
It does not borrow a newer benchmark or compare the archive to the machine
currently doing the offline verification. Keep the original report in the run;
a missing, changed or incompatible report invalidates the disk evidence.

### Required V4L2 logging contract

The installed `v4l2-ctl` must support the options used by the recorder, including
`--verbose`, `--stream-no-query`, mmap capture, polling, skip, count and output.
The [parser](scripts/parse-frames.awk) currently supports single-plane verbose
dequeue lines shaped like this **synthetic example**:

```text
cap dqbuf: 0 seq: 6 bytesused: 2240000 ts: 1234.067356 field: none (ts-monotonic, ts-src-soe)
```

It requires `seq:`, `bytesused:`, a decimal `ts:`, monotonic timestamp flags,
and the expected records for warm-up and retained frames. Multiplane records,
unsupported data offsets, missing flags, partial-write evidence, or a different
logging schema are not silently accepted. A progress dot, plausible raw file
size, or application's "fps" line cannot replace per-buffer evidence.

If the installed utility emits a different format, stop with its version and
actual log. Do not synthesize timestamps, invent sequence numbers, change
metadata to satisfy a fixture, or remove the check. Schema support must be
reviewed against real output before hardware validation can continue.

Bypassing host processing leaves sensor-internal behaviour in place. Driver
initialization can retain black-level, defect-pixel, or other on-sensor
corrections not exposed as V4L2 controls. Preserve module/source revision and
before/after control inventories; additionally record relevant fixed
initialization settings from the pinned driver for the experiment. An absent
control is **not** evidence that a correction is disabled, and a register
constant is not a per-frame measurement. Do not claim all corrections are off.

## Interpreting native raw pixels

- **Mira220 `GREY`:** one unsigned 8-bit pixel per byte, 1600 active pixels per
  row and 1400 rows. Native precision here is 8 bits, values 0..255.
- **IMX296 `Y10P`:** native 10-bit monochrome, MIPI RAW10 packing, **four pixels
  in five bytes**, not four 16-bit words. For bytes `b0..b4`, the four pixels are
  `(b0 << 2) | (b4 & 3)`, `(b1 << 2) | ((b4 >> 2) & 3)`,
  `(b2 << 2) | ((b4 >> 4) & 3)`, and `(b3 << 2) | ((b4 >> 6) & 3)`.
  Each reconstructed value is 0..1023. See the [kernel luma format reference][luma].
- The active IMX296 row payload is `1456 * 10 / 8 = 1820` bytes. CFE row stride
  is aligned to **16 bytes**, so a 1824-byte negotiated row has four padding
  bytes after its pixel data. Always use the recorded stride; do not assume
  tightly packed rows. At that stride, `sizeimage = 1824 * 1088 = 1,984,512`.
  Mira220's 1600-byte active row is already 16-byte aligned, but still verify
  the reported layout.
- Frame boundaries follow negotiated `sizeimage` and bytes used, not a guessed
  width-times-height formula. Supported capture is single-plane, with
  `sizeimage = stride * height`; helpers fail on unsupported layouts.
- Unpacking RAW10 into a 16-bit integer array for analysis is lossless storage
  expansion, **not native 16-bit sampling**. `Y16` is not a substitute for the
  requested `Y10P`; nor is a PiSP compressed raw format. This setup deliberately
  uses neither host PiSP compression nor host image enhancement.

Do not debayer monochrome data. Host auto-exposure/gain, gamma, sharpening,
denoising, tone mapping, and display normalization are outside this capture
path. Preserve original raw values; any later display stretch or conversion
must be labelled and kept separate from measurement data.

## A fair NIR comparison

Use steady illumination, not an unsynchronized flashing source. Save an
experiment note alongside the run with at least:

- wavelength and spectral bandwidth (for example 850 or 940 nm), source
  geometry/intensity/stability, ambient light and temperature;
- target, distance, framing, exposure request and estimated per-sensor times;
- each lens, aperture, focus, transmission assumptions and IR-cut filter state;
- camera PCB/module revision, optics/pixel sampling differences, and retained
  sensor correction settings;
- dark frames with the same settings and temperature, and frames of a common
  uniform/flat target under the same illumination.

Do not independently normalize each camera to the same mean or maximum before
comparing signals. Account for black level, saturation, pixel area, optical
throughput, conversion gain, field of view and target sampling. Equal exposure
requests and unity analogue gain do not make these quantities equal.

RAW8 quantization can conceal low-level noise and small signals relative to
native RAW10. Upconverting 8-bit data to 16-bit does not restore missing
precision; downconverting RAW10 to 8-bit throws precision away. Report these
as system-level, native-format comparisons, not proof of absolute quantum
efficiency or a calibrated sensor-only SNR advantage. Native-precision
characterization with calibrated illumination/optics is a separate follow-up,
not a silent change to this recording profile.

## Troubleshooting and rollback

| Symptom | Check next |
|---|---|
| Initial inspection fails | Read every `UNAVAILABLE/FAILED` item; install only reviewed prerequisites or resolve the exact header/API issue |
| Headers mismatch or vendor build fails | Preserve `uname -r`, build output and the matching build-tree information; do not upgrade or force-load to bypass it |
| Mira220 not mono | Check the loaded external module, OTP variant identification and exact pad formats; `mono` in an overlay is not sufficient |
| Sensor probes but `STREAMON` fails | Keep full media topology and kernel log; compare with the [open CFE issue][mira-issue], check formats on all pads, not only fourcc |
| No camera / wrong port | Power off before checking cables, PCB revision/orientation, scoped boot settings, lane mapping and module binding |
| Camera busy / lock / permission error | Close other camera applications; use the logged-in user's video permissions and runtime directory, not blanket root execution |
| Configuration from another boot or format/control changed | Reconfigure into a new directory; do not edit old TSV values |
| Missing or unrecognized frame logs | Confirm installed v4l2-utils version/options and real dequeue schema; do not infer successful capture |
| Single sink passes, dual sink fails | Investigate CSI, CPU/memory availability, power and thermal limits without SSD traffic |
| Dual sink passes, disk fails | Recheck actual mount, sustained SSD/cache behaviour, free space, current power/temperature and same-boot report |
| Insufficient common overlap | Compare retained timestamps and startup delays; a longer file alone is not proof of adequate overlap |
| Interrupted or invalid run | Keep status, partial bytes and all logs; retry with a new output directory after resolving the cause |

To remove this setup, first stop captures and preserve evidence. Disable
`mira220-nir` in **every** boot-config scope/include; also keep other Mira220
overlays/auto-detection from loading a Mira module during removal. Compare
against the saved backup, but do not overwrite later unrelated edits with an
old whole-file backup.

**Reboot manually with Mira220 disabled before removal.** The installer refuses
to remove a loaded module and never unloads it for you. Removal must run on the
**kernel recorded at installation**, although its build headers are not needed.
If you have already switched kernels, stop and review how to boot that recorded
kernel or arrange an audited recovery; do not force an old kernel's initramfs
over the active boot image.

Use the [installer's](scripts/install-mira220.sh) removal mode for its external
module and uniquely named overlay:

```bash
bash scripts/install-mira220.sh --remove
```

Run removal as your normal user; it uses scoped `sudo`. It removes only
checksum-matching owned files and refreshes dependencies/the supported
initramfs. It preserves packaged modules/overlays, recordings and the user
cache. Changed/unowned files or changed initramfs layouts require manual audit,
not deleting a manifest to make the check pass.

Review its output, then restore the prior camera/auto-detection settings only
where appropriate and reboot if those settings changed. Do not delete packaged
modules or overlays, blacklist the shared module name, or recursively delete
kernel directories. Verify post-reboot module paths and the boot log. Restoring
stock Mira220 does not imply support for this monochrome comparison profile.

An external module is tied to a particular kernel; there is no automatic DKMS
rebuild in this workflow. **Before switching kernels**, disable Mira220, reboot
with it disabled, and remove the owned installation while its recorded kernel
is still running. Then carry out the separately approved kernel change.
On the new kernel, repeat inspection, install exact running-kernel headers,
rerun compatibility/build/install checks and staged captures, and regenerate
configuration and storage reports. Do not copy an old `.ko` into a new kernel's
directory or assume old ownership state can be reused.

## Validation status and sources

Run the [capture-helper fixtures](tests/test-helpers.sh) and the
[installer build fixtures](tests/test-installer.sh) with:

```bash
bash tests/test-helpers.sh
bash tests/test-installer.sh
```

They exercise shell syntax, argument/error paths, timing calculations and
deterministic metadata/log fixtures. The installer tests compile a small C
reproducer of the pinned Kbuild formatting contract, validate the actual
23-character Pi fingerprint in both build and ownership checks, reject malformed
fingerprints, and retain compatible 24-character records. They use GNU Make to
verify metadata-option propagation through a recursive build, unchanged
configuration/ABI settings, the command-local retry and build-error propagation.
They need a C compiler (`cc`, supplied by the existing build prerequisites), but
do not compile or install a real kernel module. No Python or CMake test
setup is needed.
**Fixtures are schema/logic tests, not hardware validation**: synthetic logs
and sparse fixture files are not evidence of photons, sensor streams, sustained
storage, or a successful Pi kernel build.

Target acceptance remains pending until recorded evidence establishes:

1. exact-kernel vendor build/install and correct module/overlay after reboot;
2. each sensor alone, then both together, passing sink verification;
3. representative same-boot NVMe throughput with 25% rate/byte headroom;
4. 10-second and 60-second dual disk runs with sufficient actual overlap,
   FPS within 1%, no sequence gaps/error buffers, exact sizes, fixed controls
   and successful flush;
5. no unresolved power/thermal or format/metadata substitutions.

Keep actual kernel/package/module versions, inspection reports, logs and
experiment notes with the results. Update the tested matrix in
[versions.env](versions.env) only after target evidence exists. There is no
validated Pi/kernel combination claimed by this documentation.

### Source references

- [Pinned Mira220 vendor source][mira-driver]: modes, timing, fixed gain, OTP
  identification, module build and paired overlay. This guide deliberately uses
  a unique overlay name and scoped manual edits rather than blindly following
  the vendor README's append/overwrite examples.
- [Mira220 DS000642 v9-00][mira-datasheet]: full active array and timing;
  sections 7.6.4, 8.2.1 and 8.2.2 for shutter lag, exposure and blanking.
  Link only; do not copy the PDF into this repository.
- [Mira220 Pi 5/CFE streaming report #2][mira-issue]: unresolved runtime risk,
  including a reported failure on `6.18.39+rpt-rpi-2712`.
- [INNO-MAKER setup snapshot][inno-guide]: current stock IMX296 driver/overlay
  guidance for Pi 5/Trixie.
- [Raspberry Pi Mira220 driver snapshot][stock-mira],
  [IMX296 driver snapshot][imx-driver], and [IMX296 overlay][imx-overlay]:
  distinguish stock behaviour, controls and port defaults.
- [RP1 CFE format mappings][cfe-formats] and [kernel luma formats][luma]:
  native monochrome capture formats and packing.
- [Raspberry Pi IMX296 exposure/gain helper][imx-helper]: integration line time,
  offset and gain conversion; cited as a model, not an installation dependency.
- [Raspberry Pi PCIe documentation][pi-pcie]: supported Gen 2 link and Gen 3
  stability warning.

New software and documentation here are **GPL-3.0-or-later**; see the
[root GPL license text](../../LICENSE). Third-party sources retain their
respective licenses. No unrelated hardware/firmware licensing is changed.

[mira-driver]: https://github.com/ams-OSRAM/mira220_v4l2_driver/tree/aeedd655d12f6698000416aa6fd94b6f4dfa7b16
[mira-datasheet]: https://look.ams-osram.com/m/7591efdbe4af32dc/original/Mira220-1-2-7-2-2-MP-NIR-enhanced-global-shutter-image-sensor.pdf
[mira-issue]: https://github.com/ams-OSRAM/mira220_v4l2_driver/issues/2
[inno-guide]: https://github.com/INNO-MAKER/cam-imx296raw-trigger/tree/69a6afc2d9598db019915ad1bc3af025a7a7d26c
[stock-mira]: https://github.com/raspberrypi/linux/blob/0ac97ba3443f519b61bbc96079736cd8b881ea22/drivers/media/i2c/mira220.c
[imx-driver]: https://github.com/raspberrypi/linux/blob/0ac97ba3443f519b61bbc96079736cd8b881ea22/drivers/media/i2c/imx296.c
[imx-overlay]: https://github.com/raspberrypi/linux/blob/0ac97ba3443f519b61bbc96079736cd8b881ea22/arch/arm/boot/dts/overlays/imx296-overlay.dts
[cfe-formats]: https://github.com/raspberrypi/linux/blob/0ac97ba3443f519b61bbc96079736cd8b881ea22/drivers/media/platform/raspberrypi/rp1_cfe/cfe_fmts.h
[imx-helper]: https://github.com/raspberrypi/libcamera/blob/6c1dd9d55573010f710c9e190a73e7e76f0d9432/src/ipa/rpi/cam_helper/cam_helper_imx296.cpp
[pi-pcie]: https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#pcie-connector
[luma]: https://docs.kernel.org/userspace-api/media/v4l/pixfmt-yuv-luma.html
