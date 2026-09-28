<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Live focusing without the Pi ISP

The focus preview uses the same configured V4L2 sensor/CFE raw path as the
recorder: Mira220 `GREY` at 1600 x 1400 and IMX296 `Y10P` at 1456 x 1088.
It does not invoke libcamera, PiSP image processing, auto-exposure/gain,
debayering, sharpening, denoising or per-frame brightness normalization.

Camera frames pass through anonymous RAM pipes into bounded latest-frame
buffers. The application writes **no image files on the Pi** and does not need an SSD.
Diagnostic messages may appear in the terminal or service journal; those are
not saved frames. The **Capture** button explicitly downloads a lossless image
pair to the PC; ordinary preview does not automatically save anything.

The default **Smooth (JPEG)** mode requests up to **10 fps per camera**. The Pi
maps `GREY` directly to 8-bit grayscale and maps IMX296 MIPI RAW10 using
`value >> 2`, then encodes a full-resolution grayscale JPEG in RAM (quality 90).
This is lossy display compression, not Pi ISP processing. There is no resizing,
brightness stretch, sharpening, denoising, or change to camera controls.
Sensor-internal corrections remain those programmed by the recording driver.

Use the page's **Raw pixels (lossless)** option to check fine detail and exact
native pixel values. That mode sends complete native buffers, optionally with
lossless HTTP gzip, and decodes them in the browser. Raw mode can be much slower
over Wi-Fi; noisy full-resolution frames can require several hundred Mbit/s at
10 fps for both cameras. The selector only changes display transport; recording
always keeps the original raw data. JPEG may change fine texture, so confirm
critical focus in raw mode.

## Start on the Pi

Use a current, successfully configured camera directory from
`configure-cameras.sh`, with desktop camera clients paused as explained in
[QUICKSTART.md](QUICKSTART.md). Do not reuse a configuration from another boot.

```bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd "$HOME/PiTrac_V2/Software/camera-comparison"
bash scripts/focus-preview.sh \
  --config "$CONFIG"
```

The launcher checks the same dimensions, native formats, frame timing and
exposure/gain controls as recording. Startup uses that configuration unchanged;
the live exposure controls below can explicitly adjust exposure and frame
timing for focusing. It takes the shared camera lock: **preview and recording cannot run
at the same time**.

`CONFIG` must name a configuration from the **current boot**. For a fresh boot,
pause the desktop camera clients as above, then create one before launching:

```bash
CONFIG="$HOME/camera-focus-$(date +%Y%m%d-%H%M%S)"
bash scripts/configure-cameras.sh --output "$CONFIG" \
  --camera both --exposure-us 10000 --gain 1 --apply &&
bash scripts/focus-preview.sh --config "$CONFIG"
```

This example requests 10 ms for both cameras. No NVMe mount, storage benchmark,
or test recording is required for focusing. Stop at a configuration failure.

Python 3.11 or newer is required. Smooth mode and PNG capture require Pillow, provided by
the Raspberry Pi OS `python3-pil` package (already present on the inspected Pi).
Raw viewing (`--transport raw`) still uses only the Python standard library;
the Capture button is unavailable without Pillow.
No pip installation is required when using the OS package.

Options:

- `--port 8765`: unprivileged HTTP port on **Pi loopback only**.
- `--preview-fps 10`: requested browser refresh rate, 1..10 fps.
- `--buffers 32`: 8..32 native capture buffers per camera.
- `--transport jpeg`: initial browser mode; `raw` selects lossless native frames.
- `--jpeg-quality 90`: display JPEG quality, allowed range 60..95.

The sensors continue at their configured native acquisition rates. The browser
intentionally skips intermediate frames and retrieves only the latest frame;
display rate depends on network and rendering performance. This is a focusing
tool, not a replacement for the recorder's dropped-frame qualification.
The same first five warm-up frames are skipped as in recording, and they are
excluded from preview rate/cadence statistics.

## Connect from this PC

In a separate **local Windows PowerShell** terminal, keep an SSH tunnel running:

```powershell
ssh.exe -4 -N -T -L 127.0.0.1:8765:127.0.0.1:8765 -o ExitOnForwardFailure=yes -o ConnectTimeout=10 -o ServerAliveInterval=15 -o ServerAliveCountMax=4 pitrac@pitrac.local
```

