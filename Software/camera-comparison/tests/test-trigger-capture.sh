#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
#
# trigger-capture.sh end to end, off the Pi: a fake v4l2-ctl (fake_v4l2_ctl.py) that
# streams until the fake sensor is switched to slave and then emits one frame per
# trigger, the real mira220-trigger.sh against a fake i2ctransfer, and the real report.
# Only the Pi-identity, lock and media-graph helpers are stubbed.
set -euo pipefail
TESTS=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd -- "$TESTS/.." && pwd)
source "$ROOT/scripts/common.sh"
work=$(mktemp -d "${TMPDIR:-/tmp}/pitrac-capture-tests.XXXXXXXX")
cleanup() { [[ -z ${run:-} ]] || kill "$run" 2>/dev/null || true; rm -r -- "$work"; }
trap cleanup EXIT
checks=0
equal() {
    [[ $1 == "$2" ]] || die "Expected '$2', got '$1'."
    ((checks+=1))
}
contains() {
    grep -Fq -- "$2" "$1" || die "Expected '$2' in $1: $(cat "$1")"
    ((checks+=1))
}

W=8 H=4 SIZE=32
mkdir -p "$work/root" "$work/bin" "$work/i2c" "$work/config"
cp -r "$ROOT/scripts" "$ROOT/conversion" "$work/root/"
cat >> "$work/root/scripts/common.sh" <<STUBS

