#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PiTrac contributors
set -euo pipefail
export LC_ALL=C
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
umask 077

readonly REPOSITORY=https://github.com/ams-OSRAM/mira220_v4l2_driver.git
readonly COMMIT=aeedd655d12f6698000416aa6fd94b6f4dfa7b16
readonly STATE=/var/lib/pitrac-mira220-nir
readonly MANIFEST=$STATE/ownership.env
readonly LOCK=/run/pitrac-mira220-nir.lock
readonly FIRMWARE=/boot/firmware
readonly OVERLAY=$FIRMWARE/overlays/mira220-nir.dtbo
locked=0
changed=0
phase=preflight
kernel=
module=
build=
work=
module_hash=
overlay_hash=
source_version=
ramfs_mode=none
declare -a header_roots=()
declare -A ownership=()
declare -A config_seen=()
config_has_overlay=0
config_auto_initramfs=0
config_custom_initramfs=0
config_custom_layout=0

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
need() {
    local tool
    for tool in "$@"; do
        command -v "$tool" >/dev/null || die "Missing command: $tool. Install prerequisites separately; this installer never runs apt or upgrades the OS."
    done
}
exists() { [[ -e $1 || -L $1 ]]; }
hash_file() { sha256sum -- "$1" | awk '{print $1}'; }
valid_kernel() { [[ $1 =~ ^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$ ]]; }

help() {
    cat <<'EOF'
Usage: install-mira220.sh --check | --install | --remove | --help

  --check    Read-only platform, headers/API, ownership and module-path checks.
             No sudo, network, build, cache, depmod, or configuration writes.
  --install  Run as a NORMAL user on Pi 5, Linux aarch64, Trixie, a running
             Raspberry Pi 6.18.x rpi-2712 kernel and its matching headers.
             Fetch/build the pinned vendor revision in a dedicated user cache.
             Use sudo only for scoped installation/dependency/initramfs writes.
  --remove   Explicitly remove checksum-matching owned files. First restore
             boot camera configuration and manually reboot so mira220 is not
             loaded. Run on the recorded kernel; headers are not needed.
  --help     Show this help without platform checks.

Candidate only: NOT target-built or hardware/streaming-validated. Other kernels,
including 6.12, are refused. No OS upgrades/reflash, libcamera replacement,
packaged-file overwrite, module blacklist, config.txt edits, unload, or reboot.
The module name remains mira220; the overlay is uniquely mira220-nir.dtbo.
Only the stock Trixie auto_initramfs layout is supported, not custom initramfs
directives. Partial failures preserve the ownership record for --remove.

After a kernel update, do not assume this external module still works. Disable
the camera overlay and remove on the recorded kernel before switching kernels;
then --check/--install on the new, separately reviewed compatible kernel.
Source: https://github.com/ams-OSRAM/mira220_v4l2_driver/tree/aeedd655d12f6698000416aa6fd94b6f4dfa7b16
Known Pi 5 streaming report: https://github.com/ams-OSRAM/mira220_v4l2_driver/issues/2
EOF
}

manual_config() {
    cat <<'EOF'

MANUAL boot configuration step (nothing has been edited):
Inspect /boot/firmware/config.txt, its include files and existing conditional
sections/overlays first. Back up every file you will edit, using a new backup
name (for example: sudo cp -n /boot/firmware/config.txt /boot/firmware/config.txt.before-mira220-nir).
Verify the backup exists and is correct; do not overwrite an earlier backup.
Resolve conflicting camera_auto_detect/dtoverlay settings, including includes.
In a section scoped to this Pi 5 (normally [pi5]), use exactly:

[pi5]
camera_auto_detect=0
dtoverlay=mira220-nir,cam0
dtoverlay=imx296
[all]

Mira220 goes on CAM/DISP0; IMX296 defaults to CAM/DISP1. No cam1, mono,
always-on or trigger override is required. Preserve unrelated settings and
conditional sections. Check overlay_prefix/os_prefix/custom boot layouts before
editing: the installed overlay is /boot/firmware/overlays/mira220-nir.dtbo.
Only reboot manually after all installation and configuration checks pass.

Rollback: restore only your camera configuration changes from the checked
backup, including included files; remove/comment dtoverlay=mira220-nir in ALL
sections. Reboot manually with the Mira overlay disabled, then run --remove.
Never blacklist mira220: that name is shared with the packaged driver.
EOF
}