Enter the Pi password locally if prompted; do not put it in a command or share
it in chat. A quiet terminal without a remote shell prompt is normal with `-N`.
`-4` avoids an unavailable IPv6 route; the keepalives detect an unresponsive
SSH connection instead of leaving a silently stalled tunnel. They do not repair
Wi-Fi outages or automatically reconnect after a reset.
If `.local` resolution is unavailable, use the Pi's confirmed IP and retain
host-key verification, for example with `-o HostKeyAlias=pitrac.local`.

For the inspected PC/Pi pair, the user's existing key and confirmed IPv4 address
work with this command:

```powershell
ssh.exe -4 -N -T -i "$env:USERPROFILE\.ssh\id_ed25519_personal" -o IdentitiesOnly=yes -o StrictHostKeyChecking=yes -o HostKeyAlias=pitrac.local -o ExitOnForwardFailure=yes -o ConnectTimeout=10 -o ServerAliveInterval=15 -o ServerAliveCountMax=4 -L 127.0.0.1:8765:127.0.0.1:8765 pitrac@192.168.0.17
```

Confirm the address again if DHCP changes it. Do not disable host-key checking
to silence a mismatch. Run only one tunnel on local port 8765; an already-running
tunnel must not be mistaken for a failed new connection.

Open **http://127.0.0.1:8765/** on the PC. The server is not exposed directly to
the LAN. An SSH key explicitly restricted against port forwarding cannot open
this tunnel; use your normal authorized SSH connection instead of bypassing
that restriction.

Use the side-by-side panels to focus. Fit view is convenient for framing;
**1:1 view is the relevant view for judging individual pixels**. Larger zoom is
display-only. Both JPEG and raw display modes support the same exposure controls.

### Connection errors are not all Pi disconnects

| Message/symptom | Meaning and next check |
|---|---|
| `channel ... open failed: connect failed: Connection refused` in SSH | SSH is connected, but its Pi-side destination `127.0.0.1:8765` has no listener. This occurs while the preview server stops/restarts; inspect the preview service before changing Wi-Fi. |
| Browser cannot reach local port 8765 | Check that the PC-side SSH tunnel is still running. The preview can remain healthy on the Pi after the tunnel exits. |
| `client_loop: send disconnect: Connection reset` | The SSH transport ended. Reconnect the tunnel; this alone does not prove the Pi rebooted or a camera failed. |
| Timeout connecting to SSH port 22 | Check the resolved address, IPv4 reachability and PC/router connection. This is separate from the preview HTTP port. |

On the Pi, read-only checks are:

```bash
uptime
vcgencmd get_throttled
systemctl --user status pitrac-focus-preview.service --no-pager
curl --fail --max-time 5 http://127.0.0.1:8765/api/status
```

On the PC, check the local listener with:

```powershell
Get-NetTCPConnection -LocalPort 8765 -State Listen -ErrorAction SilentlyContinue
```

During the observed September 18 incident, the Pi retained its boot ID and
Wi-Fi association and both cameras stayed live, while the PC tunnel had exited.
IPv4 SSH and 30 test pings succeeded; IPv6 access/name resolution failed.
An IPv4-only tunnel restored the preview. The original TCP reset's precise
cause was not established, so no Wi-Fi, power-save or router settings were
changed. Earlier preview restarts during software deployment account for
temporary destination-refused messages, not a Pi reboot.

### Recover automatically while keeping a work VPN connected

A later reset was reproduced on IPv4 too. At that failure, Windows selected
the **PANGP / GlobalProtect virtual adapter** for the Pi's address instead of
the home Wi-Fi interface. The gateway remained reachable while the Pi timed out;
the selected route subsequently returned to Wi-Fi. This identifies a PC-side
network-path transition, not a camera or Pi reboot. IPv4 alone is not a complete
fix, and the exact reason for the route transition still needs network/IT review.

If a work VPN must stay connected, do not disable it, delete its routes, add
manual route overrides, or change managed policy to reach the Pi. Ask IT about
permitted local-LAN access. A wired connection for the PC may avoid Wi-Fi
transitions, but is not a substitute for the applicable VPN policy.