# --- test stubs: no Pi, no lock, no media graph ---
require_pi() { :; }
lock_cameras() { :; }
idle_graph() { :; }
assert_camera_live() { :; }
# Some test hosts (Git Bash on Windows) cannot set a new directory's mode.
new_directory() { mkdir -- "\$1" || die "Output directory must be new: \$1"; }
load_camera() {
    local entity bus
    entity=\$(value "\$1" entity)
    bus=\${entity#mira220 }; bus=\${bus%-0054}
    declare -gA CAM=([sensor]=mira220 [entity]="\$entity" [media]=/dev/media\$bus
        [video]=/dev/video\$bus [width]=$W [height]=$H [stride]=$W [sizeimage]=$SIZE [fourcc]=GREY)
}
STUBS
sed -n "/^cat > \"\$work\/bin\/i2ctransfer\" <<'FAKE'/,/^FAKE/p" "$TESTS/test-trigger.sh" | sed '1d;$d' \
    > "$work/bin/i2ctransfer"
printf '#!/usr/bin/env bash\nexec python3 "%s" "$@"\n' "$TESTS/fake_v4l2_ctl.py" > "$work/bin/v4l2-ctl"
for tool in media-ctl flock fuser; do printf '#!/usr/bin/env bash\nexit 0\n' > "$work/bin/$tool"; done
chmod +x "$work/bin/"*
export PATH="$work/bin:$PATH" FAKE_I2C="$work/i2c" FAKE_SIZE=$SIZE
printf 'CONFIGURED\n' > "$work/config/status"
printf 'schema\t1\nsensor\tmira220\nentity\tmira220 6-0054\n' > "$work/config/mira220.tsv"
capture=$work/root/scripts/trigger-capture.sh

# start NAME ARGS...: run trigger-capture.sh in the background on a fresh sensor.
start() {
    local name=$1; shift
    rm -rf "$work/i2c/"*
    touch "$work/i2c/powered"
    echo 0x41 > "$work/i2c/reg_1001"
    echo 0x10 > "$work/i2c/reg_1003"
    bash "$capture" --config "$work/config" --output "$work/$name" "$@" > "$work/$name.log" 2>&1 &
    run=$!
}
wait_for() {
    local deadline=$((SECONDS + 60))
    until grep -Fq -- "$2" "$1" 2>/dev/null; do
        ((SECONDS < deadline)) || die "Timed out waiting for '$2' in $1: $(cat "$1" 2>/dev/null)"
        sleep 0.2
    done
}
# finish: wait for the background run; its exit status in $rc. (Not in $(...): a subshell
# cannot wait for its parent's job.)
finish() {
    rc=0
    wait "$run" || rc=$?
    run=
}

bash -n "$capture"
bash "$capture" --help > "$work/help.txt"
contains "$work/help.txt" "--frames 3"

# Two triggers: two triggered frames, each a PGM of its own frame; the free-running ones kept.
start normal --frames 2
wait_for "$work/normal.log" "READY at"
touch "$work/i2c/trigger-1"
wait_for "$work/normal.log" "triggered frame 1 arrived"
sleep 0.5
touch "$work/i2c/trigger-2"
finish
equal "$rc" 0
contains "$work/normal.log" "slave: 0x1001 0x41 -> 0x00"
contains "$work/normal.log" "status: complete"
contains "$work/normal/switch.log" "running."
equal "$(value "$work/normal/summary.tsv" triggered_frames)" 2
# Every frame logged is whole in the raw file, the last included (the 2026-11-18 tail loss).
equal "$(value "$work/normal/summary.tsv" frames_in_raw)" "$(value "$work/normal/summary.tsv" frames_logged)"
equal "$(value "$work/normal/summary.tsv" free_running_after_ready)" no
equal "$(value "$work/normal/summary.tsv" status)" complete
free=$(value "$work/normal/summary.tsv" free_running_frames)
((free >= 1)) || die "Expected free-running frames before the switch, got $free."
((checks+=1))
# trigger-01.pgm holds frame number $free (0-based), so every pixel is that byte.
equal "$(tail -c "$((W * H))" "$work/normal/trigger-01.pgm" | od -An -tu1 -v | tr -s ' \n' ' ' | awk '{print $1, $NF}')" \
    "$free $free"
equal "$(head -c 11 "$work/normal/trigger-02.pgm" | tr '\n' ' ')" "P5 $W $H 255 "
equal "$(awk -F '\t' 'NR > 1 && $5 == "triggered"' "$work/normal/frames.tsv" | wc -l | tr -d ' ')" 2

# The switch does not take: frames keep coming after READY, and the script says so.
FAKE_MODE=stuck start stuck --frames 2
finish
equal "$rc" 0
contains "$work/stuck.log" "STOPPED: frames keep coming at a free-running rate after READY"
equal "$(value "$work/stuck/summary.tsv" status)" free-running
equal "$(value "$work/stuck/summary.tsv" free_running_after_ready)" yes

# The sensor does not answer: the switch fails, nothing is triggered, the capture is stopped.
start nosensor --frames 1
rm -f "$work/i2c/powered"
finish
[[ $rc != 0 ]] || die "Expected a failure when the sensor does not answer."
((checks+=1))
contains "$work/nosensor.log" "The switch to slave mode failed"
contains "$work/nosensor/switch.log" "Is its stream running?"

# Two cameras (bench 7c.2): one trigger line, a frame from each per trigger, each switched on
# its own bus, each camera's files in bus<N>/, and the pair report on top.
for b in 11 10; do
    mkdir -p "$work/config$b"
    printf 'CONFIGURED\n' > "$work/config$b/status"
    printf 'schema\t1\nsensor\tmira220\nentity\tmira220 %s-0054\n' "$b" > "$work/config$b/mira220.tsv"
done
start_pair() {
    local name=$1 b; shift
    rm -rf "$work/i2c/"*
    for b in 11 10; do
        mkdir -p "$work/i2c/bus$b"
        touch "$work/i2c/bus$b/powered"
        echo 0x41 > "$work/i2c/bus$b/reg_1001"
        echo 0x10 > "$work/i2c/bus$b/reg_1003"
    done
    bash "$capture" --config "$work/config11" --config "$work/config10" --output "$work/$name" "$@" \
        > "$work/$name.log" 2>&1 &
    run=$!
}
start_pair pair --frames 2
wait_for "$work/pair.log" "READY at"
contains "$work/pair.log" "One trigger exposes both cameras"
touch "$work/i2c/trigger-1"
wait_for "$work/pair.log" "bus11: triggered frame 1 arrived"
wait_for "$work/pair.log" "bus10: triggered frame 1 arrived"
sleep 0.5
touch "$work/i2c/trigger-2"
finish
equal "$rc" 0
contains "$work/pair.log" "status: complete"
for b in 11 10; do
    contains "$work/i2c/bus$b/calls" "-f -y $b w3@0x54 0x10 0x03 0x08"
    contains "$work/pair/bus$b/switch.log" "running."
    equal "$(value "$work/pair/bus$b/summary.tsv" triggered_frames)" 2
    equal "$(value "$work/pair/bus$b/summary.tsv" status)" complete
    equal "$(head -c 11 "$work/pair/bus$b/trigger-02.pgm" | tr '\n' ' ')" "P5 $W $H 255 "
done
equal "$(value "$work/pair/summary.tsv" cameras)" bus11,bus10
equal "$(value "$work/pair/summary.tsv" pairs)" 2
equal "$(value "$work/pair/summary.tsv" unpaired_bus11)" 0
equal "$(value "$work/pair/summary.tsv" unpaired_bus10)" 0
equal "$(value "$work/pair/summary.tsv" status)" complete
equal "$(wc -l < "$work/pair/pairs.tsv" | tr -d ' ')" 3

# Two cameras, one switch fails: nothing is triggered, and both captures are stopped.
start_pair pairfail --frames 1
rm -f "$work/i2c/bus10/powered"
finish
[[ $rc != 0 ]] || die "Expected a failure when one sensor does not answer."
((checks+=1))
contains "$work/pairfail.log" "The switch to slave mode failed for mira220 10-0054"

# Arguments.
for bad in "--frames 0" "--frames 51" "--wait-seconds 5" "--buffers 2"; do
    if bash "$capture" --config "$work/config" --output "$work/bad" $bad > "$work/bad.log" 2>&1; then
        die "Expected a refusal for $bad"
    fi
    ((checks+=1))
done
if bash "$capture" --config "$work/config11" --config "$work/config11" --output "$work/same" > "$work/bad.log" 2>&1; then
    die "Expected a refusal for the same camera twice"
fi
contains "$work/bad.log" "Both configurations name the same camera"
if bash "$capture" --config "$work/config" --config "$work/config11" --config "$work/config10" \
    --output "$work/three" > "$work/bad.log" 2>&1; then
    die "Expected a refusal for three cameras"
fi
contains "$work/bad.log" "twice for two"

echo "test-trigger-capture.sh: $checks checks passed"
