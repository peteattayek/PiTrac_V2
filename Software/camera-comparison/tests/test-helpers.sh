#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
set -euo pipefail
SCRIPTS=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../scripts" && pwd)
source "$SCRIPTS/common.sh"
work=$(mktemp -d "${TMPDIR:-/tmp}/pitrac-camera-tests.XXXXXXXX")
trap 'rm -r -- "$work"' EXIT
checks=0
equal() {
    [[ $1 == "$2" ]] || die "Expected '$2', got '$1'."
    ((checks+=1))
}
near() {
    awk -v actual="$1" -v expected="$2" -v tolerance="$3" \
        'BEGIN {d=actual-expected; exit d < -tolerance || d > tolerance}' ||
        die "Expected $1 to be within $3 of $2."
    ((checks+=1))
}
fails() {
    if ("$@") > "$work/negative.stdout" 2> "$work/negative.stderr"; then
        die "Expected failure: $*"
    fi
    ((checks+=1))
}
for name in common.sh inspect-platform.sh configure-cameras.sh benchmark-storage.sh record-dual.sh verify-recording.sh buffered-writer.sh; do
    script=$SCRIPTS/$name
    bash -n "$script"
    bash "$script" --help > "$work/help.txt"
    ((checks+=1))
done
mira=$(timing mira220 1000)
imx=$(timing imx296 1000)
equal "$(value /dev/stdin exposure_lines <<< "$mira")" 124
equal "$(value /dev/stdin estimated_exposure_us <<< "$mira")" 999.761667
equal "$(value /dev/stdin exposure_lines <<< "$imx")" 67
equal "$(value /dev/stdin estimated_exposure_us <<< "$imx")" 1006.852593
equal "$(value /dev/stdin vblank <<< "$mira")" 18
equal "$(value /dev/stdin gain_code <<< "$imx")" 0
fails timing mira220 0
fails timing mira220 12000
fails timing imx296 20
fails timing mira220 '1;echo unsafe'
fails bash "$SCRIPTS/configure-cameras.sh" --calculate --gain 2
fails bash "$SCRIPTS/configure-cameras.sh" --calculate --apply
fails bash "$SCRIPTS/configure-cameras.sh" --calculate --camera imx296 --exposure-us 12000
fails bash "$SCRIPTS/configure-cameras.sh" --calculate --camera mira220 --exposure-us 27
timing mira220 11188.5 > "$work/upper-bound.tsv"
equal "$(value "$work/upper-bound.tsv" exposure_lines)" 1411
printf 'key\t1\nkey\t2\n' > "$work/duplicate.tsv"
fails value "$work/duplicate.tsv" key
equal "$(control_value test_pattern <<< 'test_pattern: 0 (Disabled)')" 0
equal "$(control_value exposure <<< 'exposure: 124')" 124
printf 'test_pattern: 0 (Disabled)\ntest_pattern: 1 (Gradient)\n' > "$work/controls.txt"
fails control_value test_pattern < "$work/controls.txt"
printf '%s\n' '- entity 42: mira220 6-0054 (1 pad, 1 link)' > "$work/topology.txt"
equal "$(sensor_entity mira220 < "$work/topology.txt")" 'mira220 6-0054'
printf '%s\n' '- entity 52: mira220 4-0054 (1 pad, 1 link)' >> "$work/topology.txt"
fails sensor_entity mira220 < "$work/topology.txt"
cat > "$work/cfe-topology.txt" <<'EOF'
- entity 1: csi2 (8 pads, 8 links, 0 routes)
        pad0: SINK
        pad4: SOURCE
- entity 10: pisp-fe (5 pads, 7 links, 0 routes)
        pad4: SOURCE,MUST_CONNECT
- entity 16: imx296 10-001a (1 pad, 1 link, 0 routes)
        pad0: SOURCE