The [Windows reconnecting tunnel](scripts/focus-tunnel.ps1) uses only the
system's existing route and the authorized SSH key. It retries network failures
every ten seconds, retains strict host-key checking, and stops on authentication,
host-key or local-port conflicts. It does not override the VPN or make the Pi
reachable while the network path is unavailable.

Run from the repository on the **Windows PC**, instead of a separate SSH tunnel:

```powershell
& .\Software\camera-comparison\scripts\focus-tunnel.ps1 -PiAddress 192.168.0.17
```

The default identity is `$env:USERPROFILE\.ssh\id_ed25519_personal`; use
`-IdentityFile` and `-UserName` if yours differ. `-HostKeyAlias pitrac.local`
keeps verification against the established Pi identity when an IP is supplied.
An encrypted key must already be available through your normal SSH agent.
The script never asks for a password and does not change execution policy;
if organizational policy prevents running it, use an IT-approved method.

Keep that terminal open and press Ctrl+C to stop. Only one tunnel can own
port 8765. Automatic reconnection restores the transport when possible; camera
capture on the Pi is managed independently by the preview service.

## Capture a lossless image from both cameras

Click **Capture** under **Save both cameras**. The browser downloads one
uniquely named `pitrac-capture-*.zip` to its normal download location (or asks
where to save, according to browser settings). Keep the page and tunnel open
until the download is requested. Extract the ZIP to access:

| File | Contents |
|---|---|
| `mira220.png` | Full 1600 x 1400, lossless 8-bit grayscale; native values 0..255 unchanged |
| `imx296.png` | Full 1456 x 1088, lossless 16-bit grayscale container holding all native 10-bit values 0..1023 unchanged |
| `mira220.raw`, `imx296.raw` | Byte-exact native `GREY` / packed `Y10P` buffers, including row padding |
| `metadata.json` | Dimensions, strides, format, native/PNG bit depths, frame IDs, exposure settings, receive times, and file SHA-256 hashes |
| `README.txt` | Pixel-value and timing interpretation |

**The IMX296 PNG is not an 8-bit preview.** It preserves the two low bits
discarded by the JPEG/display mapping. Native samples are right-aligned in
the 16-bit container, with no shift, stretching, gamma or normalization.
It may therefore look dark in viewers that display the full 0..65535 range;
use a 16-bit-capable image tool with display-only levels for viewing.

Capture uses the newest live native frame from each camera when the Pi handles
the request, independent of JPEG/raw display mode and zoom. It does not capture
the browser canvas or change exposure, gain, acquisition or recording settings.
These are **not hardware-synchronized exposures**. Times and the pair's time
difference are measured at frame receipt on the Pi, not at exposure; they must
not be used as proof of optical synchronization.

Both cameras must be present and live. Startup, stale, stopped or failed
capture prevents the entire download rather than exporting an old or partial
pair. Changes in exposure/preview session invalidate stale requests; wait for
current status and retry. Only one capture download is processed at a time.
Encoding and ZIP creation happen in RAM; nothing is saved to Pi storage.
The API's `saves_frames: false` continues to mean no server-side frame files;
`snapshot_capture` reports the explicit browser-download capability.

## Linked and independent exposure controls

**Link camera exposures** is on by default. Edit either microsecond input and
the other follows; click **Apply exposure** to update both sensors. Uncheck the
link to enter different values and apply them independently. Re-enabling the
link copies the Mira220 input to both drafts; nothing changes on the cameras
until Apply is pressed. **Reload current values** discards unsent edits and
reads the most recently confirmed settings.

- Range: **30..500,000 microseconds (500 ms)**, with analogue gain fixed at 1x.
- Linked means the same **requested time**, not identical register codes.
  The page reports each sensor's quantized estimated integration time.
- Longer exposures increase vertical blanking and lower acquisition FPS as
  needed. At 500 ms the cameras run at approximately 2 fps; the browser cannot
  show fresh images faster than acquisition. Format, resolution, gain and
  image-processing path do not change.
- Apply briefly stops/restarts capture and discards five warm-up frames. At
  long exposure this can take several seconds. Old frames are not presented
  as evidence of the new setting. Check the target and measured rates below
  each image after it becomes live again.
- Writes are serialized, range-checked and read back. A partial two-camera
  failure attempts to restore the previous settings. A failed rollback marks
  capture unavailable rather than reporting a successful linked update.
  A stale browser revision is rejected; use Reload current values before retrying.
