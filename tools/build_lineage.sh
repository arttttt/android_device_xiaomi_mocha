#!/bin/bash
# OrangeFox fox_12.1 recovery (mocha) — init / sync / post-sync / clean / build.
# The LineageOS script of 19.1, cut down to the one tree this branch builds:
# a twrp-12.1 minimal manifest with OrangeFox on top, and the kernel from
# SmokeR24.1-stable built the way 19.1 builds it.

export LC_ALL=C

# Where a failing run leaves its output. Next to the script as invoked, not
# next to the file the symlink resolves to -- the usual entry point is
# ~/build_lineage.sh pointing into a checked-out device tree, and dropping a
# log inside that tree would dirty the git working copy. Independent of the
# working directory, which changes constantly as actions cd into $BUILD_DIR.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ERROR_FILE="$SCRIPT_DIR/error.txt"

# Capture the whole run so a failure leaves something behind, and on success
# remove the file, so its presence always means "the last run failed" rather
# than "something failed at some point".
#
# Done by re-executing under script(1) rather than piping into tee. repo, git
# and make print progress only when stdout is a terminal; a pipe silences them
# completely, so `repo sync` would download for twenty minutes showing nothing
# at all. script(1) gives the child a pty, so everything behaves as if run
# directly, while the session is written to a file in parallel.
#
# The guard variable stops the re-exec from recursing. If script(1) is absent
# or is not the util-linux one (its -e/-c options differ elsewhere), the run
# simply proceeds unwrapped -- losing the error file is much better than
# losing the ability to run at all.
if [ -z "${BUILD_LINEAGE_LOGGED:-}" ] \
   && script --version 2>/dev/null | grep -q util-linux; then
    export BUILD_LINEAGE_LOGGED=1
    _bl_tmp=$(mktemp)
    script -q -e -c "$(printf '%q ' "$0" "$@")" "$_bl_tmp"
    _bl_rc=$?
    if [ "$_bl_rc" -ne 0 ]; then
        {
            echo "=== $(date '+%Y-%m-%d %H:%M:%S')  FAILED: $0 $*  (exit $_bl_rc)"
            echo
            # col -b resolves the carriage returns that progress meters leave
            # behind; without it the file is one long unreadable line.
            if command -v col >/dev/null 2>&1; then col -bx < "$_bl_tmp"; else cat "$_bl_tmp"; fi
        } > "$ERROR_FILE"
        echo "==> FAILED — output written to $ERROR_FILE" >&2
    else
        rm -f "$ERROR_FILE"
    fi
    rm -f "$_bl_tmp"
    exit "$_bl_rc"
fi

#==============================================================================
# Shared helpers
#==============================================================================

