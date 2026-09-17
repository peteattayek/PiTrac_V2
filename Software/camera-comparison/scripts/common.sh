#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors

export LC_ALL=C

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
need() {
    local tool
    for tool in "$@"; do
        command -v "$tool" >/dev/null || die "Missing command: $tool. See README.md prerequisites."
    done
}
positive_integer() { [[ $1 =~ ^[1-9][0-9]{0,8}$ ]]; }
number() { [[ $1 =~ ^[0-9]+([.][0-9]+)?$ && ${#1} -le 16 ]]; }
kv() {
    [[ $2 != *$'\n'* && $2 != *$'\r'* && $2 != *$'\t'* ]] || die "Unsupported whitespace in $1."
    printf '%s\t%s\n' "$1" "$2"
}
value() {
    awk -F '\t' -v key="$2" '
        $1 == key { if (NF != 2) exit 2; result=$2; count++ }
        END { if (count != 1 || result == "") exit 2; print result }
    ' "$1"
}
new_directory() {
    [[ $1 != *$'\n'* && $1 != *$'\r'* && $1 != *$'\t'* ]] || die "Invalid directory name."
    mkdir -m 700 -- "$1" || die "Output directory must be new, with an existing parent: $1"
}
require_pi() {
    [[ $(uname -s) == Linux && $(uname -m) == aarch64 ]] || die "Requires 64-bit Linux on the Pi 5."
    grep -aq 'Raspberry Pi 5' /proc/device-tree/model || die "This is not a Raspberry Pi 5."
    grep -qx 'VERSION_CODENAME=trixie' /etc/os-release || die "This setup targets Raspberry Pi OS Trixie."
}
lock_cameras() {
    local runtime=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
    [[ -d $runtime && $(stat -c %u "$runtime") == "$(id -u)" ]] ||
        die "A user-owned XDG_RUNTIME_DIR is required for the camera lock."
    exec 9>"$runtime/pitrac-camera-comparison.lock"
    flock -n 9 || die "Another comparison helper is configuring or recording."
}
idle_graph() {
    local node rc
    while read -r node; do
        if fuser -s -- "$node"; then
            die "A process has $node open. Close camera applications first."
        else
            rc=$?
            [[ $rc == 1 ]] || die "Cannot check whether $node is busy (fuser exit $rc)."
        fi
    done < <(awk '/device node name/ { print $NF }' <<< "$1")
}
control() {
    local output
    output=$(v4l2-ctl -d "$1" "--get-ctrl=$2") || return
    control_value "$2" <<< "$output"
}
control_value() {
    awk -v key="$1" '
        index($0, key ": ") == 1 {
            result=substr($0, length(key)+3); n++;
            if (result !~ /^[0-9]+( \(.*\))?$/) invalid=1;
            sub(/ .*/, "", result);
        }
        END { if (n != 1 || invalid) exit 2; print result }
    '
}
timing() {
    number "$2" || die "Exposure must be a positive decimal number in microseconds."
    awk -v sensor="$1" -v requested="$2" '
        BEGIN {
            if (sensor == "mira220") {
                row=304/38.4; offset=18.095; maximum=1411;
                fps=38400000/(304*1418); blank=18; gain=1;
            } else if (sensor == "imx296") {
                row=1100/74.25; offset=14.26; maximum=1114;
                fps=74250000/(1100*1118); blank=30; gain=0;
            } else exit 2;
            if (requested < row+offset || requested > maximum*row+offset) {
                printf "Exposure outside %s full-rate range %.6f..%.6f us\n",
                    sensor, row+offset, maximum*row+offset > "/dev/stderr";
                exit 2;
            }
            lines=int((requested-offset)/row+0.5);
            if (lines < 1 || lines > maximum) exit 2;
            printf "exposure_lines\t%d\nestimated_exposure_us\t%.6f\n", lines, lines*row+offset;
            printf "row_us\t%.9f\nexposure_offset_us\t%.6f\n", row, offset;
            printf "fps\t%.9f\nvblank\t%d\ngain_code\t%d\n", fps, blank, gain;
        }'
}
validate_shared_exposure() {
    timing mira220 "$1" >/dev/null || die "Exposure is outside the shared full-rate comparison range."
    timing imx296 "$1" >/dev/null || die "Exposure is outside the shared full-rate comparison range."
}
validate_storage_report() {
    local report=$1 boot=$2 kernel=$3 filesystem=$4 total=$5 rate=$6 measured tested
    [[ $(value "$report" schema) == 1 &&
       $(value "$report" method) == fio-direct-random-end-fsync &&
       $(value "$report" filesystem_device) == "$filesystem" &&
       $(value "$report" boot_id) == "$boot" &&
       $(value "$report" kernel) == "$kernel" ]] ||
        die "Storage report must match the recording boot, kernel and output filesystem."
    measured=$(value "$report" mbps)
    tested=$(value "$report" bytes)
    number "$measured" && number "$tested" || die "Invalid storage measurements."
    awk -v measured="$measured" -v rate="$rate" -v tested="$tested" -v total="$total" \
        'BEGIN {exit !(measured*1e6 >= 1.25*rate && tested >= 1.25*total)}' ||
        die "Storage test is too small or below the required 25% throughput headroom."
}
sensor_entity() {
    awk -v sensor="$1" '
        /- entity [0-9]+:/ {
            line=$0; sub(/^.*- entity [0-9]+: /, "", line); sub(/ \(.*/, "", line);
            if (line ~ ("^" sensor " [0-9]+-[0-9a-f]+$")) { print line; n++ }
        }
        END { if (n != 1) exit 2 }
    '
}
has_cfe_raw_source_pad() {
    awk '
        /- entity [0-9]+:/ { csi = ($0 ~ /- entity [0-9]+: csi2 \(/) }
        csi && $1 == "pad4:" && toupper($2) ~ /^SOURCE(,|$)/ { found=1 }
        END { exit !found }
    '
}
format_values() {
    awk '
        /Width\/Height[[:space:]]*:/ { split($NF, wh, "/"); w=wh[1]; h=wh[2]; nw++ }
        /Pixel Format[[:space:]]*:/ { split($0, a, "\047"); fmt=a[2]; nf++ }
        /Bytes per Line[[:space:]]*:/ { stride=$NF; ns++ }
        /Size Image[[:space:]]*:/ { size=$NF; ni++ }
        END {
            if (nw != 1 || nf != 1 || ns != 1 || ni != 1 ||
                w !~ /^[0-9]+$/ || h !~ /^[0-9]+$/ ||
                stride !~ /^[0-9]+$/ || size !~ /^[0-9]+$/) exit 2;
            printf "width\t%s\nheight\t%s\nfourcc\t%s\nstride\t%s\nsizeimage\t%s\n",
                w,h,fmt,stride,size;
        }'
}
load_camera() {
    local file=$1 key derived expected
    declare -gA CAM=()
    for key in schema sensor media entity subdev video width height fourcc stride sizeimage \
        exposure_lines estimated_exposure_us requested_exposure_us row_us exposure_offset_us \
        fps vblank gain_code hblank pixel_rate kernel boot_id; do
        CAM[$key]=$(value "$file" "$key") || die "Missing/duplicate/invalid $key in $file"
    done
    [[ ${CAM[schema]} == 1 ]] || die "Unsupported camera configuration schema."
    [[ ${CAM[sensor]} == mira220 || ${CAM[sensor]} == imx296 ]] || die "Unknown sensor."
    for key in width height stride sizeimage exposure_lines vblank hblank pixel_rate; do
        positive_integer "${CAM[$key]}" || die "Invalid $key in $file"
    done
    [[ ${CAM[gain_code]} == 0 || ${CAM[gain_code]} == 1 ]] || die "Invalid unity gain code."
    for key in estimated_exposure_us requested_exposure_us row_us exposure_offset_us fps; do
        number "${CAM[$key]}" || die "Invalid $key in $file"
    done
    [[ ${CAM[media]} =~ ^/dev/media[0-9]+$ &&
       ${CAM[subdev]} =~ ^/dev/v4l-subdev[0-9]+$ &&
       ${CAM[video]} =~ ^/dev/video[0-9]+$ ]] || die "Invalid device node in $file"
    if [[ ${CAM[sensor]} == mira220 ]]; then
        [[ ${CAM[width]} == 1600 && ${CAM[height]} == 1400 && ${CAM[fourcc]} == GREY &&
           ${CAM[gain_code]} == 1 && ${CAM[vblank]} == 18 &&
           ${CAM[hblank]} == 1440 && ${CAM[pixel_rate]} == 384000000 ]] ||
            die "Mira220 configuration is not the supported full-rate mono profile."
    else
        [[ ${CAM[width]} == 1456 && ${CAM[height]} == 1088 && ${CAM[fourcc]} == Y10P &&
           ${CAM[gain_code]} == 0 && ${CAM[vblank]} == 30 &&
           ${CAM[hblank]} == 304 && ${CAM[pixel_rate]} == 118800000 ]] ||
            die "IMX296 configuration is not the supported full-rate mono profile."
    fi
    (( CAM[stride] <= 65536 && CAM[sizeimage] == CAM[stride] * CAM[height] )) ||
        die "Unsupported padded/planar buffer layout."
    if [[ ${CAM[sensor]} == mira220 ]]; then expected=1600; else expected=1820; fi
    (( CAM[stride] >= expected && CAM[stride] % 16 == 0 )) || die "Invalid CFE row stride."
    validate_shared_exposure "${CAM[requested_exposure_us]}"
    derived=$(timing "${CAM[sensor]}" "${CAM[requested_exposure_us]}") || die "Invalid exposure request."
    for key in exposure_lines estimated_exposure_us row_us exposure_offset_us fps vblank gain_code; do
        expected=$(value /dev/stdin "$key" <<< "$derived")
        awk -v actual="${CAM[$key]}" -v expected="$expected" \
            'BEGIN {d=actual-expected; exit d < -0.000001 || d > 0.000001}' ||
            die "Configuration $key disagrees with the supported sensor timing model."
    done
}
assert_camera_live() {
    local fmt graph entity controls pad code
    [[ ${CAM[kernel]} == "$(uname -r)" && ${CAM[boot_id]} == "$(< /proc/sys/kernel/random/boot_id)" ]] ||
        die "Configuration belongs to another kernel/boot. Reconfigure cameras."
    graph=$(media-ctl -d "${CAM[media]}" -p)
    entity=$(sensor_entity "${CAM[sensor]}" <<< "$graph") || die "Sensor is missing/ambiguous."
    [[ $entity == "${CAM[entity]}" &&
       $(media-ctl -d "${CAM[media]}" -e "$entity") == "${CAM[subdev]}" &&
       $(media-ctl -d "${CAM[media]}" -e rp1-cfe-csi2_ch0) == "${CAM[video]}" ]] ||
        die "Media device identity changed. Reconfigure cameras."
    if [[ ${CAM[sensor]} == mira220 ]]; then code=Y8_1X8; else code=Y10_1X10; fi
    for pad in "\"$entity\":0" '"csi2":0' '"csi2":4'; do
        fmt=$(media-ctl -d "${CAM[media]}" --get-v4l2 "$pad")
        grep -Fq "fmt:$code/${CAM[width]}x${CAM[height]} " <<< "$fmt" ||
            die "Media pad format changed. Reconfigure cameras."
    done
    if [[ ${CAM[sensor]} == imx296 ]]; then
        [[ -r /sys/module/imx296/parameters/trigger_mode &&
           $(< /sys/module/imx296/parameters/trigger_mode) == 0 ]] ||
            die "IMX296 trigger mode must be disabled for free-running capture."
    fi
    fmt=$(v4l2-ctl -d "${CAM[video]}" --get-fmt-video | format_values)
    local key
    for key in width height fourcc stride sizeimage; do
        [[ $(value /dev/stdin "$key" <<< "$fmt") == "${CAM[$key]}" ]] ||
            die "Capture $key changed. Reconfigure cameras."
    done
    controls=$(v4l2-ctl -d "${CAM[subdev]}" \
        --get-ctrl=exposure,analogue_gain,vertical_blanking,horizontal_blanking,pixel_rate,test_pattern)
    for key in exposure analogue_gain vertical_blanking horizontal_blanking pixel_rate test_pattern; do
        local expected
        case $key in
            exposure) expected=${CAM[exposure_lines]} ;;
            analogue_gain) expected=${CAM[gain_code]} ;;
            vertical_blanking) expected=${CAM[vblank]} ;;
            horizontal_blanking) expected=${CAM[hblank]} ;;
            pixel_rate) expected=${CAM[pixel_rate]} ;;
            test_pattern) expected=0 ;;
        esac
        [[ $(control_value "$key" <<< "$controls") == "$expected" ]] ||
            die "Sensor $key changed or cannot be verified."
    done
}