- Settings are **preview-only**. Stopping preview normally restores the startup
  exposure and vertical blanking from its original configuration. Saved
  configuration TSVs and the raw recorder's maximum-rate limits are not changed.
  After an abrupt kill or a reported restore failure, run the configuration
  helper again before recording; never bypass a readback mismatch.

At a 1000 us exposure, an unlit scene or covered lens can look almost black:
there is deliberately no automatic display brightening. Check illumination and
lens caps, or apply a longer matched exposure in the controls above.
Do not confuse a dark but updating raw image with a
stopped stream.

The page distinguishes configured/native capture rate from browser refresh.
Unavailable or stale frames are marked explicitly; a frozen last image is not
proof of a live camera. Timing warnings should be investigated before recording,
but preview itself does not generate a verified recording manifest.

## Stop before recording

For the foreground command above, press **Ctrl+C in the Pi terminal running
the preview**. This stops its own capture processes, restores the startup
exposure/timing, and releases the camera lock. Check for any restoration error.

**Closing the browser or SSH tunnel does not stop a server running separately
on the Pi.** Do not try to start recording while preview still owns the cameras.

### Optional background service

To run preview independently of an SSH terminal, use a transient user service
instead of an untracked background shell:

```bash
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
systemd-run --user --unit=pitrac-focus-preview --collect \
  --property="WorkingDirectory=$HOME/PiTrac_V2/Software/camera-comparison" \
  /usr/bin/bash "$HOME/PiTrac_V2/Software/camera-comparison/scripts/focus-preview.sh" \
  --config "$CONFIG"
```

Inspect and stop that service with:

```bash
systemctl --user status pitrac-focus-preview.service --no-pager
journalctl --user -u pitrac-focus-preview.service --no-pager
systemctl --user stop pitrac-focus-preview.service
```

Do not start a second instance or overwrite an existing unit of that name
without checking what is running. The service is transient, not enabled at boot.

## Tests

From this component directory:

```bash
python3 -m unittest preview.test_server preview.test_exposure preview.test_capture
```

These tests cover raw frame boundaries, lossless transport, JPEG display mapping
and dimensions, mode selection, server session identity, latest-frame replacement,
stale/error handling, strict profile parsing and loopback HTTP behavior. Exposure
tests cover linked/unlinked updates, sensor timing and safe write ordering,
readback failures and two-camera rollback, same-origin/revision checks,
range validation, and startup-setting restoration. Capture tests verify every
RAW10 value, full native dimensions, padded-row decoding, byte-exact ZIP contents,
settings consistency, unavailable-camera rejection, and download controls.
JPEG and PNG capture tests require Pillow. Decoder tests can also run on a development PC with Node already
available:

```text
node --test web/decoder.test.mjs web/preview.test.mjs web/exposure.test.mjs web/capture.test.mjs
```

The Pi preview needs no Node installation. Validation on the inspected Pi and
PC confirmed both raw feeds through the private tunnel, approximately 89.08 and
60.38 fps acquisition, unchanged 1000 us/unity settings, and no image files.
Paired capture was additionally verified against live camera data: every PNG
sample matched its included native raw buffer at full resolution and bit depth.
The optimized bulk RAW10 unpacker kept both streams live with no added timing
warnings during that capture check; this is not a dropped-frame recording qualification.
The original raw-only browser display was approximately 4-5 fps with a 5 fps
request cap under the original test conditions. Later Wi-Fi measurements took
0.5-0.8 seconds to transfer a full raw frame, motivating the JPEG option. Live/error
state recovery after a service restart, native canvas dimensions, 1:1 and 2:1
display geometry, lossless transport, and camera-lock exclusion/release were
checked. This does not replace the recorder's separate full-frame validation.

Live exposure checks on the inspected Pi also passed: linked 10 ms retained
89.08/60.38 fps, linked 500 ms produced about 2 fps on both sensors, and unlinked
2 ms / 20 ms produced about 89.08/49.89 fps. Stopping preview restored the
original 1 ms configuration (Mira exposure 124 / VBLANK 18; IMX exposure 67 /
VBLANK 30), which then passed the existing recording helper's live-control check.