# Patches for upstream projects we do not fork. They live in the device tree
# under patches/, in a directory named after the project they apply to, so
# patches/frameworks/base/*.patch goes to $BUILD_DIR/frameworks/base. A sync
# resets those projects, so this re-applies after every one.
#
# Idempotent, per project rather than per patch: a patch cannot tell by
# itself whether it is in once a later patch of the same project has
# changed the lines around it -- the P2P tethering series in
# frameworks/opt/net/wifi does exactly that, and its first patch stopped
# reverse-applying. A project with no local changes gets its series applied
# in order; one with changes is compared with the series applied to HEAD in
# a scratch index, and is either exactly that (already in) or something
# else, which is an error.
#
# Projects named as arguments limit it to their series; with none it applies
# every series under patches/.
patch_trees() {
    local only=" $* "
    local tree="$BUILD_DIR/device/xiaomi/mocha"
    local root="$tree/patches"
    # A branch with nothing to patch is a normal state, not an error: each
    # device tree branch carries its own patches/ and some carry none. Only
    # a missing device tree is worth failing over, and that is a different
    # thing entirely -- it means the sync did not happen.
    if [ ! -d "$tree" ]; then
        echo "  patches: $tree missing - sync the device tree first" >&2
        return 1
    fi
    if [ ! -d "$root" ]; then
        echo "  patches: none on this branch"
        return 0
    fi
    local proj dir p idx patches applied n tree
    for proj in $(find "$root" -name '*.patch' -exec dirname {} \; | sort -u); do
        proj=${proj#$root/}
        if [ "$only" != "  " ] && [ "${only#* $proj }" = "$only" ]; then
            continue
        fi
        dir="$BUILD_DIR/$proj"
        if [ ! -d "$dir/.git" ]; then
            echo "  $proj: not a git project, skipping its patches" >&2
            continue
        fi
        if git -C "$dir" diff --quiet HEAD; then
            for p in $(find "$root/$proj" -maxdepth 1 -name '*.patch' | sort); do
                if git -C "$dir" apply -p1 "$p" 2>/dev/null; then
                    echo "  $proj: applied $(basename "$p")"
                else
                    echo "  $proj: FAILED to apply $(basename "$p")" >&2
                    return 1
                fi
            done
            continue
        fi
        # The tree is patched already: find how far into the series it is,
        # by applying the series one patch at a time to a scratch index and
        # comparing, and apply the rest. A series that grew since the last
        # post-sync is the usual case; anything else is not ours to touch.
        applied=-1 n=0
        patches=$(find "$root/$proj" -maxdepth 1 -name '*.patch' | sort)
        idx=$(mktemp)
        if GIT_INDEX_FILE="$idx" git -C "$dir" read-tree HEAD; then
            for p in $patches; do
                GIT_INDEX_FILE="$idx" git -C "$dir" apply --cached -p1 "$p" 2>/dev/null \
                    || break
                n=$((n + 1))
                tree=$(GIT_INDEX_FILE="$idx" git -C "$dir" write-tree)
                git -C "$dir" diff --quiet "$tree" && applied=$n
            done
        fi
        rm -f "$idx"
        if [ "$applied" -lt 1 ]; then
            echo "  $proj: FAILED - local changes that are not its patches" >&2
            return 1
        fi
        n=0
        for p in $patches; do
            n=$((n + 1))
            [ "$n" -le "$applied" ] && continue
            if git -C "$dir" apply -p1 "$p" 2>/dev/null; then
                echo "  $proj: applied $(basename "$p")"
            else
                echo "  $proj: FAILED to apply $(basename "$p")" >&2
                return 1
            fi
        done
        [ "$applied" -eq "$n" ] && echo "  $proj: already applied"
    done
    return 0
}

#==============================================================================
# fox_12.1
#==============================================================================

config_fox121() {
    VER="fox_12.1"
    BUILD_DIR="/home/artem/DATA/projects/android/fox_12.1"
    TWRP_MANIFEST_URL="https://github.com/minimal-manifest-twrp/platform_manifest_twrp_aosp.git"
    TWRP_MANIFEST_BRANCH="twrp-12.1"
    DEVICE_TREE_BRANCH="fox_12.1"

    # The tree's mke2fs reads /etc/mke2fs.conf when nothing else is named,
    # and on a rolling distribution that lists features it has never heard
    # of ("Invalid filesystem option set: ...,orphan_file"). The tree ships a
    # configuration of its own.
    export MKE2FS_CONFIG="$BUILD_DIR/system/extras/ext4_utils/mke2fs.conf"

    # The kernel is the same SmokeR24.1-stable 19.1 builds, with the same
    # toolchain: the in-tree androideabi-4.9 pin miscompiles it on this host,
    # linaro-4.9.4 does not. Env overrides still win.
    : "${KERNEL_TOOLCHAIN:=/home/artem/Projects/toolchain/linaro-4.9.4/bin}"
    : "${TARGET_KERNEL_CROSS_COMPILE_PREFIX:=arm-linux-gnueabihf-}"
    export KERNEL_TOOLCHAIN TARGET_KERNEL_CROSS_COMPILE_PREFIX

    # Mocha has no vendor partition, so root/vendor is a link to
    # /system/vendor, while the recovery modules install real files under
    # recovery/root/vendor/etc. Copying root over recovery, rsync cannot turn
    # that non-empty directory into the link and the ramdisk step fails
    # ("cannot delete non-empty directory: root/vendor"). With this set,
    # bootable/recovery of OrangeFox makes root/vendor a directory instead.
    export OF_MANUAL_ROOT_VENDOR_ERROR_FIX=1

    # With gzip the ramdisk alone is 16.7 MB and the image does not fit the
    # 20 MiB partition; LZMA brings the ramdisk to about 11.4 MB. OrangeFox
    # turns this into BOARD_RAMDISK_USE_LZMA, the legacy .lzma format, which
    # the kernel reads since SmokeR24.1-stable enables RD_LZMA.
    export OF_USE_LZMA_COMPRESSION=1
}

#==============================================================================
# Actions
#==============================================================================

DEVICE="mocha"
DEVICE_TREE_PATH="device/xiaomi/$DEVICE"
DEVICE_TREE_URL="https://github.com/arttttt/android_device_xiaomi_mocha"
LUNCH="omni_${DEVICE}-eng"

# The local manifest is the list of every repository the build needs beyond
# the minimal manifest of TWRP. Keeping it inside .repo means it exists on
# exactly one machine and nowhere in history, which is how
# hardware/nvidia/libstagefrighthw went missing on 15.1: device.mk asked for
# the package, no repo provided it, and the build said nothing.
#
# So the manifest lives in the device tree under manifests/mocha-<ver>.xml and
# this installs it. Adding a repository becomes a one-line commit there.
do_manifest() {
    echo "==> local manifest ($VER)"
    local src="$BUILD_DIR/$DEVICE_TREE_PATH/manifests/mocha-$VER.xml"

    # Bootstrap: the manifest lives in the device tree, and the device tree is
    # itself one of the projects that manifest declares. On a fresh tree take
    # the file from a throwaway shallow clone rather than dropping a git
    # checkout where repo expects to manage one.
    if [ ! -f "$src" ]; then
        echo "  device tree not checked out yet, fetching manifest directly"
        local tmp
        tmp=$(mktemp -d) || return 1
        if git clone -q --depth 1 -b "$DEVICE_TREE_BRANCH" \
                "$DEVICE_TREE_URL" "$tmp/dt"; then
            src="$tmp/dt/manifests/mocha-$VER.xml"
        fi
    fi

    if [ ! -f "$src" ]; then
        echo "==> ERROR: no manifest for $VER (looked for manifests/mocha-$VER.xml)" >&2
        return 1
    fi

    # Check it parses before handing it to repo. repo reports a malformed
    # manifest only as "not well-formed (invalid token)" with a line and
    # column, never the reason -- and the reason is usually a double hyphen
    # inside a comment, which XML forbids and which reads as ordinary prose.
    if ! python3 -c "import sys, xml.dom.minidom as m; m.parse(sys.argv[1])" "$src" 2>/dev/null; then
        echo "==> ERROR: $src is not well-formed XML" >&2
        python3 -c "import sys, xml.dom.minidom as m; m.parse(sys.argv[1])" "$src" 2>&1 \
            | tail -2 | sed 's/^/    /' >&2
        return 1
    fi

    mkdir -p "$BUILD_DIR/.repo/local_manifests"
    local dst="$BUILD_DIR/.repo/local_manifests/mocha.xml"
    if [ -f "$dst" ] && ! cmp -s "$src" "$dst"; then
        cp "$dst" "$dst.$(date +%Y%m%d-%H%M%S).bak"
        echo "  previous manifest differed, backed up"
    fi
    cp "$src" "$dst"
    echo "  installed mocha-$VER.xml ($(grep -c '<project ' "$dst") projects)"
}

# Only prepares the directory: the minimal manifest of TWRP and ours on top of
# it. Nothing is downloaded but the manifest itself. Depth 1, as the official
# orangefox_sync.sh initialises it.
do_init() {
    echo "==> init ($VER)"
    local dir="$BUILD_DIR" avail
    [ -d "$dir" ] || dir=$(dirname "$BUILD_DIR")
    avail=$(df -BG --output=avail "$dir" | tail -1 | tr -dc 0-9)
    if [ "${avail:-0}" -lt 45 ]; then
        echo "==> ERROR: ${avail:-0} GB free under $dir, the tree needs at least 45" >&2
        return 1
    fi
    mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR" || return 1
    if [ -d .repo ]; then
        echo "  already initialised, leaving repo init alone"
    elif ! repo init --depth=1 -u "$TWRP_MANIFEST_URL" -b "$TWRP_MANIFEST_BRANCH"; then
        echo "==> ERROR: repo init failed" >&2
        return 1
    fi
    do_manifest || return 1
    echo "==> init OK - next: sync"
}

do_sync() {
    echo "==> repo sync ($VER)"
    if [ ! -d "$BUILD_DIR/.repo" ]; then
        echo "==> ERROR: $BUILD_DIR is not initialised - run init first" >&2
        return 1
    fi
    cd "$BUILD_DIR" || return 1
    do_manifest || return 1
    # repo sync's exit status is the only thing that distinguishes a finished
    # sync from one that gave up halfway. It reports per-project failures on
    # stderr and keeps going, so without this check the function announced
    # "sync OK" over a tree that had not moved -- and the build that followed
    # used whatever was there. A local edit to a tracked file is enough to
    # trigger it: --force-sync overrides a project whose path or remote
    # changed, not a dirty working file. The other flags are the ones the
    # official orangefox_sync.sh syncs with.
    if ! repo sync -c -j$(nproc) --force-sync --no-clone-bundle --no-tags; then
        echo "  repo sync did not complete - the tree is not at the revisions" >&2
        echo "  the manifest names, so do not build from it" >&2
        return 1
    fi
    echo "==> sync OK"
}

# What orangefox_sync.sh does to the tree after its sync -- patch build/,
# system/ and vendor/twrp -- with the patches it uses, kept in orangefox/ of
# the device tree. Done here so that it can run again after every sync, which
# resets those projects: a patch already in is recognised and left alone.
# orangefox_sync.sh itself is not run at all. It inits and clones on its own,
# and over a finished tree it applies its patches a second time and fails.
apply_fox_patches() {
    local src="$BUILD_DIR/$DEVICE_TREE_PATH/orangefox"
    local pair dir diff
    for pair in build:build.diff system:system.diff vendor/twrp:vendor-twrp.diff; do
        dir="$BUILD_DIR/${pair%%:*}"
        diff="$src/${pair#*:}"
        if [ ! -d "$dir" ] || [ ! -f "$diff" ]; then
            echo "  orangefox: $dir or $diff missing" >&2
            return 1
        fi
        if patch -d "$dir" -p1 -R --dry-run -s -f < "$diff" >/dev/null 2>&1; then
            echo "  orangefox: ${pair#*:} already applied"
        elif patch -d "$dir" -p1 --dry-run -s -f < "$diff" >/dev/null 2>&1; then
            patch -d "$dir" -p1 -s -f < "$diff" || return 1
            echo "  orangefox: applied ${pair#*:}"
        else
            echo "  orangefox: ${pair#*:} does not apply to $dir" >&2
            return 1
        fi
    done
}

do_post_sync() {
    echo "==> post-sync patches ($VER)"
    apply_fox_patches || return 1
    patch_trees || return 1
    echo "==> post-sync OK"
}

do_clean() {
    echo "==> make clobber ($VER)"
    cd "$BUILD_DIR" || return 1
    source build/envsetup.sh
    lunch "$LUNCH" || return 1
    make clobber
    echo "==> clean OK"
}

# installclean is the narrow tool for a module that left the product: the
# installed images and the staging directories go, out/soong and the object
# files stay, so the rebuild is minutes rather than the hours clobber costs.
# Without it, a file a module already installed survives in out/ and ships.
do_installclean() {
    echo "==> make installclean ($VER)"
    cd "$BUILD_DIR" || return 1
    source build/envsetup.sh
    lunch "$LUNCH" || return 1
    make installclean
    echo "==> installclean OK"
}

check_kernel_toolchain() {
    echo "    KERNEL_TOOLCHAIN = $KERNEL_TOOLCHAIN"
    echo "    KERNEL_PREFIX    = $TARGET_KERNEL_CROSS_COMPILE_PREFIX"
    if [ ! -x "$KERNEL_TOOLCHAIN/${TARGET_KERNEL_CROSS_COMPILE_PREFIX}gcc" ]; then
        echo "==> ERROR: kernel gcc not found at $KERNEL_TOOLCHAIN/${TARGET_KERNEL_CROSS_COMPILE_PREFIX}gcc" >&2
        return 1
    fi
}

# Success is an image this run wrote, that fits the partition and that carries
# the device tree: without it the bootloader has nothing to hand the kernel.
do_build() {
    echo "==> recovery ($VER)"
    check_kernel_toolchain || return 1
    cd "$BUILD_DIR" || return 1
    source build/envsetup.sh
    lunch "$LUNCH" || return 1

    local OUT="$BUILD_DIR/out/target/product/$DEVICE"
    local IMG="$OUT/recovery.img"
    local LIMIT=20971520
    local stamp size dt fox
    stamp=$(mktemp)
    trap 'rm -f "$stamp"' RETURN

    # Not piped anywhere: the whole run is already captured by the pty wrapper
    # at the top of this script, and a pipe would silence the build's own
    # progress.
    mka adbd recoveryimage || return 1

    if [ ! "$IMG" -nt "$stamp" ]; then
        echo "==> build reported success but did not write $IMG" >&2
        return 1
    fi
    size=$(stat -c %s "$IMG")
    if [ "$size" -gt "$LIMIT" ]; then
        echo "==> $IMG is $size bytes, $((size - LIMIT)) over the $LIMIT of the partition" >&2
        return 1
    fi
    echo "==> recovery.img: $size bytes, $((LIMIT - size)) to spare"
    # The header is v0: dt_size is the little-endian word at offset 40.
    dt=$(python3 -c 'import struct,sys; f=open(sys.argv[1],"rb"); print(struct.unpack_from("<I", f.read(44), 40)[0])' "$IMG")
    if [ "${dt:-0}" -eq 0 ]; then
        echo "==> $IMG carries no device tree (dt_size 0) and would not boot" >&2
        return 1
    fi
    echo "    dt_size: $dt"
    fox=$(ls -t "$OUT"/OrangeFox-*.img 2>/dev/null | head -1)
    [ -n "$fox" ] && echo "==> $(ls -lh "$fox" | awk '{print $5, $NF}')"
    return 0
}

do_status() {
    echo "==> last build ($VER)"
    local OUT="$BUILD_DIR/out/target/product/$DEVICE"
    local f
    for f in "$OUT/recovery.img" "$(ls -t "$OUT"/OrangeFox-*.img 2>/dev/null | head -1)"; do
        if [ -n "$f" ] && [ -f "$f" ]; then
            ls -lh "$f"
        else
            echo "  none"
        fi
    done
    if [ -f "$ERROR_FILE" ]; then
        echo
        echo "  last run FAILED, $ERROR_FILE (last lines):"
        tail -3 "$ERROR_FILE"
    fi
}

do_full() {
    do_sync && do_post_sync && do_build
}

#==============================================================================
# Entry point — an argument when given, the menu otherwise (single-shot either way)
#==============================================================================

usage() {
    cat <<USAGE
usage: $(basename "$0") [<action>]

  action    init | manifest | sync | post-sync | clean | installclean | build
            | full | status
            init      = repo init of the twrp-12.1 minimal manifest and our
                        local manifest on top; downloads no sources
            manifest  = install manifests/mocha-fox_12.1.xml as the local
                        manifest (init and sync do this first)
            sync      = repo sync
            post-sync = the OrangeFox patches (orangefox/) and patches/
            installclean = drop the installed images and staging, keep the
                        object files
            build     = mka adbd recoveryimage, then check that the image is
                        new, fits the 20 MiB partition and carries the dtb
            full      = sync -> post-sync -> build (not init)

Without arguments the menu below is shown, so this stays usable by hand.
With an argument nothing is prompted, which is what lets it run over ssh.
USAGE
}

# Single dispatch point for both the argument form and the menu, so the
# error-file behaviour is identical however the script was started.
run_action() {
    case "$1" in
        init)      do_init ;;
        manifest)  do_manifest ;;
        sync)      do_sync ;;
        post-sync) do_post_sync ;;
        clean)     do_clean ;;
        installclean) do_installclean ;;
        build)     do_build ;;
        full)      do_full ;;
        status)    do_status ;;
        *) echo "unknown action: $1" >&2; return 1 ;;
    esac
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
esac

config_fox121

if [ $# -gt 0 ]; then
    if [ $# -ne 1 ]; then
        usage >&2
        exit 1
    fi
    run_action "$1"
    exit $?
fi

cat <<MENU

=============  OrangeFox fox_12.1 (mocha)  ===============
  1) init (repo init, downloads nothing)
  2) repo sync
  3) post-sync patches
  4) clean (make clobber)
  5) installclean (drop images, keep objects)
  6) build recovery
  7) full chain: sync -> post-sync -> build
  8) status (last image)
  9) install local manifest only
  q) quit
==========================================================
MENU
read -p "> " ans
case "$ans" in
    1) run_action init ;;
    2) run_action sync ;;
    3) run_action post-sync ;;
    4) run_action clean ;;
    5) run_action installclean ;;
    6) run_action build ;;
    7) run_action full ;;
    8) run_action status ;;
    9) run_action manifest ;;
    q|Q|"") echo "bye" ;;
    *) echo "unknown: $ans"; exit 1 ;;
esac
