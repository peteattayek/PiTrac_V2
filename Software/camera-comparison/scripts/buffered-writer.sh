#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/common.sh"
if [[ ${1:-} == --help ]]; then
    echo "Usage: bash buffered-writer.sh INPUT_FIFO NEW_RAW_FILE"
    echo "Internal writer: 128 MiB bounded buffer, 1 MiB blocks, no overwrite."
    exit 0
fi
[[ $# == 2 && -p $1 && ! -L $1 ]] || die "Writer input must be an existing non-symlink FIFO."
[[ ! -e $2 && ! -L $2 ]] || die "Writer output must not already exist: $2"
need mbuffer
exec mbuffer -e -q -s 1048576 -m 134217728 -i "$1" -o "$2"
