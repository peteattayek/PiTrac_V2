#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
#
# mira220-trigger.sh against a fake i2ctransfer that keeps the sensor's registers in
# files: the bus taken from the configured entity, the exact write sequence, the bits
# kept and changed in 0x1001, the read-back check, and the refusals.
set -euo pipefail
SCRIPTS=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../scripts" && pwd)
source "$SCRIPTS/common.sh"
work=$(mktemp -d "${TMPDIR:-/tmp}/pitrac-trigger-tests.XXXXXXXX")
trap 'rm -r -- "$work"' EXIT
checks=0
equal() {
    [[ $1 == "$2" ]] || die "Expected '$2', got '$1'."
    ((checks+=1))
}
contains() {
    grep -Fq -- "$2" "$1" || die "Expected '$2' in $1: $(cat "$1")"
    ((checks+=1))
}
fails() {
    if ("$@") > "$work/out" 2>&1; then die "Expected failure: $*"; fi
    ((checks+=1))
}

mkdir -p "$work/bin" "$work/i2c" "$work/config"
cat > "$work/bin/i2ctransfer" <<'FAKE'
#!/usr/bin/env bash
# i2ctransfer -f -y BUS w2@0x54 HI LO r1  |  i2ctransfer -f -y BUS w3@0x54 HI LO VALUE
# Each sensor's registers live in FAKE_I2C/bus<BUS> when that exists (two cameras), else in FAKE_I2C.
state=$FAKE_I2C
if [[ ${3:-} =~ ^[0-9]+$ && -d $FAKE_I2C/bus$3 ]]; then state=$FAKE_I2C/bus$3; fi
printf '%s\n' "$*" >> "$state/calls"
[[ $1 == -f && $2 == -y && $3 =~ ^[0-9]+$ ]] || { echo "fake: bad flags" >&2; exit 9; }
[[ -f $state/powered ]] || { echo "Error: Read failed" >&2; exit 1; }
reg=$(printf '%02x%02x' "$5" "$6")
case $4 in
    w2@0x54)
        [[ $# == 7 && $7 == r1 ]] || exit 9
        if [[ -f $state/reg_$reg ]]; then cat "$state/reg_$reg"; else echo 0x00; fi ;;
    w3@0x54)
        [[ $# == 7 ]] || exit 9
        [[ -f $state/stuck_$reg ]] || printf '0x%02x\n' "$7" > "$state/reg_$reg" ;;
    *) exit 9 ;;
esac
FAKE
# Process start-up on some hosts takes longer than the tool's 40 ms wait, so the wait is
# checked by its place in the sequence and its length, not by a clock.
printf '#!/usr/bin/env bash\nprintf "sleep %%s\\n" "$*" >> "$FAKE_I2C/calls"\n' > "$work/bin/sleep"
chmod +x "$work/bin/i2ctransfer" "$work/bin/sleep"
export PATH="$work/bin:$PATH" FAKE_I2C="$work/i2c"
printf 'schema\t1\nsensor\tmira220\nentity\tmira220 6-0054\n' > "$work/config/mira220.tsv"
tool=$SCRIPTS/mira220-trigger.sh
reset_sensor() {
    rm -f "$work/i2c/"*
    touch "$work/i2c/powered"
    echo "$1" > "$work/i2c/reg_1001"
    echo 0x10 > "$work/i2c/reg_1003"
}
writes() { grep -F 'w3@' "$work/i2c/calls" | awk '{print $5, $6, $7}' | paste -sd ';' -; }

bash -n "$tool"
bash "$tool" --help > "$work/out"
contains "$work/out" "status|slave|master"

# status: reads only, on the bus from the entity.
reset_sensor 0x41
bash "$tool" --config "$work/config" status > "$work/out"
contains "$work/out" "Mira220 on bus 6, address 0x54"
contains "$work/out" "0x1001 0x41  bits 6 and 0 set"
equal "$(grep -c 'w3@' "$work/i2c/calls" || true)" 0
contains "$work/i2c/calls" "-f -y 6 w2@0x54 0x10 0x01 r1"

# slave: stop at a row boundary, stop, clear bits 6 and 0 keeping the others, slave, run.
reset_sensor 0x47
bash "$tool" --config "$work/config" slave > "$work/out"
equal "$(writes)" "0x10 0x03 0x02;0x10 0xf0 0x00;0x10 0x01 0x06;0x10 0x03 0x08;0x10 0xf0 0x01"
equal "$(cat "$work/i2c/reg_1001")" 0x06
contains "$work/out" "slave: 0x1001 0x47 -> 0x06, 0x1003 0x08, running."
contains "$work/out" "bits 6 and 0 clear"
equal "$(tail -n 1 "$work/i2c/calls")" "-f -y 6 w2@0x54 0x10 0x01 r1"
# The imager gets its 40 ms to stop before 0x1001 changes.
equal "$(grep -A1 -F 'w3@0x54 0x10 0xf0 0x00' "$work/i2c/calls" | tail -n 1)" "sleep 0.04"

# master: the same with both bits set and 0x1003 = 0x10; --bus instead of --config.
reset_sensor 0x06
bash "$tool" --bus 4 master > "$work/out"
equal "$(writes)" "0x10 0x03 0x02;0x10 0xf0 0x00;0x10 0x01 0x47;0x10 0x03 0x10;0x10 0xf0 0x01"
contains "$work/i2c/calls" "-f -y 4 w2@0x54"
contains "$work/out" "master: 0x1001 0x06 -> 0x47"

# Not streaming: the sensor does not answer, and nothing is written.
reset_sensor 0x41
rm "$work/i2c/powered"
fails bash "$tool" --config "$work/config" slave
contains "$work/out" "Is its stream running?"
equal "$(grep -c 'w3@' "$work/i2c/calls" || true)" 0

# 0x1001 does not take the write: refused, and the stopped imager is named.
reset_sensor 0x41
touch "$work/i2c/stuck_1001"
fails bash "$tool" --config "$work/config" slave
contains "$work/out" "0x1001 reads 0x41 after writing 0x00"
contains "$work/out" "The imager may be stopped"

# Arguments.
reset_sensor 0x41
fails bash "$tool" --config "$work/config"
fails bash "$tool" --config "$work/config" --bus 6 status
fails bash "$tool" slave
fails bash "$tool" --bus x status
fails bash "$tool" --bus 6 slave master
fails bash "$tool" --config "$work/config" start
printf 'schema\t1\nentity\tmira220 6-0055\n' > "$work/config/mira220.tsv"
fails bash "$tool" --config "$work/config" status
contains "$work/out" "expected 'mira220 <bus>-0054'"
rm "$work/config/mira220.tsv"
fails bash "$tool" --config "$work/config" status
contains "$work/out" "No mira220.tsv"

echo "test-trigger.sh: $checks checks passed"
