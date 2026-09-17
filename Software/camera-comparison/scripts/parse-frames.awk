# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
# Parse v4l2-ctl --verbose single-plane capture records, not ioctl query records.
function fail(message) {
    print "ERROR: " message > "/dev/stderr"
    bad=1
}
BEGIN {
    print "sequence\ttimestamp_seconds\tbytesused\tflags"
}
/^[[:space:]]*cap dqbuf:/ {
    seq=""; timestamp=""; used=""; flags=$0
    for (i=1; i<=NF; i++) {
        if ($i == "seq:") seq=$(i+1)
        if ($i == "ts:") timestamp=$(i+1)
        if ($i == "bytesused:") used=$(i+1)
    }
    if (seq !~ /^[0-9]+$/ || timestamp !~ /^[0-9]+[.][0-9]+$/ ||
        used !~ /^[0-9]+$/) {
        fail("unrecognized/multiplane frame record; need seq, ts and bytesused"); next
    }
    if (tolower($0) ~ /error/ || $0 ~ / offset:/) fail("error flag or unsupported data offset")
    if ($0 !~ /ts-monotonic/) fail("monotonic device timestamps are required")
    n++
    if (n <= warmup) next
    if (used != bytes) fail("bytesused differs from the negotiated sizeimage")
    if (retained) {
        if (seq != (previous_seq+1)%4294967296) fail("non-contiguous frame sequence")
        if (timestamp <= previous_ts) fail("non-increasing device timestamps")
        interval=timestamp-previous_ts
        if (interval < 0.5/target_fps || interval > 1.5/target_fps)
            fail("frame interval outside half to one-and-a-half target periods")
        if (!minimum_interval || interval<minimum_interval) minimum_interval=interval
        if (interval>maximum_interval) maximum_interval=interval
    } else first=timestamp
    previous_seq=seq; previous_ts=timestamp; last=timestamp; retained++
    sub(/^.* field: [^ ]+ */, "", flags)
    gsub(/\t/, " ", flags)
    printf "%s\t%s\t%s\t%s\n", seq, timestamp, used, flags
}
/[0-9]+ != [0-9]+/ { fail("v4l2-ctl reported a partial write") }
END {
    if (retained != expected || n != expected+warmup) fail("frame count does not match the requested retained count plus warm-up")
    if (retained < 2 || last <= first) fail("insufficient timestamp evidence")
    if (!bad) {
        fps=(retained-1)/(last-first)
        difference=fps/target_fps-1
        if (difference < -0.01 || difference > 0.01) fail("measured FPS differs by more than 1% from the timing target")
    }
    if (bad) exit 1
    printf "frames\t%d\nfps\t%.9f\nfirst_ts\t%.6f\nlast_ts\t%.6f\nspan_seconds\t%.6f\n",
        retained,fps,first,last,last-first > summary
    printf "min_interval_us\t%.6f\nmax_interval_us\t%.6f\n",
        minimum_interval*1e6,maximum_interval*1e6 > summary
}
