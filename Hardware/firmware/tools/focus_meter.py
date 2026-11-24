"""Live focus score for the hold-flash preview (BRINGUP-style 'peak and lock', The_One_Board §18.2).

Reads each new flash-lit frame from the preview through the SSH tunnel and prints a sharpness
score: normalised gradient energy, mean(|grad I|^2) / mean(I)^2 over a region of interest, with
the black level subtracted. Only lit frames are scored (mean >= the preview's flash baseline + 3).
Turn the lens until the score peaks; "<<< LOCK" marks scores within 95 % of the best seen.

Usage: python focus_meter.py [--url http://127.0.0.1:8766] [--roi x,y,w,h] [--black 14]
The default ROI is a 500 x 500 box centred on the brightest area of the first lit frame
(the ball, in the current scene), then fixed. Ctrl-C ends it. Restart it for the other camera.
"""
import argparse
import collections
import gzip
import json
import sys
import time
import urllib.error
import urllib.request

import numpy as np

W, H = 1600, 1400


def get(url, timeout=5.0):
    req = urllib.request.Request(url, headers={"Accept-Encoding": "gzip"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        body = r.read()
        if r.headers.get("Content-Encoding") == "gzip":
            body = gzip.decompress(body)
        return r.status, r.headers, body


def auto_roi(img, size=500):
    small = img[: H - H % 50, : W - W % 50].reshape(H // 50, 50, W // 50, 50).mean(axis=(1, 3))
    cy, cx = np.unravel_index(np.argmax(small), small.shape)
    x = int(min(max(cx * 50 + 25 - size // 2, 0), W - size))
    y = int(min(max(cy * 50 + 25 - size // 2, 0), H - size))
    return x, y, size, size


def score(img, roi, black):
    x, y, w, h = roi
    a = np.clip(img[y:y + h, x:x + w] - black, 0, None)
    gx = a[1:-1, 2:] - a[1:-1, :-2]
    gy = a[2:, 1:-1] - a[:-2, 1:-1]
    m = max(float(a.mean()), 0.5)
    return 1000.0 * float((gx * gx + gy * gy).mean()) / (m * m), float(a.mean())


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--url", default="http://127.0.0.1:8766")
    p.add_argument("--roi", default="")
    p.add_argument("--black", type=float, default=14.0)
    args = p.parse_args()
    roi = tuple(int(v) for v in args.roi.split(",")) if args.roi else None
    after, peak, baseline, last_status = 0, 0.0, None, 0.0
    recent = collections.deque(maxlen=10)
    print("Waiting for flash-lit frames from", args.url, "(start strobe_focus.ps1 if not running)")
    while True:
        try:
            if time.monotonic() - last_status > 2.0:
                _, _, body = get(args.url + "/api/status")
                cam = json.loads(body)["cameras"][0]
                baseline, last_status = cam.get("flash_baseline"), time.monotonic()
                if not cam.get("hold_flash"):
                    sys.exit("The preview is not in --hold-flash mode.")
            status, headers, body = get(f"{args.url}/api/frame/mira220?after={after}&format=raw")
            if status == 204 or len(body) != W * H:
                time.sleep(0.1)
                continue
            after = int(headers.get("X-Frame-Id", after))
            img = np.frombuffer(body, np.uint8).reshape(H, W).astype(np.float32)
            mean_all = float(img[::7, ::7].mean())
            if baseline is not None and mean_all < baseline + 3.0:
                time.sleep(0.1)
                continue  # not a lit frame (the hold expired)
            # Dim frames score falsely high (noise): strobe_focus's cal climbs with 20 us pulses,
            # and at 11 ms exposure a flash can straddle the end of a frame and light it only
            # partly. Full flashes repeat within ~0.3 %, so score only frames within 10 % of the
            # brightest of the last 10, and start again (ROI, peak) when frames get much brighter.
            ref = max(recent) if recent else 0.0
            if mean_all > ref / 0.75:
                if ref > 0:
                    print("  (much brighter flashes now: ROI and peak reset)")
                recent.clear()
                peak = 0.0
                if not args.roi:
                    roi = None
            recent.append(mean_all)
            if mean_all < 0.9 * max(recent):
                print(f"frame {after:7d}  partial or dim flash (mean {mean_all:.1f}): not scored", flush=True)
                continue
            if roi is None:
                roi = auto_roi(img)
                print(f"ROI x,y,w,h = {roi[0]},{roi[1]},{roi[2]},{roi[3]}  (fixed now; --roi to choose)")
            s, m = score(img, roi, args.black)
            peak = max(peak, s)
            bar = "#" * int(40 * s / peak) if peak > 0 else ""
            lock = "  <<< LOCK (within 95 % of peak)" if s >= 0.95 * peak else ""
            print(f"frame {after:7d}  ROI mean {m:6.1f}  score {s:8.2f}  peak {peak:8.2f}  [{bar:<40}]{lock}",
                  flush=True)
        except (urllib.error.URLError, ConnectionError, TimeoutError, OSError) as e:
            print("  (no preview at", args.url, "-", getattr(e, "reason", e), ") retrying ...", flush=True)
            time.sleep(1.0)
        except KeyboardInterrupt:
            print(f"\nbest score {peak:.2f}")
            return


if __name__ == "__main__":
    main()