EOF
has_cfe_raw_source_pad < "$work/cfe-topology.txt" || die "Uppercase CSI source pad was not recognized."
((checks+=1))
equal "$(sensor_entity imx296 < "$work/cfe-topology.txt")" 'imx296 10-001a'
sed 's/SOURCE/Source/g' "$work/cfe-topology.txt" > "$work/cfe-mixed-case.txt"
has_cfe_raw_source_pad < "$work/cfe-mixed-case.txt" || die "Mixed-case CSI source pad was not recognized."
((checks+=1))
sed 's/pad4: SOURCE$/pad4: SINK/' "$work/cfe-topology.txt" > "$work/cfe-wrong-pad.txt"
fails has_cfe_raw_source_pad < "$work/cfe-wrong-pad.txt"
printf '%s\n' '- entity 10: pisp-fe (5 pads, 7 links, 0 routes)' '        pad4: SOURCE' > "$work/cfe-missing.txt"
fails has_cfe_raw_source_pad < "$work/cfe-missing.txt"
check_idle_fixture() (
    local outcome=$1
    fuser() {
        [[ $# == 2 && $1 == -s && $2 == /dev/null ]] || return 2
        case $outcome in
            idle) return 1 ;;
            busy) return 0 ;;
            usage) echo 'No process specification given' >&2; return 1 ;;
            error) echo 'Cannot inspect process table' >&2; return 2 ;;
        esac
    }
    idle_graph '            device node name /dev/null'
)
check_idle_fixture idle
((checks+=1))
fails check_idle_fixture busy
fails check_idle_fixture usage
fails check_idle_fixture error
fails idle_graph 'No device nodes in this graph'
fails idle_graph 'device node name /dev/pitrac-camera-nonexistent-fixture'
for headroom in 0 19 101 020 25.0 invalid; do fails validate_headroom "$headroom"; done
for count in 0 7 33 032 8.0 invalid; do fails validate_buffer_count "$count"; done
validate_buffer_count 8
((checks+=1))
validate_buffer_count 32
((checks+=1))
printf 'schema\t1\n' > "$work/legacy-headroom.tsv"
equal "$(recorded_headroom "$work/legacy-headroom.tsv")" 25
printf 'min_headroom_percent\t20\nmin_headroom_percent\t25\n' > "$work/duplicate-headroom.tsv"
fails recorded_headroom "$work/duplicate-headroom.tsv"
cat > "$work/format.txt" <<'EOF'
Format Video Capture:
    Width/Height      : 1456/1088
    Pixel Format      : 'Y10P' (10-bit Greyscale (MIPI Packed))
    Bytes per Line    : 1824
    Size Image        : 1984512
EOF
format_values < "$work/format.txt" > "$work/format.tsv"
equal "$(value "$work/format.tsv" fourcc)" Y10P
equal "$(value "$work/format.tsv" sizeimage)" 1984512
awk 'BEGIN {
    for(i=0;i<4;i++)
        printf "cap dqbuf: 0 seq: %d bytesused: 16 ts: %.6f field: none (ts-monotonic, ts-src-soe)\n",
            i,1000000+i/50
}' > "$work/good.log"
parse() {
    awk -v warmup=1 -v expected="${2:-3}" -v bytes=16 -v target_fps=50 \
        -v summary="$work/parsed-summary.tsv" -f "$SCRIPTS/parse-frames.awk" "$1"
}
parse "$work/good.log" > "$work/frames.tsv"
equal "$(value "$work/parsed-summary.tsv" frames)" 3
equal "$(value "$work/parsed-summary.tsv" first_ts)" 1000000.020000
near "$(value "$work/parsed-summary.tsv" fps)" 50 0.000001
awk 'BEGIN {
    for(i=0;i<4;i++)
        printf "cap dqbuf: 0 seq: %.0f bytesused: 16 ts: %.6f field: none (ts-monotonic, ts-src-soe)\n",
            (4294967294+i)%4294967296,1000000+i/50
}' > "$work/wrap.log"
parse "$work/wrap.log" > "$work/wrap.tsv"
equal "$(value "$work/parsed-summary.tsv" frames)" 3
sed 's/seq: 3 /seq: 5 /' "$work/good.log" > "$work/gap.log"
fails parse "$work/gap.log"
sed 's/bytesused: 16/bytesused: 15/' "$work/good.log" > "$work/short.log"
fails parse "$work/short.log"
sed 's/ts-monotonic/error, ts-monotonic/' "$work/good.log" > "$work/error.log"
fails parse "$work/error.log"
sed 's/ts-monotonic/ts-copy/' "$work/good.log" > "$work/clock.log"
fails parse "$work/clock.log"
sed 's/1000000.060000/1000000.040000/' "$work/good.log" > "$work/time.log"
fails parse "$work/time.log"
sed '$d' "$work/good.log" > "$work/missing.log"
fails parse "$work/missing.log"
cp "$work/good.log" "$work/write-error.log"
echo '15 != 16' >> "$work/write-error.log"
fails parse "$work/write-error.log"
sed 's/1000000.060000/1000000.080000/' "$work/good.log" > "$work/slow.log"
fails parse "$work/slow.log"
awk 'BEGIN {
    for(i=0;i<202;i++)
        printf "cap dqbuf: 0 seq: %d bytesused: 16 ts: %.6f field: none (ts-monotonic, ts-src-soe)\n",
            i,1000000+(i+(i>=101))/50
}' > "$work/cadence-gap.log"
fails parse "$work/cadence-gap.log" 201

