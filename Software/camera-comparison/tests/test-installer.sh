#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
set -euo pipefail
SCRIPTS=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../scripts" && pwd)
source "$SCRIPTS/common.sh"
need make cc awk cmp sha256sum
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
fails() {
    if ("$@") > "$fixture_root/negative.stdout" 2> "$fixture_root/negative.stderr"; then
        die "Expected failure: $*"
    fi
    ((checks+=1))
}
check_srcversion() (
    source "$SCRIPTS/install-mira220.sh"
    valid_srcversion "$1"
)
check_ownership() (
    source "$SCRIPTS/install-mira220.sh"
    ownership=(
        [schema]=1 [kernel]="$fixture_kernel" [commit]="$COMMIT"
        [module_sha]="$(printf '%064d' 0)" [overlay_sha]="$(printf '%064d' 0)"
        [srcversion]="$1" [initramfs]=none
    )
    validate_ownership
)

bash -n "$SCRIPTS/install-mira220.sh"
((checks+=1))
bash "$SCRIPTS/install-mira220.sh" --help > "$fixture_root/help.txt"
((checks+=1))
(
    source "$SCRIPTS/install-mira220.sh"
    manual_config
) > "$fixture_root/manual-config.txt"
primary_config=$(awk '
    /^\[pi5\]$/ {active=1; next}
    active && /^\[all\]$/ {exit}
    active {print}
' "$fixture_root/manual-config.txt")
equal "$primary_config" $'camera_auto_detect=0\ndtoverlay=imx296,cam0\ndtoverlay=mira220-nir'

# Mirror the 6.18 modpost buffer/size contract, not the MD4 calculation itself.
cat > "$fixture_root/srcversion-format.c" <<'EOF'
#include <stdio.h>

int main(void)
{
    char srcversion[25];
    snprintf(srcversion, sizeof(srcversion) - 1, "%08X%08X%08X%08X",
             0x712AA4F7u, 0x4D072B09u, 0xAA7CE8F0u, 0x12345678u);
    puts(srcversion);
    return 0;
}
EOF
if ! cc "$fixture_root/srcversion-format.c" -o "$fixture_root/srcversion-format" \
    > "$fixture_root/format-build.log" 2>&1; then
    cat "$fixture_root/format-build.log" >&2
    die "Could not compile the intentional-truncation formatting fixture."
fi
generated_srcversion=$("$fixture_root/srcversion-format")
observed_srcversion=712AA4F74D072B09AA7CE8F
equal "$generated_srcversion" "$observed_srcversion"
equal "${#generated_srcversion}" 23
for fingerprint in "$observed_srcversion" "${observed_srcversion,,}" 0123456789ABCDEF01234567; do
    check_srcversion "$fingerprint" || die "A supported fingerprint was rejected."
    ((checks+=1))
    check_ownership "$fingerprint" || die "A supported ownership fingerprint was rejected."
    ((checks+=1))
done
for fingerprint in "" "${observed_srcversion:0:22}" "${observed_srcversion}00" \
    "Z${observed_srcversion:1}" " $observed_srcversion" "$observed_srcversion"$'\n'; do
    fails check_srcversion "$fingerprint"
    fails check_ownership "$fingerprint"
done

# mkdir -m controls the leaf, not intermediate directories created by -p.
(
    umask 077
    mkdir -p -m 0755 -- "$fixture_root/old-mask/updates/pitrac-mira220-nir"
)
equal "$(stat -c %a "$fixture_root/old-mask/updates")" 700
equal "$(stat -c %a "$fixture_root/old-mask/updates/pitrac-mira220-nir")" 755
mkdir -m 0750 -- "$fixture_root/existing-parent"
(
    umask 077
    source "$SCRIPTS/install-mira220.sh"
    mkdir -p -m 0755 -- "$fixture_root/new-mask/updates/pitrac-mira220-nir"
    printf 'fixture metadata\n' > "$fixture_root/new-mask/updates/modules.dep.fixture"
    mkdir -p -m 0755 -- "$fixture_root/existing-parent/child"
    XDG_CACHE_HOME=$fixture_root/cache-base
    kernel=$fixture_kernel
    new_cache > "$fixture_root/cache.log"
    printf '%s\n' "$work" > "$fixture_root/cache-path"
)
equal "$(stat -c %a "$fixture_root/new-mask/updates")" 755
equal "$(stat -c %a "$fixture_root/new-mask/updates/pitrac-mira220-nir")" 755
equal "$(stat -c %a "$fixture_root/new-mask/updates/modules.dep.fixture")" 644
equal "$(stat -c %a "$fixture_root/cache-base/pitrac-mira220-nir")" 700
equal "$(stat -c %a "$(< "$fixture_root/cache-path")")" 700
equal "$(stat -c %a "$fixture_root/existing-parent")" 750

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

# MAKEFLAGS enables metadata but cannot repair an incorrect fingerprint validator.
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

printf 'All %d installer checks passed (permissions, C formatting, actual Pi fingerprint and GNU Make fixtures; no installation).\n' "$checks"
