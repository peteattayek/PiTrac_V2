#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/common.sh"
if [[ ${1:-} == --help ]]; then
    echo "Usage: bash verify-recording.sh --run RUN_DIRECTORY"
    echo "Offline verification of logs, controls, timestamps, overlap and raw file sizes."
    exit 0
fi
[[ $# == 2 && $1 == --run && -d $2 ]] || die "Use --help for usage."
run=$(cd -- "$2" && pwd)
need awk stat sha256sum
trap 'rc=$?; if ((rc)); then printf "INVALID: verification failed\n" > "$run/status"; fi' EXIT
(cd -- "$run" && sha256sum --check --strict --status manifest.sha256) ||
    die "Run/configuration manifest changed or its checksum is missing."
[[ $(value "$run/run.tsv" schema) == 1 ]] || die "Invalid run schema."
seconds=$(value "$run/run.tsv" seconds)
warmup=$(value "$run/run.tsv" warmup)
sink=$(value "$run/run.tsv" sink)
positive_integer "$seconds" && ((seconds <= 60)) || die "Invalid recording duration."
[[ $warmup == 5 && ( $sink == 0 || $sink == 1 ) ]] || die "Unsupported warm-up or recording mode."
[[ $(value "$run/completion.tsv" controls_readback_passed) == 1 &&
   $(value "$run/completion.tsv" durable) == "$((1-sink))" ]] || die "Recording did not complete and flush successfully."
selection=$(value "$run/run.tsv" cameras)
case $selection in
    mira220|imx296|'mira220 imx296') read -r -a sensors <<< "$selection" ;;
    *) die "Invalid camera selection in manifest." ;;
esac
total=0 rate=0 first=0 last=0 min_exposure= max_exposure= requested=
for sensor in "${sensors[@]}"; do
    load_camera "$run/$sensor.tsv"
    frames=$(value "$run/run.tsv" "$sensor.frames")
    positive_integer "$frames" || die "Invalid expected frame count."
    awk -v warmup="$warmup" -v expected="$frames" -v bytes="${CAM[sizeimage]}" \
        -v target_fps="${CAM[fps]}" -v summary="$run/$sensor.summary.tsv" \
        -f "$HERE/parse-frames.awk" "$run/$sensor.capture.log" > "$run/$sensor.frames.tsv" ||
        die "$sensor frame evidence failed verification."
    if ((!sink)); then
        [[ $(stat -c %s "$run/$sensor.raw") == "$((frames * CAM[sizeimage]))" ]] ||
            die "$sensor raw file size does not match retained frames."
    fi
    ((total+=frames * CAM[sizeimage]))
    rate=$(awk -v total="$rate" -v fps="${CAM[fps]}" -v size="${CAM[sizeimage]}" \
        'BEGIN {printf "%.6f", total+fps*size}')
    a=$(value "$run/$sensor.summary.tsv" first_ts)
    b=$(value "$run/$sensor.summary.tsv" last_ts)
    first=$(awk -v old="$first" -v candidate="$a" 'BEGIN {printf "%.6f", (old>candidate ? old : candidate)}')
    last=$(awk -v old="$last" -v candidate="$b" 'BEGIN {printf "%.6f", (old==0 || candidate<old ? candidate : old)}')
    if [[ -n $requested ]]; then
        awk -v a="$requested" -v b="${CAM[requested_exposure_us]}" 'BEGIN {exit a!=b}' ||
            die "Cameras did not use the same exposure request."
    fi
    requested=${CAM[requested_exposure_us]}
    min_exposure=$(awk -v old="${min_exposure:-0}" -v candidate="${CAM[estimated_exposure_us]}" \
        'BEGIN {printf "%.6f", (old==0 || candidate<old ? candidate : old)}')
    max_exposure=$(awk -v old="${max_exposure:-0}" -v candidate="${CAM[estimated_exposure_us]}" \
        'BEGIN {printf "%.6f", (candidate>old ? candidate : old)}')
done
[[ $(value "$run/run.tsv" expected_total_bytes) == "$total" ]] || die "Total byte budget does not match files."
if ((!sink)); then
    validate_storage_report "$run/storage.tsv" "$(value "$run/run.tsv" boot_id)" \
        "$(value "$run/run.tsv" kernel)" "$(value "$run/run.tsv" filesystem_device)" "$total" "$rate"
fi
overlap=$(awk -v first="$first" -v last="$last" 'BEGIN {printf "%.6f", last-first}')
awk -v overlap="$overlap" -v seconds="$seconds" 'BEGIN {exit overlap < seconds}' ||
    die "Retained streams overlap for less than the requested duration."
awk -v a="$min_exposure" -v b="$max_exposure" 'BEGIN {exit b-a > 1100/74.25+0.000001}' ||
    die "Estimated exposures differ by more than one IMX296 exposure-line interval."
{
    kv overlap_seconds "$overlap"
    kv exposure_difference_us "$(awk -v a="$min_exposure" -v b="$max_exposure" 'BEGIN {printf "%.6f", b-a}')"
    kv calculated_exposure_not_optically_measured 1
} > "$run/verification.tsv"
if ((sink)); then
    printf 'VERIFIED_SINK_ONLY\n' > "$run/status"
    echo "Verified capture-to-sink evidence only; storage was not tested."
else
    printf 'VERIFIED_RECORDING\n' > "$run/status"
    echo "Verified recording: exact formats, frame counts, sizes, FPS, controls and $overlap seconds of overlap."
fi
