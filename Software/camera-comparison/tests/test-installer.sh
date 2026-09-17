#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
set -euo pipefail
SCRIPTS=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../scripts" && pwd)
source "$SCRIPTS/common.sh"
need make awk cmp sha256sum
fixture_root=$(mktemp -d "${TMPDIR:-/tmp}/pitrac-installer-tests.XXXXXXXX")
trap 'rm -r -- "$fixture_root"' EXIT
fixture_source=$fixture_root/vendor
fixture_headers=$fixture_root/headers
fixture_kernel=6.18.50+rpt-rpi-2712
mkdir -- "$fixture_source" "$fixture_headers"
checks=0
equal() {
    [[ $1 == "$2" ]] || die "Expected '$2', got '$1'."
    ((checks+=1))
}

bash -n "$SCRIPTS/install-mira220.sh"
((checks+=1))
bash "$SCRIPTS/install-mira220.sh" --help > "$fixture_root/help.txt"
((checks+=1))

cat > "$fixture_source/Makefile" <<'EOF'
.PHONY: all
all:
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)" modules
EOF
cat > "$fixture_headers/auto.conf" <<'EOF'
CONFIG_MODULE_SRCVERSION_ALL :=
CONFIG_MODVERSIONS := y
EOF
cat > "$fixture_headers/Makefile" <<'EOF'
include auto.conf
.PHONY: modules
modules:
	@printf 'modpost_flag\t%s\norigin\t%s\nkernel\t%s\nabi_versions\t%s\n' \
	  '$(if $(CONFIG_MODULE_SRCVERSION_ALL),-a,disabled)' \
	  '$(origin CONFIG_MODULE_SRCVERSION_ALL)' \
	  '$(KERNELRELEASE)' '$(CONFIG_MODVERSIONS)' > "$(M)/build-settings.tsv"
EOF
cp -- "$fixture_headers/auto.conf" "$fixture_root/auto.conf.before"

MAKEFLAGS= make -C "$fixture_source" KDIR="$fixture_headers" KERNELRELEASE="$fixture_kernel" \
    > "$fixture_root/default-build.log"
equal "$(value "$fixture_source/build-settings.tsv" modpost_flag)" disabled

(
    source "$SCRIPTS/install-mira220.sh"
    build_module "$fixture_source" "$fixture_headers" "$fixture_kernel"
) > "$fixture_root/installer-build.log"
equal "$(value "$fixture_source/build-settings.tsv" modpost_flag)" -a
equal "$(value "$fixture_source/build-settings.tsv" origin)" 'command line'
equal "$(value "$fixture_source/build-settings.tsv" kernel)" "$fixture_kernel"
equal "$(value "$fixture_source/build-settings.tsv" abi_versions)" y
cmp -- "$fixture_headers/auto.conf" "$fixture_root/auto.conf.before"
((checks+=1))

# The command-local retry also works with an older Pi-side installer.
MAKEFLAGS='CONFIG_MODULE_SRCVERSION_ALL=y' make -C "$fixture_source" \
    KDIR="$fixture_headers" KERNELRELEASE="$fixture_kernel" -j2 > "$fixture_root/retry-build.log"
equal "$(value "$fixture_source/build-settings.tsv" modpost_flag)" -a
equal "$(value "$fixture_source/build-settings.tsv" origin)" 'command line'
cmp -- "$fixture_headers/auto.conf" "$fixture_root/auto.conf.before"
((checks+=1))

if (
    source "$SCRIPTS/install-mira220.sh"
    build_module "$fixture_source" "$fixture_root/missing-headers" "$fixture_kernel"
) > "$fixture_root/failed-build.log" 2>&1; then
    die "A failed recursive build was incorrectly accepted."
fi
((checks+=1))

printf 'All %d installer fixture checks passed (GNU Make fixtures; no module installation or target build).\n' "$checks"
