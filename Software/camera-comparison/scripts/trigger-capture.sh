#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
#
# trigger-capture.sh -- stream one or two configured Mira220s, switch each to REQ EXP-triggered
# (slave) exposure once its first frame arrives, and keep the frames that triggers produce.
# For PiTrac V2 Hardware/firmware/BENCH_P5_P7_MIC_CAMERA.md 7c.1 (one camera) and 7c.2 (two).
# Nothing on the Pi drives REQ EXP: the PiTrac board does, from its USB CLI ('cam trig',
# 'cam shot'), and its one trigger line reaches both cameras.
#
# v4l2-ctl runs WITHOUT --stream-poll, so it blocks until a frame arrives however long
# that takes (with --stream-poll it gives up after 2 s, which is why focus-preview.sh
# and record-dual.sh cannot capture triggered frames). The streams stop at the end,
# and the driver starts the next one free-running again.
#
# It writes the frames to v4l2-ctl's stdout, unbuffered (stdbuf -o0, --stream-to=-). Into a
# named file, v4l2-ctl keeps up to 4 KB of each frame in its stdio buffer until the next frame
# or a normal exit (it flushes per frame only when streaming to a host), so the TERM that ends
# the capture lost the last frame's tail: board 1, 2026-11-18, 3072 bytes each run.
# '-' also turns on its --silent, which only stops ioctl tracing; --verbose still logs.
#
# Two cameras: one process streams both (the camera lock is per user, so two instances cannot
# run side by side). Each camera's files go into bus<N>/, from its entity 'mira220 <N>-0054',
# and conversion/triggered_pairs.py matches each trigger's frame on one camera with the other's.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/common.sh"

usage() {
    echo "Usage: bash trigger-capture.sh --config CONFIG_DIR [--config CONFIG_DIR2] --output NEW_DIR"
    echo "       [--frames 3] [--wait-seconds 600] [--buffers 8]"
    echo "Streams the configured Mira220 (8-bit profile), or two Mira220s with two --config,"
    echo "switches each to triggered exposure after its first frame, prints READY, then keeps"
    echo "each frame a trigger produces. Ends when every camera has --frames triggered frames,"
    echo "after --wait-seconds, or on Ctrl-C."
    echo "Writes mira220.raw (every frame), capture.log, switch.log, frames.tsv,"
    echo "trigger-NN.pgm (each triggered frame) and summary.tsv. With two cameras these go"
    echo "into bus<N>/ for each, and pairs.tsv and summary.tsv describe the pair."
}

configs=() output= frames=3 wait_seconds=600 buffers=8
while (($#)); do
    case $1 in
        --help) usage; exit 0 ;;
        --config|--output|--frames|--wait-seconds|--buffers)
            (($# >= 2)) || die "Missing value for $1"
            case $1 in
                --config) configs+=("$2") ;; --output) output=$2 ;; --frames) frames=$2 ;;
                --wait-seconds) wait_seconds=$2 ;; --buffers) buffers=$2 ;;
            esac
            shift 2 ;;
        *) die "Unknown argument: $1 (--help for usage)" ;;
    esac