mkdir "$work/run"
run=$work/run
total=0
declare -A counts=()
for sensor in mira220 imx296; do
    settings=$(timing "$sensor" 1000)
    fps=$(value /dev/stdin fps <<< "$settings")
    if [[ $sensor == mira220 ]]; then
        width=1600 height=1400 stride=1600 fourcc=GREY hblank=1440 pixel_rate=384000000
    else
        width=1456 height=1088 stride=1824 fourcc=Y10P hblank=304 pixel_rate=118800000
    fi
    size=$((stride*height))
    count=$(awk -v fps="$fps" 'BEGIN {n=2*fps; print int(n)+(n>int(n))+1}')
    counts[$sensor]=$count
    ((total+=count*size))
    {
        kv schema 1; kv sensor "$sensor"; kv media /dev/media0; kv entity "$sensor 6-0054"
        kv subdev /dev/v4l-subdev0; kv video /dev/video0
        kv width "$width"; kv height "$height"; kv stride "$stride"; kv fourcc "$fourcc"
        kv sizeimage "$size"; kv requested_exposure_us 1000
        kv hblank "$hblank"; kv pixel_rate "$pixel_rate"; kv kernel fixture; kv boot_id fixture
        printf '%s\n' "$settings"
    } > "$run/$sensor.tsv"
    awk -v frames="$((count+5))" -v bytes="$size" -v fps="$fps" 'BEGIN {
        for(i=0;i<frames;i++)
            printf "cap dqbuf: 0 seq: %d bytesused: %d ts: %.6f field: none (ts-monotonic, ts-src-soe)\n",
                i,bytes,1000000+i/fps
    }' > "$run/$sensor.capture.log"
done
manifest() {
    hash_files=(run.tsv mira220.tsv imx296.tsv)
    [[ ! -f $run/storage.tsv ]] || hash_files+=(storage.tsv)
    (cd -- "$run" && sha256sum "${hash_files[@]}") > "$run/manifest.sha256"
}
{
    kv schema 1; kv seconds 1; kv warmup 5; kv sink 1; kv cameras 'mira220 imx296'
    kv expected_total_bytes "$total"
    kv boot_id fixture; kv kernel fixture; kv filesystem_device 1234
    kv mira220.frames "${counts[mira220]}"; kv imx296.frames "${counts[imx296]}"
} > "$run/run.tsv"
printf 'controls_readback_passed\t1\ndurable\t0\n' > "$run/completion.tsv"
manifest
bash "$SCRIPTS/verify-recording.sh" --run "$run"
equal "$(< "$run/status")" VERIFIED_SINK_ONLY
cp "$run/mira220.tsv" "$work/mira-original.tsv"
sed 's/gain_code\t1/gain_code\t0/' "$work/mira-original.tsv" > "$run/mira220.tsv"
manifest
fails bash "$SCRIPTS/verify-recording.sh" --run "$run"
equal "$(< "$run/status")" 'INVALID: verification failed'
cp "$work/mira-original.tsv" "$run/mira220.tsv"
sed 's/fps\t89.080246455/fps\t60/' "$work/mira-original.tsv" > "$run/mira220.tsv"
manifest
fails bash "$SCRIPTS/verify-recording.sh" --run "$run"
cp "$work/mira-original.tsv" "$run/mira220.tsv"
cp "$run/imx296.capture.log" "$work/imx-log-original.txt"
awk '{for(i=1;i<=NF;i++) if($i=="ts:") $(i+1)=sprintf("%.6f",$(i+1)+3); print}' \
    "$work/imx-log-original.txt" > "$run/imx296.capture.log"