root_secure() {
    local path uid mode
    path=$(realpath -e -- "$1") || die "Cannot resolve trusted path: $1"
    while :; do
        read -r uid mode < <(stat -Lc '%u %a' -- "$path")
        [[ $uid == 0 && $mode =~ ^[0-7]{3,4}$ ]] ||
            die "Expected a root-owned path: $path"
        (( (8#$mode & 0022) == 0 )) ||
            die "Refusing group/world-writable system path: $path"
        [[ $path == / ]] && break
        path=${path%/*}
        [[ -n $path ]] || path=/
    done
}

platform() {
    [[ $(uname -s) == Linux && $(uname -m) == aarch64 ]] ||
        die "Requires Linux aarch64 on the target Pi 5; this host is not modified."
    [[ -r /proc/device-tree/model ]] &&
        grep -aq '^Raspberry Pi 5 Model' /proc/device-tree/model ||
        die "Requires a Raspberry Pi 5."
    [[ -r /etc/os-release ]] &&
        grep -Eq '^VERSION_CODENAME=("trixie"|trixie)$' /etc/os-release ||
        die "Requires the existing Trixie OS. Do not reflash or upgrade automatically."
    kernel=$(uname -r)
    valid_kernel "$kernel" || die "Unexpected kernel release: $kernel"
    module=/lib/modules/$kernel/updates/pitrac-mira220-nir/mira220.ko
    need awk grep stat realpath sha256sum modinfo modprobe find
    printf 'Platform: Pi 5, Trixie, Linux aarch64; running kernel %s\n' "$kernel"
}

header_has() {
    local relative=$1 pattern=$2 root
    for root in "${header_roots[@]}"; do
        if [[ -r $root/include/$relative ]] &&
            grep -Eq "$pattern" "$root/include/$relative"; then
            return 0
        fi
    done
    die "Required vendor API not found in matching headers: $relative ($pattern). No automatic kernel upgrade/backport is permitted."
}

candidate_headers() {
    local common
    [[ $kernel =~ ^6\.18\.[0-9]+[-+._[:alnum:]]*-rpi-2712$ ]] ||
        die "Only Raspberry Pi 6.18.x rpi-2712 is a support CANDIDATE; 6.12 and other releases require separate review."
    build=$(realpath -e -- "/lib/modules/$kernel/build") ||
        die "Install headers for EXACTLY $kernel separately; the running kernel build link is missing."
    root_secure "$build"
    [[ -r $build/Makefile && -s $build/Module.symvers && -r $build/.config ]] ||
        die "Incomplete prepared headers at $build (Makefile, .config, Module.symvers required)."
    [[ -r $build/include/generated/utsrelease.h ]] &&
        grep -Fqx "#define UTS_RELEASE \"$kernel\"" "$build/include/generated/utsrelease.h" ||
        die "Header UTS_RELEASE does not match running kernel $kernel."
    grep -Eq '^CONFIG_VIDEO_RP1_CFE=[my]$' "$build/.config" &&
        grep -Eq '^CONFIG_V4L2_CCI_I2C=[my]$' "$build/.config" &&
        grep -qx 'CONFIG_MODULES=y' "$build/.config" ||
        die "Headers do not describe a Raspberry Pi CFE/CCI module-capable kernel."
    if grep -qx 'CONFIG_VIDEO_MIRA220=y' "$build/.config"; then
        die "mira220 is built into this kernel; an external module cannot take precedence."
    fi
    if grep -qx 'CONFIG_MODULE_SIG_FORCE=y' "$build/.config" ||
        { [[ -r /proc/sys/kernel/module_sig_enforce ]] &&
          grep -qx 1 /proc/sys/kernel/module_sig_enforce; } ||
        { [[ -r /sys/kernel/security/lockdown ]] &&
          grep -Eq '\[(integrity|confidentiality)\]' /sys/kernel/security/lockdown; }; then
        die "Enforced module signing/lockdown needs a separately approved signing workflow."
    fi
    header_roots=("$build")
    if [[ -d /lib/modules/$kernel/source ]]; then
        common=$(realpath -e -- "/lib/modules/$kernel/source")
        root_secure "$common"
        header_roots+=("$common")
    fi
    # Debian split headers may name their common tree in the wrapper Makefile.
    while IFS= read -r common; do
        [[ $common == /usr/src/*/Makefile && $common != *..* ]] ||
            die "Unexpected common-header Makefile path: $common"
        common=${common%/Makefile}
        root_secure "$common"
        header_roots+=("$common")
    done < <(awk '$1 == "include" && NF == 2 && $2 ~ /^\/usr\/src\/.*\/Makefile$/ {print $2}' "$build/Makefile")
    header_has media/v4l2-common.h 'devm_v4l2_sensor_clk_get'
    header_has media/v4l2-common.h 'v4l2_link_freq_to_bitmap'
    header_has media/v4l2-subdev.h 'v4l2_subdev_s_stream_helper'
    header_has media/v4l2-subdev.h 'v4l2_subdev_state_get_format'
    header_has media/v4l2-cci.h 'devm_cci_regmap_init_i2c'
    header_has linux/property.h 'DEFINE_FREE\(fwnode_handle'
    printf 'Headers/API preflight passed (6.18 candidate only; compile and hardware tests still required).\n'
}

scan_config_file() {
    local file=$1 line included
    file=$(realpath -e -- "$file") || die "Cannot resolve boot configuration: $file"
    [[ $file == "$FIRMWARE/"* && -f $file && -r $file ]] ||
        die "Configuration must be readable and within $FIRMWARE: $file"
    [[ ${config_seen[$file]:-0} == 0 ]] || return 0
    (( ${#config_seen[@]} < 64 )) || die "Too many included configuration files; review manually."
    config_seen["$file"]=1
    while IFS= read -r line || [[ -n $line ]]; do
        line=${line%$'\r'}
        line=${line%%#*}
        if [[ $line =~ ^[[:space:]]*include[[:space:]]+(.+)$ ]]; then
            included=${BASH_REMATCH[1]}
            included=${included%"${included##*[![:space:]]}"}
            [[ $included =~ ^[A-Za-z0-9_./-]+$ && $included != /* && $included != *..* ]] ||
                die "Unsupported include directive in $file; review it manually."
            scan_config_file "$FIRMWARE/$included"
        elif [[ $line =~ ^[[:space:]]*dtoverlay[[:space:]]*=[[:space:]]*mira220-nir([,.[:space:]]|$) ]]; then
            printf 'Configured Mira overlay (conservatively treating every section as active): %s: %s\n' "$file" "$line"
            config_has_overlay=1
        elif [[ $line =~ ^[[:space:]]*auto_initramfs[[:space:]]*=[[:space:]]*1[[:space:]]*$ ]]; then
            config_auto_initramfs=1
        elif [[ $line =~ ^[[:space:]]*(initramfs[[:space:]]|ramfsfile[[:space:]]*=) ]]; then
            config_custom_initramfs=1
        elif [[ $line =~ ^[[:space:]]*(kernel|os_prefix|overlay_prefix)[[:space:]]*= ]]; then
            config_custom_layout=1
        fi
    done < "$file"
}

scan_config() {
    config_seen=()
    config_has_overlay=0
    config_auto_initramfs=0
    config_custom_initramfs=0
    config_custom_layout=0
    scan_config_file "$FIRMWARE/config.txt"
}

read_ownership() {
    local line key value
    [[ ! -L $STATE && -d $STATE && ! -L $MANIFEST && -f $MANIFEST ]] ||
        die "Incomplete or unsafe ownership state at $STATE; audit manually, never infer ownership from a filename."
    root_secure "$STATE"
    root_secure "$MANIFEST"
    [[ $(find "$STATE" -mindepth 1 -maxdepth 1 -printf '%f\n') == ownership.env ]] ||
        die "Unexpected files in ownership directory; preserve and audit them before proceeding."
    [[ $(stat -c %s -- "$MANIFEST") -le 2048 ]] || die "Oversized ownership manifest."
    ownership=()
    while IFS= read -r line || [[ -n $line ]]; do
        line=${line%$'\r'}
        [[ $line =~ ^([a-z_]+)=([A-Za-z0-9._+-]+)$ ]] || die "Invalid ownership data; nothing will be sourced or executed."
        key=${BASH_REMATCH[1]}
        value=${BASH_REMATCH[2]}
        case $key in
            schema|kernel|commit|module_sha|overlay_sha|srcversion|initramfs) ;;
            *) die "Unknown ownership key: $key" ;;
        esac
        [[ ! ${ownership[$key]+present} ]] || die "Duplicate ownership key: $key"
        ownership["$key"]=$value
    done < "$MANIFEST"
    [[ ${#ownership[@]} == 7 && ${ownership[schema]:-} == 1 &&
       ${ownership[commit]:-} == "$COMMIT" &&
       ${ownership[module_sha]:-} =~ ^[a-f0-9]{64}$ &&
       ${ownership[overlay_sha]:-} =~ ^[a-f0-9]{64}$ &&
       ${ownership[srcversion]:-} =~ ^[A-Fa-f0-9]{24}$ &&
       ${ownership[initramfs]:-} =~ ^(none|stock)$ ]] ||
        die "Invalid/incompatible ownership manifest; manual audit required."
    valid_kernel "${ownership[kernel]:-}" || die "Invalid recorded kernel."
}

verify_owned_file() {
    local path=$1 expected=$2 allow_missing=$3
    if ! exists "$path"; then
        [[ $allow_missing == yes ]] || die "Owned file is missing (partial installation): $path"
        printf 'Already absent: %s\n' "$path"
        return 0
    fi
    [[ ! -L $path && -f $path ]] || die "Refusing non-regular/symlink owned path: $path"
    root_secure "$path"
    [[ $(hash_file "$path") == "$expected" ]] ||
        die "CHECKSUM MISMATCH: $path. Preserved; audit/restore the exact recorded bytes before retrying --remove. Do not blindly delete or replace the manifest."
}

verify_precedence() {
    local selected dependencies load_path
    selected=$(modinfo -k "$kernel" -n mira220) ||
        die "depmod/modinfo cannot resolve mira220 for $kernel."
    [[ $(realpath -e -- "$selected") == "$(realpath -e -- "$module")" ]] ||
        die "mira220 resolves to $selected, NOT $module. Inspect depmod.d search/override rules and other external modules; do not blacklist mira220."
    dependencies=$(modprobe --show-depends --use-blacklist --set-version "$kernel" mira220) ||
        die "Cannot inspect mira220 modprobe dependency selection."
    load_path=$(awk '$1 == "insmod" && $2 ~ /\/mira220[.]ko([.](xz|gz|zst))?$/ {print $2}' <<< "$dependencies")
    [[ -n $load_path && $load_path != *$'\n'* ]] &&
        [[ $(realpath -e -- "$load_path") == "$(realpath -e -- "$module")" ]] ||
        die "modprobe would not select the owned mira220 module. Inspect existing blacklist/install/alias rules manually; none are changed here."
}

ramfs_preflight() {
    local image=/boot/initrd.img-$kernel boot_image=$FIRMWARE/initramfs_2712
    [[ $config_custom_initramfs == 0 ]] ||
        die "Custom initramfs/ramfsfile configuration is unsupported. Resolve its boot image and refresh procedure separately before any file changes."
    [[ $config_custom_layout == 0 ]] ||
        die "Explicit kernel/os_prefix/overlay_prefix boot settings need separate review; only the stock Trixie Pi 5 boot layout is supported."
    ramfs_mode=none
    if exists "$image" || exists "$boot_image" || [[ $config_auto_initramfs == 1 ]]; then
        [[ -f $image && ! -L $image && -f $boot_image && ! -L $boot_image ]] ||
            die "Stock initramfs pair missing: $image and $boot_image. Resolve the current boot/kernel layout separately."
        root_secure "$image"
        root_secure "$boot_image"
        [[ $(hash_file "$image") == "$(hash_file "$boot_image")" ]] ||
            die "Boot initramfs differs from the running kernel image. Resolve pending kernel updates/custom boot layout before proceeding."
        need update-initramfs lsinitramfs unmkinitramfs find
        ramfs_mode=stock
    fi
}

verify_ramfs() {
    local operation=$1 image=/boot/initrd.img-$kernel listing extracted path count=0
    [[ $ramfs_mode == stock ]] || return 0
    [[ ! -L $image && ! -L $FIRMWARE/initramfs_2712 ]] ||
        die "Initramfs hook changed the expected images into symlinks; audit before reboot."
    root_secure "$image"
    root_secure "$FIRMWARE/initramfs_2712"
    [[ $(hash_file "$image") == "$(hash_file "$FIRMWARE/initramfs_2712")" ]] ||
        die "Boot initramfs_2712 is stale after update-initramfs. DO NOT reboot; inspect the Raspberry Pi initramfs post-update hook, repair and retry rollback."
    listing=$(lsinitramfs "$image") || die "Cannot inspect regenerated initramfs."
    if ! grep -Eq '(^|/)mira220[.]ko([.](xz|gz|zst))?$' <<< "$listing"; then
        printf 'Initramfs contains no mira220 module; root-filesystem module selection applies.\n'
        return 0
    fi
    extracted=$(mktemp -d "$work/initramfs.XXXXXXXX")
    unmkinitramfs "$image" "$extracted"
    find "$extracted" -name 'mira220.ko*' -print0 > "$work/initramfs-modules.list"
    # Inspect the actual archive bytes, not just a modules.dep entry or filename.
    while IFS= read -r -d '' path; do
        ((count+=1))
        [[ -f $path && ! -L $path ]] || die "Unsafe mira220 entry in regenerated initramfs: $path"
        if [[ $operation == install ]]; then
            [[ $path == */lib/modules/"$kernel"/updates/pitrac-mira220-nir/mira220.ko ]] ||
                die "Stale/competing mira220 in regenerated initramfs: $path. Audit initramfs hooks/module lists; installation is NOT complete."
            [[ $(hash_file "$path") == "$module_hash" ]] ||
                die "Mira220 initramfs bytes do not match the installed vendor module."
        else
            [[ $path != */updates/pitrac-mira220-nir/* ]] ||
                die "Removed external module remains in initramfs: $path. Repair hooks and retry --remove; ownership state is retained."
            [[ $(modinfo -F srcversion "$path") != "${ownership[srcversion]}" ]] ||
                die "An initramfs hook retained the vendor module under another path: $path. Audit the hook and retry --remove."
        fi
    done < "$work/initramfs-modules.list"
    (( count > 0 )) || die "Archive listing and extracted mira220 files disagree."
    rm -r -- "$extracted"
}

new_cache() {
    local cache=${XDG_CACHE_HOME:-${HOME:?HOME is required}/.cache}/pitrac-mira220-nir
    # The pinned Makefile does not quote M=$(SRC); reject make/shell metacharacters.
    [[ $cache =~ ^/[A-Za-z0-9_./-]+$ && $cache != *..* ]] ||
        die "Cache path must be absolute, without spaces/metacharacters or '..' (vendor Makefile restriction)."
    [[ ! -L $cache ]] || die "Cache may not be a symlink: $cache"
    mkdir -p -m 0700 -- "$cache"
    [[ $(stat -c %u -- "$cache") == "$(id -u)" && $(stat -c %a -- "$cache") == 700 ]] ||
        die "Cache must be owned by the current user with mode 700: $cache"
    work=$(mktemp -d "$cache/$kernel.XXXXXXXX")
    printf 'Build/diagnostic cache retained at: %s\n' "$work"
}

build_module() {
    # Request the optional fingerprint from modpost without changing kernel config or ABI checks.
    make -C "$1" KDIR="$2" KERNELRELEASE="$3" CONFIG_MODULE_SRCVERSION_ALL=y -j2
}

build_vendor() {
    local src=$work/source vermagic
    phase=source-fetch-and-build
    git -c core.hooksPath=/dev/null init "$src"
    git -C "$src" -c core.hooksPath=/dev/null -c protocol.file.allow=never \
        -c protocol.ext.allow=never fetch --depth=1 "$REPOSITORY" "$COMMIT"
    [[ $(git -C "$src" rev-parse 'FETCH_HEAD^{commit}') == "$COMMIT" ]] ||
        die "Fetched revision is not the pinned commit."
    git -C "$src" -c core.hooksPath=/dev/null -c core.autocrlf=false checkout --detach "$COMMIT"
    [[ $(git -C "$src" rev-parse HEAD) == "$COMMIT" &&
       -z $(git -C "$src" status --porcelain) ]] || die "Source checkout is not clean and pinned."
    # Vendor Makefile: all -> make -C $(KDIR) M=$(SRC) modules. No overlay target.
    build_module "$src" "$build" "$kernel" 2>&1 | tee "$work/build.log"
    dtc -@ -I dts -O dtb -i "$src/dts/rpi" -o "$work/mira220-nir.dtbo" \
        "$src/dts/rpi/mira220-overlay.dts" 2>&1 | tee "$work/overlay.log"
    [[ -s $src/mira220.ko && -s $work/mira220-nir.dtbo ]] || die "Build artifacts are missing/empty."
    [[ $(modinfo -F name "$src/mira220.ko") == mira220 ]] || die "Unexpected built module name."
    vermagic=$(modinfo -F vermagic "$src/mira220.ko")
    [[ ${vermagic%% *} == "$kernel" ]] || die "Built module vermagic does not match $kernel."
    source_version=$(modinfo -F srcversion "$src/mira220.ko")
    [[ $source_version =~ ^[A-Fa-f0-9]{24}$ ]] ||
        die "Built module has missing/invalid srcversion despite CONFIG_MODULE_SRCVERSION_ALL=y. Inspect $work/build.log and modinfo on $src/mira220.ko; no driver files have been installed."
    module_hash=$(hash_file "$src/mira220.ko")
    overlay_hash=$(hash_file "$work/mira220-nir.dtbo")
}

write_new_root_file() {
    local source=$1 destination=$2
    exists "$destination" && die "Refusing to overwrite any existing path: $destination"
    # A small, fixed privileged write only. Arguments are data, never evaluated.
    # noclobber closes the check/create race even for a pre-existing symlink.
    sudo -- bash -c 'set -euo pipefail; umask 022; set -C; cat -- "$1" > "$2"' \
        pitrac-exclusive-write "$source" "$destination"
}

lock_installation() {
    root_secure /run
    sudo -- mkdir -m 0755 -- "$LOCK" ||
        die "Installation lock exists or cannot be created: $LOCK. If stale, verify no installer is running before an administrator removes this EMPTY directory."
    locked=1
}

unlock_installation() {
    sudo -- rmdir -- "$LOCK"
    locked=0
}

on_exit() {
    local rc=$?
    trap - EXIT
    if [[ $locked == 1 ]]; then
        if ! sudo -- rmdir -- "$LOCK"; then
            printf 'ERROR: Could not release %s; inspect the empty lock directory manually.\n' "$LOCK" >&2
            rc=1
        fi
    fi
    if (( rc != 0 )); then
        printf 'FAILED during %s; no success or hardware validation is implied.\n' "$phase" >&2
        if [[ $changed == 1 ]]; then
            printf 'Partial system writes may exist. DO NOT enable the overlay or reboot into it.\nOwnership state: %s\nRestore/disable the Mira boot configuration first, then retry --remove on kernel %s.\nIf a file checksum differs, it is preserved for manual audit; never blindly delete packaged or unowned files.\n' "$STATE" "$kernel" >&2
        fi
    fi
    exit "$rc"
}

check_installation() {
    candidate_headers
    scan_config
    if exists "$STATE"; then
        read_ownership
        [[ ${ownership[kernel]} == "$kernel" ]] ||
            die "Ownership belongs to ${ownership[kernel]}, not the running kernel. A kernel update does not rebuild this driver."
        verify_owned_file "$module" "${ownership[module_sha]}" no
        verify_owned_file "$OVERLAY" "${ownership[overlay_sha]}" no
        verify_precedence
        printf 'Owned module/overlay checksums and on-disk modinfo selection match.\n'
        if [[ -r /sys/module/mira220/srcversion ]]; then
            [[ $(< /sys/module/mira220/srcversion) == "${ownership[srcversion]}" ]] ||
                die "Loaded mira220 srcversion differs; on-disk modinfo alone does NOT identify the loaded module."
            printf 'Loaded module srcversion matches the recorded build (not a streaming test).\n'
        else
            printf 'Loaded module identity not established: mira220 is absent or exposes no srcversion.\n'
        fi
        printf 'Initramfs contents are not extracted in read-only --check; installation verifies archive bytes when regenerating it.\n'
    else
        exists "$OVERLAY" && die "Unowned overlay already exists: $OVERLAY"
        exists "$module" && die "Unowned module already exists: $module"
        printf 'No installer ownership record. No files changed.\n'
    fi
    printf 'CHECK ONLY: candidate preflight, NOT target-build/boot/streaming/hardware validation.\n'
}

prepare_mutation() {
    [[ $EUID != 0 ]] || die "Run as a normal user, not sudo/root. Privileged operations are individually scoped."
    need sudo mkdir rmdir rm cat mktemp tee mountpoint depmod
    mountpoint -q "$FIRMWARE" || die "$FIRMWARE is not mounted; refusing to write into a hidden/unmounted boot directory."
    root_secure "$FIRMWARE/overlays"
    root_secure /var/lib
    root_secure "/lib/modules/$kernel"
    [[ ! -L /lib/modules/$kernel/updates && ! -L ${module%/*} ]] ||
        die "Refusing symlink in installation-specific module directories."
    scan_config
    [[ $config_has_overlay == 0 ]] ||
        die "Mira overlay is still configured. Restore the scoped camera configuration (including includes) and disable mira220-nir before --install/--remove. Reboot manually with it disabled."
    [[ ! -d /sys/module/mira220 ]] ||
        die "mira220 is loaded. Restore/disable its camera configuration and reboot manually before changing files; this installer never unloads a live sensor module."
    ramfs_preflight
    lock_installation
}

install_driver() {
    candidate_headers
    need git make gcc dtc
    prepare_mutation
    if exists "$STATE"; then
        die "Ownership state already exists. Use --check; for a partial/replacement install, restore boot config and run --remove first."
    fi
    exists "$OVERLAY" && die "Unowned overlay already exists: $OVERLAY"
    exists "$module" && die "Unowned module already exists: $module"
    if exists "/lib/modules/$kernel/updates"; then root_secure "/lib/modules/$kernel/updates"; fi
    if exists "${module%/*}"; then root_secure "${module%/*}"; fi
    new_cache
    build_vendor
    phase=recording-ownership
    printf 'schema=1\nkernel=%s\ncommit=%s\nmodule_sha=%s\noverlay_sha=%s\nsrcversion=%s\ninitramfs=%s\n' \
        "$kernel" "$COMMIT" "$module_hash" "$overlay_hash" "$source_version" "$ramfs_mode" > "$work/ownership.env"
    changed=1
    sudo -- mkdir -m 0755 -- "$STATE"
    write_new_root_file "$work/ownership.env" "$MANIFEST"
    read_ownership
    phase=installing-files
    sudo -- mkdir -p -m 0755 -- "${module%/*}"
    root_secure "${module%/*}"
    write_new_root_file "$work/source/mira220.ko" "$module"
    write_new_root_file "$work/mira220-nir.dtbo" "$OVERLAY"
    verify_owned_file "$module" "$module_hash" no
    verify_owned_file "$OVERLAY" "$overlay_hash" no
    phase=regenerating-module-dependencies
    sudo -- depmod -a "$kernel"
    verify_precedence
    phase=refreshing-initramfs
    if [[ $ramfs_mode == stock ]]; then
        sudo -- update-initramfs -u -k "$kernel"
        verify_ramfs install
    fi
    unlock_installation
    changed=0
    printf 'Installation files and dependency/initramfs checks completed for %s.\nNo boot configuration was changed. No hardware/streaming validation has occurred.\n' "$kernel"
    manual_config
    printf '\nAfter your manual reboot, run --check. Expected loaded srcversion: %s\nThen prove mono identification and each/concurrent raw pipeline; probe success alone is insufficient.\n' "$source_version"
}

remove_driver() {
    local restored_version
    exists "$STATE" || die "No ownership state exists. Nothing will be removed or adopted."
    read_ownership
    [[ ${ownership[kernel]} == "$kernel" ]] ||
        die "Removal requires recorded kernel ${ownership[kernel]} to be running, to avoid overwriting another kernel's boot initramfs. Disable the overlay first; boot the recorded kernel or seek an audited manual recovery."
    prepare_mutation
    read_ownership
    [[ ${ownership[initramfs]} == "$ramfs_mode" ]] ||
        die "Initramfs layout changed since installation; audit the boot configuration before removal."
    verify_owned_file "$module" "${ownership[module_sha]}" yes
    verify_owned_file "$OVERLAY" "${ownership[overlay_sha]}" yes
    new_cache
    phase=removing-owned-files
    changed=1
    if exists "$module"; then
        verify_owned_file "$module" "${ownership[module_sha]}" no
        sudo -- rm -- "$module"
    fi
    if exists "$OVERLAY"; then
        verify_owned_file "$OVERLAY" "${ownership[overlay_sha]}" no
        sudo -- rm -- "$OVERLAY"
    fi
    phase=restoring-module-dependencies
    sudo -- depmod -a "$kernel"
    if [[ -s /lib/modules/$kernel/modules.dep ]] &&
        grep -Fq 'updates/pitrac-mira220-nir/mira220.ko' "/lib/modules/$kernel/modules.dep"; then
        die "Removed module remains in modules.dep; investigate depmod before retrying --remove."
    fi
    if restored_version=$(modinfo -k "$kernel" -F srcversion mira220 2>&1); then
        [[ $restored_version != "${ownership[srcversion]}" ]] ||
            die "Another on-disk module still resolves to this vendor build. It is unowned and preserved; audit competing modules before completing rollback."
    elif [[ $restored_version == 'modinfo: ERROR: Module mira220 not found.' ]]; then
        printf 'No replacement mira220 module exists; no packaged driver is invented or installed.\n'
    else
        die "Cannot verify restored mira220 selection: $restored_version"
    fi
    phase=restoring-initramfs
    if [[ $ramfs_mode == stock ]]; then
        sudo -- update-initramfs -u -k "$kernel"
        verify_ramfs remove
    fi
    phase=removing-ownership-record
    sudo -- rm -- "$MANIFEST"
    sudo -- rmdir -- "$STATE"
    unlock_installation
    changed=0
    printf 'Removed only checksum-matching owned module/overlay; dependencies/initramfs refreshed.\nPackaged driver/overlay, user cache, camera configuration and recordings were not deleted.\nEmpty module directories are deliberately retained. No reboot performed.\n'
}

main() {
    [[ $# == 1 ]] || { help >&2; die "Supply exactly one explicit operation."; }
    case $1 in
        --help) help; return 0 ;;
        --check|--install|--remove) ;;
        *) help >&2; die "Unknown operation: $1" ;;
    esac
    trap on_exit EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    platform
    case $1 in
        --check) check_installation ;;
        --install) install_driver ;;
        --remove) remove_driver ;;
    esac
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
    main "$@"
fi
