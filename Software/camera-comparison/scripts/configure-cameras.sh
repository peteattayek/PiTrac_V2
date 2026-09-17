#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/common.sh"

output= exposure=1000 gain=1 selection=both apply=0 calculate=0
while (($#)); do
    case $1 in
        --help)
            echo "Usage: bash configure-cameras.sh --output NEW_DIR --apply [--camera both|mira220|imx296]"
            echo "       [--exposure-us 1000] [--gain 1]"
            echo "       bash configure-cameras.sh --calculate [--exposure-us 1000]"
            echo "--calculate is offline; --apply resets mutable links only on each selected CFE graph."
            exit 0 ;;
        --output|--exposure-us|--gain|--camera)
            (($# >= 2)) || die "Missing value for $1"
            case $1 in
                --output) output=$2 ;; --exposure-us) exposure=$2 ;;
                --gain) gain=$2 ;; --camera) selection=$2 ;;
            esac
            shift 2 ;;
        --apply) apply=1; shift ;;
        --calculate) calculate=1; shift ;;
        *) die "Unknown argument: $1" ;;
    esac
done
[[ $gain == 1 ]] || die "Matched comparison supports only 1x analogue gain; no digital substitute."
[[ $selection == both || $selection == mira220 || $selection == imx296 ]] || die "Invalid camera selection."
validate_shared_exposure "$exposure"
sensors=(mira220 imx296)
[[ $selection == both ]] || sensors=("$selection")
for sensor in "${sensors[@]}"; do
    settings=$(timing "$sensor" "$exposure") || die "Unsupported exposure."
    if ((calculate)); then printf '\n%s (estimates, not optical measurements)\n%s\n' "$sensor" "$settings"; fi
done
if ((calculate)); then
    ((apply == 0)) || die "Do not combine --calculate and --apply."
    exit 0
fi
[[ -n $output && $apply == 1 ]] || die "Use --output NEW_DIR --apply, or --calculate."
need media-ctl v4l2-ctl awk flock fuser stat
require_pi
lock_cameras
new_directory "$output"
output=$(cd -- "$output" && pwd)
trap 'rc=$?; if ((rc)); then echo "INVALID: configuration failed; inspect logs and reconfigure into a new directory." > "$output/status"; fi' EXIT
printf 'CONFIGURING\n' > "$output/status"
shopt -s nullglob
devices=(/dev/media*)
declare -A graphs=() entities=()
for sensor in "${sensors[@]}"; do
    count=0
    for device in "${devices[@]}"; do
        graph=$(media-ctl -d "$device" -p) || die "Cannot inspect $device"
        if entity=$(sensor_entity "$sensor" <<< "$graph"); then
            ((count+=1))
            graphs[$sensor]=$device
            entities[$sensor]=$entity
        fi
    done
    ((count == 1)) || die "Expected exactly one $sensor; found $count. Run inspection first."
done
if ((${#sensors[@]} == 2)); then
    [[ ${graphs[mira220]} != "${graphs[imx296]}" ]] || die "Cameras must use separate CFE receivers."
fi
for sensor in "${sensors[@]}"; do
    device=${graphs[$sensor]} entity=${entities[$sensor]}
    graph=$(media-ctl -d "$device" -p)
    grep -Eq '^model[[:space:]]+rp1-cfe' <<< "$graph" || die "Unsupported receiver; expected downstream rp1-cfe."
    grep -Fq 'rp1-cfe-csi2_ch0' <<< "$graph" || die "Raw channel 0 is missing."
    grep -Eq 'pad4: Source' <<< "$graph" || die "Unsupported CFE pad layout."
    idle_graph "$graph"
    printf '%s\n' "$graph" > "$output/$sensor.before-topology.txt"
    subdev=$(media-ctl -d "$device" -e "$entity")
    video=$(media-ctl -d "$device" -e rp1-cfe-csi2_ch0)
    v4l2-ctl -d "$subdev" --list-ctrls > "$output/$sensor.controls.txt"
    settings=$(timing "$sensor" "$exposure")
    lines=$(value /dev/stdin exposure_lines <<< "$settings")
    blank=$(value /dev/stdin vblank <<< "$settings")
    if [[ $sensor == mira220 ]]; then
        width=1600 height=1400 code=Y8_1X8 fourcc=GREY depth=8 hblank=1440 pixel_rate=384000000
    else
        width=1456 height=1088 code=Y10_1X10 fourcc=Y10P depth=10 hblank=304 pixel_rate=118800000
    fi
    # This is the validated-in-source downstream CFE profile, not an arbitrary media graph.
    media-ctl -d "$device" --reset
    media-ctl -d "$device" --links "\"$entity\":0 -> \"csi2\":0 [1], \"csi2\":4 -> \"rp1-cfe-csi2_ch0\":0 [1]"
    if [[ $sensor == imx296 ]]; then
        media-ctl -d "$device" --set-v4l2 "\"$entity\":0 [crop:(0,0)/1456x1088]"
        actual=$(media-ctl -d "$device" --get-v4l2 "\"$entity\":0")
        grep -Fq 'crop:(0,0)/1456x1088' <<< "$actual" || die "Could not restore the IMX296 full active crop."
    fi
    for pad in "\"$entity\":0" '"csi2":0' '"csi2":4'; do
        media-ctl -d "$device" --set-v4l2 "$pad [fmt:$code/${width}x${height} field:none]"
        actual=$(media-ctl -d "$device" --get-v4l2 "$pad")
        grep -Fq "fmt:$code/${width}x${height} " <<< "$actual" ||
            die "$sensor pad $pad did not retain $code/${width}x${height}; check mono OTP and graph."
    done
    [[ $(control "$subdev" horizontal_blanking) == "$hblank" &&
       $(control "$subdev" pixel_rate) == "$pixel_rate" ]] || die "$sensor timing differs from the supported profile."
    v4l2-ctl -d "$subdev" "--set-ctrl=vertical_blanking=$blank"
    if [[ $sensor == imx296 ]]; then v4l2-ctl -d "$subdev" --set-ctrl=analogue_gain=0; fi
    v4l2-ctl -d "$subdev" "--set-ctrl=exposure=$lines,test_pattern=0"
    v4l2-ctl -d "$video" "--set-fmt-video=width=$width,height=$height,pixelformat=$fourcc"
    actual=$(v4l2-ctl -d "$video" --get-fmt-video)
    printf '%s\n' "$actual" > "$output/$sensor.video-format.txt"
    fmt=$(format_values <<< "$actual") || die "Unsupported video format report."
    {
        kv schema 1; kv sensor "$sensor"; kv media "$device"; kv entity "$entity"
        kv subdev "$subdev"; kv video "$video"; kv native_depth "$depth"
        kv requested_exposure_us "$exposure"; kv hblank "$hblank"; kv pixel_rate "$pixel_rate"
        kv kernel "$(uname -r)"; kv boot_id "$(< /proc/sys/kernel/random/boot_id)"
        printf '%s\n%s\n' "$fmt" "$settings"
    } > "$output/$sensor.tsv"
    load_camera "$output/$sensor.tsv"
    assert_camera_live
    media-ctl -d "$device" -p > "$output/$sensor.topology.txt"
done
printf 'CONFIGURED\n' > "$output/status"
echo "Configured: $output. This does not yet prove streaming or storage throughput."
