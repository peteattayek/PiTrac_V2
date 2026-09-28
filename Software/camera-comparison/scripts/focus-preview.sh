#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/common.sh"
config= port=8765 preview_fps=10 buffers=32 transport=jpeg jpeg_quality=90
while (($#)); do
    case $1 in
        --help)
            echo "Usage: bash focus-preview.sh --config CONFIG_DIR [--port 8765] [--preview-fps 10] [--buffers 32]"
            echo "       [--transport jpeg|raw] [--jpeg-quality 90] (60..95; JPEG requires python3-pil)"
            echo "Loopback-only browser preview; native raw acquisition, latest frames in RAM, no image files on Pi."
            echo "Optional Capture downloads full-bit-depth PNGs and raw buffers to the browser (requires Pillow)."
            echo "Use an SSH tunnel to view from another computer. Stop preview before recording."
            exit 0 ;;
        --config|--port|--preview-fps|--buffers|--transport|--jpeg-quality)
            (($# >= 2)) || die "Missing value for $1"
            case $1 in
                --config) config=$2 ;; --port) port=$2 ;;
                --preview-fps) preview_fps=$2 ;; --buffers) buffers=$2 ;;
                --transport) transport=$2 ;; --jpeg-quality) jpeg_quality=$2 ;;
            esac
            shift 2 ;;
        *) die "Unknown argument: $1" ;;
    esac
done
[[ -d $config ]] || die "--config CONFIG_DIR is required."
positive_integer "$port" && ((port >= 1024 && port <= 65535)) || die "Port must be 1024..65535."
positive_integer "$preview_fps" && ((preview_fps <= 10)) || die "Preview rate must be 1..10 fps."
validate_buffer_count "$buffers"
[[ $transport == jpeg || $transport == raw ]] || die "Transport must be jpeg or raw."
positive_integer "$jpeg_quality" && ((jpeg_quality >= 60 && jpeg_quality <= 95)) || die "JPEG quality must be 60..95."
need python3 v4l2-ctl media-ctl awk flock fuser stat
require_pi
[[ $(< "$config/status") == CONFIGURED ]] || die "Camera configuration is incomplete."
config=$(cd -- "$config" && pwd)
lock_cameras
count=0 memory_required=134217728 exposure=
for sensor in mira220 imx296; do
    [[ -f $config/$sensor.tsv ]] || continue
    load_camera "$config/$sensor.tsv"
    assert_camera_live
    graph=$(media-ctl -d "${CAM[media]}" -p)
    idle_graph "$graph"
    if [[ -n $exposure ]]; then
        awk -v a="$exposure" -v b="${CAM[requested_exposure_us]}" 'BEGIN {exit a!=b}' ||
            die "The cameras must use the same exposure request."
    fi
    exposure=${CAM[requested_exposure_us]}
    ((memory_required+=3 * buffers * CAM[sizeimage]))
    ((count+=1))
done
((count > 0)) || die "No supported cameras found in this configuration."
available=$(awk '/^MemAvailable:/ {printf "%.0f", $2*1024}' /proc/meminfo)
((available >= memory_required)) || die "Insufficient available RAM for preview buffers."
cd -- "$HERE/.."
exec python3 -u -m preview.server --config "$config" --port "$port" \
    --preview-fps "$preview_fps" --buffers "$buffers" --transport "$transport" --jpeg-quality "$jpeg_quality"