manifest
fails bash "$SCRIPTS/verify-recording.sh" --run "$run"
cp "$work/imx-log-original.txt" "$run/imx296.capture.log"
cp "$run/completion.tsv" "$work/completion-original.tsv"
printf 'controls_readback_passed\t0\ndurable\t0\n' > "$run/completion.tsv"
fails bash "$SCRIPTS/verify-recording.sh" --run "$run"
cp "$work/completion-original.tsv" "$run/completion.tsv"
printf 'tampered\t1\n' >> "$run/run.tsv"
fails bash "$SCRIPTS/verify-recording.sh" --run "$run"
sed '/^tampered\t/d' "$run/run.tsv" > "$work/untampered.tsv"
cp "$work/untampered.tsv" "$run/run.tsv"
sed 's/sink\t1/sink\t0/' "$run/run.tsv" > "$work/disk.tsv"
cp "$work/disk.tsv" "$run/run.tsv"
printf 'controls_readback_passed\t1\ndurable\t1\n' > "$run/completion.tsv"
{
    kv schema 1; kv method fio-direct-random-end-fsync; kv mbps 500; kv bytes 30000000000
    kv boot_id fixture; kv kernel fixture; kv filesystem_device 1234
} > "$run/storage.tsv"
manifest
for sensor in mira220 imx296; do
    size=$(value "$run/$sensor.tsv" sizeimage)
    truncate -s "$((size * counts[$sensor]))" "$run/$sensor.raw"
done
bash "$SCRIPTS/verify-recording.sh" --run "$run"
equal "$(< "$run/status")" VERIFIED_RECORDING
cp "$run/storage.tsv" "$work/storage-original.tsv"
cp "$run/run.tsv" "$work/run-original.tsv"
sed 's/mbps\t500/mbps\t397.022302/' "$work/storage-original.tsv" > "$run/storage.tsv"
manifest
fails bash "$SCRIPTS/verify-recording.sh" --run "$run"
printf 'min_headroom_percent\t20\n' >> "$run/run.tsv"
manifest
bash "$SCRIPTS/verify-recording.sh" --run "$run"
equal "$(< "$run/status")" VERIFIED_RECORDING_REDUCED_HEADROOM
equal "$(value "$run/verification.tsv" min_headroom_percent)" 20
sed 's/min_headroom_percent\t20/min_headroom_percent\t19/' "$run/run.tsv" > "$work/invalid-headroom.tsv"
cp "$work/invalid-headroom.tsv" "$run/run.tsv"
manifest
fails bash "$SCRIPTS/verify-recording.sh" --run "$run"
cp "$work/run-original.tsv" "$run/run.tsv"
sed 's/mbps\t500/mbps\t100/' "$work/storage-original.tsv" > "$run/storage.tsv"
manifest
fails bash "$SCRIPTS/verify-recording.sh" --run "$run"
sed 's/boot_id\tfixture/boot_id\twrong-boot/' "$work/storage-original.tsv" > "$run/storage.tsv"
manifest
fails bash "$SCRIPTS/verify-recording.sh" --run "$run"
rm -- "$run/storage.tsv"
manifest
fails bash "$SCRIPTS/verify-recording.sh" --run "$run"
cp "$work/storage-original.tsv" "$run/storage.tsv"
manifest
truncate -s 1 "$run/mira220.raw"
fails bash "$SCRIPTS/verify-recording.sh" --run "$run"
printf '\nAll %d offline helper checks passed (synthetic fixtures; no Pi hardware tested).\n' "$checks"