done
((${#configs[@]} >= 1)) || die "--config CONFIG_DIR is required."
((${#configs[@]} <= 2)) || die "Give --config once for one Mira220, or twice for two."
for config in "${configs[@]}"; do
    [[ -d $config ]] || die "--config $config is not a directory."
done
[[ -n $output ]] || die "--output NEW_DIR is required."
positive_integer "$frames" && ((frames <= 50)) || die "--frames must be 1..50."
positive_integer "$wait_seconds" && ((wait_seconds >= 10 && wait_seconds <= 3600)) ||
    die "--wait-seconds must be 10..3600."
positive_integer "$buffers" && ((buffers >= 4 && buffers <= 32)) || die "--buffers must be 4..32."
need v4l2-ctl media-ctl awk flock fuser stat python3 i2ctransfer stdbuf
require_pi
for i in "${!configs[@]}"; do
    [[ $(< "${configs[i]}/status") == CONFIGURED ]] || die "Camera configuration is incomplete: ${configs[i]}."
    configs[i]=$(cd -- "${configs[i]}" && pwd)
    [[ -f ${configs[i]}/mira220.tsv ]] ||
        die "No Mira220 in ${configs[i]} (configure-cameras.sh --camera mira220)."
done
lock_cameras

# What each camera needs after load_camera's CAM is gone: per-camera arrays, index i.
cams=${#configs[@]}
names=() entities=() videos=() widths=() heights=() strides=() sizes=() fourccs=()
for i in "${!configs[@]}"; do
    load_camera "${configs[i]}/mira220.tsv"
    [[ ${CAM[fourcc]} == GREY ]] || die "8-bit profile only: configure without --mira-bit-depth 12."
    assert_camera_live
    idle_graph "$(media-ctl -d "${CAM[media]}" -p)"
    [[ ${CAM[entity]} =~ ^mira220\ ([0-9]{1,3})-0054$ ]] ||
        die "Unexpected Mira220 entity '${CAM[entity]}'; expected 'mira220 <bus>-0054'."
    names[i]=bus${BASH_REMATCH[1]}
    entities[i]=${CAM[entity]}
    videos[i]=${CAM[video]}
    widths[i]=${CAM[width]} heights[i]=${CAM[height]} strides[i]=${CAM[stride]}
    sizes[i]=${CAM[sizeimage]} fourccs[i]=${CAM[fourcc]}
done
if ((cams == 2)); then
    [[ ${entities[0]} != "${entities[1]}" && ${videos[0]} != "${videos[1]}" ]] ||
        die "Both configurations name the same camera (${entities[0]}): configure each with --bus."
fi
new_directory "$output"
output=$(cd -- "$output" && pwd)
dirs=() prefix=()
for i in "${!configs[@]}"; do
    if ((cams == 1)); then
        dirs[i]=$output prefix[i]=
    else
        dirs[i]=$output/${names[i]} prefix[i]="${names[i]}: "
        new_directory "${dirs[i]}"
    fi
done

monotonic() { python3 -I -c 'import time; print(f"{time.monotonic():.6f}")'; }
captures=()
stop_capture() {
    local pid
    for pid in "${captures[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            # Background jobs of a script ignore SIGINT, so TERM. The frames go out unbuffered,
            # so every frame logged is already whole in mira220.raw.
            kill -TERM "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    captures=()
}
trap stop_capture EXIT
interrupted=0
trap 'interrupted=1' INT

# Every dequeued frame, in order, to mira220.raw; one 'cap dqbuf' line each in capture.log.
# stdbuf execs v4l2-ctl, so $! is v4l2-ctl itself.
for i in "${!configs[@]}"; do
    stdbuf -o0 v4l2-ctl -d "${videos[i]}" --verbose --stream-no-query "--stream-mmap=$buffers" \
        --stream-to=- > "${dirs[i]}/mira220.raw" 2> "${dirs[i]}/capture.log" &
    captures[i]=$!
done

# counts LOG READY_TS -> "<frames> <after ready> <first after ready> <last>"
counts() {
    awk -v ready="$2" '
        /cap dqbuf:/ { for (i = 1; i <= NF; i++) if ($i == "ts:") {
            n++; ts = $(i + 1) + 0
            if (ready != "" && ts > ready + 0) { t++; if (t == 1) first = ts; last = ts }
        } }
        END { printf "%d %d %.6f %.6f\n", n, t, first, last }
    ' "$1"
}

for i in "${!configs[@]}"; do
    echo "Streaming ${entities[i]} free-running; waiting for its first frame ..."
done
deadline=$((SECONDS + 5))
for i in "${!configs[@]}"; do
    until read -r n _ < <(counts "${dirs[i]}/capture.log" "") && ((n >= 1)); do
        kill -0 "${captures[i]}" 2>/dev/null ||
            die "v4l2-ctl stopped before ${entities[i]}'s first frame; see ${dirs[i]}/capture.log."
        ((SECONDS < deadline)) ||
            die "No frame from ${entities[i]} within 5 s: the stream did not start; see ${dirs[i]}/capture.log."
        sleep 0.05
    done
done

for i in "${!configs[@]}"; do
    if ! bash "$HERE/mira220-trigger.sh" --config "${configs[i]}" slave 2>&1 | tee "${dirs[i]}/switch.log"; then
        die "The switch to slave mode failed for ${entities[i]}; see ${dirs[i]}/switch.log. Nothing was triggered."
    fi
done
sleep 0.2       # frames already in flight arrive before READY
ready=$(monotonic)
before=
for i in "${!configs[@]}"; do
    read -r n_ready _ < <(counts "${dirs[i]}/capture.log" "")
    if ((cams == 1)); then before=$n_ready; else before+="${before:+ and }$n_ready (${names[i]})"; fi
done
echo
echo "READY at $ready s (CLOCK_MONOTONIC), after $before free-running frames."
echo "Now trigger from the board's CLI ('cam trig' or 'cam shot' as the bench step says)."
if ((cams == 2)); then
    echo "One trigger exposes both cameras: each should give one frame per trigger."
    echo "Waiting for $frames triggered frame(s) from each, up to $wait_seconds s; Ctrl-C ends early."
else
    echo "Waiting for $frames triggered frame(s), up to $wait_seconds s; Ctrl-C ends early."
fi

seen=() status=complete
for i in "${!configs[@]}"; do seen[i]=0; done
deadline=$((SECONDS + wait_seconds))
while :; do
    all_done=1 stop=0
    for i in "${!configs[@]}"; do
        read -r n t first last < <(counts "${dirs[i]}/capture.log" "$ready")
        while ((seen[i] < t)); do
            seen[i]=$((seen[i] + 1))
            echo "  $(date +%H:%M:%S) ${prefix[i]}triggered frame ${seen[i]} arrived"
        done
        # Triggers typed at a CLI come seconds apart; three or more frames under 50 ms apart on
        # average are the sensor's own frame rate. (The mean, not the span: a late poll sees many.)
        if ((t >= 3)) && awk -v a="$first" -v b="$last" -v n="$t" 'BEGIN { exit !((b - a) / (n - 1) < 0.05) }'; then
            status=free-running stop=1
            echo "STOPPED: ${prefix[i]}frames keep coming at a free-running rate after READY: slave mode did not take."
            break
        fi
        if ((t < frames)); then
            all_done=0
            if ! kill -0 "${captures[i]}" 2>/dev/null; then
                status=capture-ended stop=1
                echo "v4l2-ctl ended by itself; see ${dirs[i]}/capture.log."
                break
            fi
        fi
    done
    ((stop == 0)) || break
    ((all_done == 0)) || break
    if ((interrupted)); then status=interrupted; echo "Ctrl-C: ending with what arrived."; break; fi
    if ((SECONDS >= deadline)); then status=timed-out; echo "No more frames within $wait_seconds s."; break; fi
    sleep 0.2
done
sleep 0.3       # the last frame's write completes
stop_capture
trap - INT

echo
cd -- "$HERE/.."
for i in "${!configs[@]}"; do
    ((cams == 1)) || echo "${names[i]} (${entities[i]}):"
    python3 -m conversion.triggered_frames --log "${dirs[i]}/capture.log" --raw "${dirs[i]}/mira220.raw" \
        --out "${dirs[i]}" --ready-ts "$ready" --width "${widths[i]}" --height "${heights[i]}" \
        --stride "${strides[i]}" --sizeimage "${sizes[i]}" --fourcc "${fourccs[i]}" ||
        die "The report failed; the raw capture and logs are in ${dirs[i]}."
    # The report reads every frame logged, including any that came after the loop ended.
    if [[ $status != free-running && $(value "${dirs[i]}/summary.tsv" free_running_after_ready) == yes ]]; then
        status=free-running
        echo "STOPPED: ${prefix[i]}frames keep coming at a free-running rate after READY: slave mode did not take."
    fi
    ((cams == 1)) || echo
done
if ((cams == 2)); then
    echo "The pair:"
    python3 -m conversion.triggered_pairs --out "$output" \
        --camera "${names[0]}=${dirs[0]}/frames.tsv" --camera "${names[1]}=${dirs[1]}/frames.tsv" ||
        die "The pair report failed; each camera's report is in its own directory under $output."
    printf 'status\t%s\n' "$status" >> "$output/summary.tsv"
fi
for i in "${!configs[@]}"; do
    printf 'status\t%s\n' "$status" >> "${dirs[i]}/summary.tsv"
done
echo "status: $status"
if ((cams == 1)); then
    echo "Output: $output. The stream has stopped; the next one starts free-running."
else
    echo "Output: $output. The streams have stopped; the next ones start free-running."
fi
