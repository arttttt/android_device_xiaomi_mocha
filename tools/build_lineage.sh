#!/bin/bash
# LineageOS (mocha) — unified sync / post-sync / clean / build menu.
# Version is chosen via the first menu. Each version has its own config and
# its own post-sync patches — patches are per-system, never shared, even when
# they look similar.
#
# Only the versions still being worked on live here. 14.1, 15.1 and 16.0 were
# dropped; each of those device tree branches carries its own copy of this
# script, and that copy is the one to build them with.

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
patch_trees() {
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
    local proj dir p idx expected
    for proj in $(find "$root" -name '*.patch' -exec dirname {} \; | sort -u); do
        proj=${proj#$root/}
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
        idx=$(mktemp)
        expected=
        if GIT_INDEX_FILE="$idx" git -C "$dir" read-tree HEAD; then
            expected=ok
            for p in $(find "$root/$proj" -maxdepth 1 -name '*.patch' | sort); do
                if ! GIT_INDEX_FILE="$idx" git -C "$dir" apply --cached -p1 "$p" 2>/dev/null; then
                    expected=
                    break
                fi
            done
            [ -n "$expected" ] && expected=$(GIT_INDEX_FILE="$idx" git -C "$dir" write-tree)
        fi
        rm -f "$idx"
        if [ -n "$expected" ] && git -C "$dir" diff --quiet "$expected"; then
            echo "  $proj: already applied"
        else
            echo "  $proj: FAILED - local changes that are not its patches" >&2
            return 1
        fi
    done
    return 0
}

#==============================================================================
# 17.1
#==============================================================================

config_171() {
    VER="17.1"
    V=171
    BUILD_DIR="/home/artem/DATA/projects/android/10.0.0"
    REPO_INIT_URL="https://github.com/LineageOS/android.git"
    REPO_INIT_BRANCH="lineage-17.1"
    REPO_INIT_FLAGS="--git-lfs"
    DEVICE_TREE_BRANCH="lineage-17.1"

    # No JDK and no python here. Q hands out both
    # itself: soong_ui sets JAVA_HOME to prebuilts/jdk/jdk9 and prepends it to
    # PATH (build/soong/ui/build/config.go), overwriting whatever we export,
    # and prebuilts/build-tools/path carries python, python2 and python2.7 as
    # links to its own py2-cmd, which is what the build scripts' shebangs find.
    # Setting either here only makes the file look like it decides something.

    # What the host does still get wrong is mke2fs. The tree's copy is from
    # 2018 and reads /etc/mke2fs.conf, which on a rolling distribution lists
    # features it has never heard of, so building an APEX payload dies with
    #
    #   Invalid filesystem option set: ...,orphan_file
    #
    # The tree ships a configuration of its own; point the tool at it.
    export MKE2FS_CONFIG="$BUILD_DIR/system/extras/ext4_utils/mke2fs.conf"

    # The in-tree androideabi-4.9 pin miscompiles on this host, linaro-4.9.4
    # does not. Env overrides still win.
    : "${KERNEL_TOOLCHAIN:=/home/artem/Projects/toolchain/linaro-4.9.4/bin}"
    : "${TARGET_KERNEL_CROSS_COMPILE_PREFIX:=arm-linux-gnueabihf-}"
    export KERNEL_TOOLCHAIN TARGET_KERNEL_CROSS_COMPILE_PREFIX
}

# Only what the bring-up actually hit, so the list stays a record of observed
# needs rather than inherited habit.
post_sync_171() {
    echo "==> post-sync patches (17.1)"
    # The zlib.so and webview LFS hacks the pre-Q trees needed are not wanted:
    # Q hands out its own python through prebuilts/build-tools, and this tree
    # syncs with --git-lfs, so the webview prebuilts arrive whole.
    #
    # The tree patches are a different matter. They were left off while it was
    # unclear which of them Q had made unnecessary, and that turned out to cost
    # more than it saved: without 0001-libbt-fm-bt-via-v4l2 the controller is
    # brought up twice, once by the kernel's shared line discipline and once by
    # libbt, and the second pass wedges it -- Bluetooth restarts every sixteen
    # seconds and never pairs. That patch's own message had already said 17.1
    # would need it.
    patch_trees || return 1
    echo "==> post-sync OK"
}

#==============================================================================
# 18.1
#==============================================================================

config_181() {
    VER="18.1"
    V=181
    BUILD_DIR="/home/artem/DATA/projects/android/11.0.0"
    REPO_INIT_URL="https://github.com/LineageOS/android.git"
    REPO_INIT_BRANCH="lineage-18.1"
    REPO_INIT_FLAGS="--git-lfs"
    DEVICE_TREE_BRANCH="lineage-18.1"

    # The tree's mke2fs is from 2019 and reads /etc/mke2fs.conf, which on a
    # rolling distribution lists features it has never heard of, so building
    # an APEX payload dies with
    #
    #   Invalid filesystem option set: ...,orphan_file
    #
    # as it did on 17.1. The tree ships a configuration of its own.
    export MKE2FS_CONFIG="$BUILD_DIR/system/extras/ext4_utils/mke2fs.conf"

    # The in-tree androideabi-4.9 pin miscompiles the kernel on this host,
    # linaro-4.9.4 does not. Anything else 17.1 needed comes back only when
    # the bring-up hits the failure it cures.
    : "${KERNEL_TOOLCHAIN:=/home/artem/Projects/toolchain/linaro-4.9.4/bin}"
    : "${TARGET_KERNEL_CROSS_COMPILE_PREFIX:=arm-linux-gnueabihf-}"
    export KERNEL_TOOLCHAIN TARGET_KERNEL_CROSS_COMPILE_PREFIX
}

# The tree patches, reviewed against R: the three Codec2 stride fixes and the
# Jelly relaunch fix are upstream there and were dropped; the rest were
# carried over (the CCodec and SurfaceFlinger ones ported to R's code).
post_sync_181() {
    echo "==> post-sync patches (18.1)"
    patch_trees || return 1
    echo "==> post-sync OK"
}

#==============================================================================
# 19.1
#==============================================================================

# The bring-up starts bare: the repo, its branch and where the tree lives,
# nothing else. 18.1's host fixes (MKE2FS_CONFIG, the linaro kernel
# toolchain) and the tree patches each answered a failure 18.1 hit; on S they
# come back one at a time, when the build hits the failure they cure.
config_191() {
    VER="19.1"
    V=191
    BUILD_DIR="/home/artem/DATA/projects/android/12.0.0"
    REPO_INIT_URL="https://github.com/LineageOS/android.git"
    REPO_INIT_BRANCH="lineage-19.1"
    REPO_INIT_FLAGS="--git-lfs"
    DEVICE_TREE_BRANCH="lineage-19.1"
}

# Nothing yet. The device tree's patches/ was carried over from 18.1 and was
# written against R; it is not applied until each patch has been looked at
# against S.
post_sync_191() {
    echo "==> post-sync (19.1): nothing to apply yet"
    echo "==> post-sync OK"
}

#==============================================================================
# Actions (version-agnostic, driven by config_* vars)
#==============================================================================

DEVICE="mocha"
DEVICE_TREE_PATH="device/xiaomi/$DEVICE"
DEVICE_TREE_URL="https://github.com/arttttt/android_device_xiaomi_mocha"

# The local manifest is the list of every repository the build needs beyond
# upstream LineageOS. Keeping it inside .repo means it exists on exactly one
# machine and nowhere in history, which is how hardware/nvidia/libstagefrighthw
# went missing on 15.1: device.mk asked for the package, no repo provided it,
# and the build said nothing -- hardware video decode was simply absent.
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

do_sync() {
    echo "==> repo sync ($VER)"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    if [ ! -d .repo ]; then
        repo init -u "$REPO_INIT_URL" -b "$REPO_INIT_BRANCH" $REPO_INIT_FLAGS
    fi
    do_manifest || return 1
    # repo sync's exit status is the only thing that distinguishes a finished
    # sync from one that gave up halfway. It reports per-project failures on
    # stderr and keeps going, so without this check the function announced
    # "sync OK" over a tree that had not moved -- and the build that followed
    # used whatever was there. A local edit to a tracked file is enough to
    # trigger it: --force-sync overrides a project whose path or remote
    # changed, not a dirty working file.
    if ! repo sync -j$(nproc) --force-sync; then
        echo "  repo sync did not complete - the tree is not at the revisions" >&2
        echo "  the manifest names, so do not build from it" >&2
        return 1
    fi
    echo "==> sync OK"
}

do_post_sync() {
    "post_sync_$V"
}

do_clean() {
    echo "==> make clobber ($VER)"
    cd "$BUILD_DIR"
    source build/envsetup.sh
    lunch "lineage_${DEVICE}-userdebug"
    make clobber
    echo "==> clean OK"
}

# Whether the device manifest actually satisfies the framework's requirements
# is a question the build does not answer here, despite
# PRODUCT_ENFORCE_VINTF_MANIFEST being set. Two independent reasons:
#
#   - the rule that runs the check hangs off the vendor image, and mocha has
#     no vendor partition at all -- /vendor is a symlink into /system, so
#     INSTALLED_VENDORIMAGE_TARGET is empty and verified_assembled_vendor_manifest.xml
#     is never built;
#   - BUILT_SYSTEM_MATRIX, which that rule passes to assemble_vintf as the
#     matrix to check against, is referenced four times in
#     build/make/core/Makefile and assigned nowhere in the tree. Even where
#     the rule does run, it expands to "-c" with no file, which is syntax
#     validation and nothing more.
#
# So the check is run here instead, on the images the last build produced: the
# framework matrices it installed, combined, against the device manifest it
# assembled. Exit zero means the manifest is compatible at whatever
# target-level it claims. Run it after touching hidl/manifest.xml.
do_vintf() {
    echo "==> verify VINTF manifest ($VER)"
    local out="$BUILD_DIR/out/target/product/$DEVICE"
    local av="$BUILD_DIR/out/host/linux-x86/bin/assemble_vintf"
    if [ ! -x "$av" ]; then
        echo "  assemble_vintf not built yet - build first" >&2
        return 1
    fi
    # A device with a real vendor partition puts the manifest in the vendor
    # image; ours lands under system/vendor. Take whichever exists.
    local manifest
    for manifest in "$out/vendor/etc/vintf/manifest.xml" \
                    "$out/system/vendor/etc/vintf/manifest.xml"; do
        [ -f "$manifest" ] && break
    done
    if [ ! -f "$manifest" ]; then
        echo "  no assembled device manifest under $out - build first" >&2
        return 1
    fi
    local mats
    mats=$(ls "$out"/system/etc/vintf/compatibility_matrix.*.xml 2>/dev/null \
           | tr '\n' ':' | sed 's/:$//')
    if [ -z "$mats" ]; then
        echo "  no framework matrices under $out/system/etc/vintf - build first" >&2
        return 1
    fi
    echo "  manifest: $manifest"

    # The device manifest is not one file. Since R, services install their own
    # fragments beside it in vintf/manifest/, and libvintf merges them on the
    # device; one HAL declared both there and in manifest.xml makes the whole
    # device manifest unusable, and hwservicemanager then turns away every
    # HIDL service on the board. Checking manifest.xml alone passed exactly
    # that image. So merge the fragments first, as the device does -- a
    # duplicate fails here -- and check the result.
    local frags merged
    frags=$(ls "$(dirname "$manifest")"/manifest/*.xml 2>/dev/null | tr '\n' ':' | sed 's/:$//')
    merged=$(mktemp)
    echo "  fragments: $(echo "$frags" | tr ':' '\n' | grep -c . )"
    if ! "$av" -i "$manifest${frags:+:$frags}" -o "$merged"; then
        echo "  the manifest and its fragments do not merge (a HAL declared twice?)" >&2
        rm -f "$merged"
        return 1
    fi
    manifest="$merged"
    trap 'rm -f "$merged"' RETURN

    if PRODUCT_ENFORCE_VINTF_MANIFEST=true "$av" -i "$mats" -c "$manifest" \
            -o /dev/null; then
        echo "==> VINTF OK"
    else
        echo "  the manifest is not compatible with the framework matrices" >&2
        return 1
    fi
}

# Removing a module from PRODUCT_PACKAGES does not remove the file it already
# installed: out/ keeps it, the next package picks it up, and the image ships
# an implementation the device no longer declares. That bit us moving the HAL
# set to Q -- audio@2.0-impl, audio.effect@2.0-impl and mapper@2.0-impl stayed
# beside their 5.0 and 2.1 replacements, and the mapper pair is genuinely
# ambiguous: the passthrough loader searches for libraries whose name starts
# with the requested version's prefix, both of those match
# android.hardware.graphics.mapper@2.0-impl, and which one answers is whatever
# order the directory happens to give.
#
# installclean is the narrow tool for it: the installed images and the staging
# directories go, out/soong and the object files stay, so the rebuild is
# minutes rather than the hours clobber costs. Reach for it whenever a module
# leaves PRODUCT_PACKAGES.
#
# It has one gap, and it is ours to close. The list of what installclean
# removes lives in the build system and names the staging directories it
# knows about -- system, vendor, ramdisk, the images, and so on. It does not
# name out/target/product/<device>/install, the directory whose contents the
# package builder carries into the zip as install/. So a file that stops
# being copied there stays, and ships.
#
# That is not hypothetical: renaming the secure world's image from tos.img to
# tos-psci-0.1.img left both in the package through two rebuilds, one of them
# an installclean, and the whole point of the rename was that nobody should
# have to wonder which TOS they are looking at. Removing the directory is
# safe -- every file in it is put there by PRODUCT_COPY_FILES on the next
# build.
do_installclean() {
    echo "==> make installclean ($VER)"
    cd "$BUILD_DIR"
    source build/envsetup.sh
    lunch "lineage_${DEVICE}-userdebug"
    make installclean

    local staged="$BUILD_DIR/out/target/product/$DEVICE/install"
    if [ -d "$staged" ]; then
        echo "  also dropping $staged, which installclean does not"
        rm -rf "$staged"
    fi

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

do_build() {
    echo "==> brunch $DEVICE ($VER)"
    check_kernel_toolchain || return 1
    cd "$BUILD_DIR"
    source build/envsetup.sh

    # Not piped anywhere: the whole run is already captured by the pty wrapper
    # at the top of this script, and a pipe here would silence the build's own
    # progress exactly as it silenced repo sync. Its exit status is what
    # decides success -- a stale zip from an earlier run must not read as one.
    brunch "$DEVICE" || return 1

    local OUT="$BUILD_DIR/out/target/product/$DEVICE"
    local ZIP=$(ls -t "$OUT"/lineage-$VER-*.zip 2>/dev/null | head -1)
    if [ -n "$ZIP" ]; then
        echo "==> ROM: $(ls -lh "$ZIP" | awk '{print $5, $NF}')"
    else
        echo "==> build reported success but produced no zip" >&2
        return 1
    fi
}

# The kernel and ramdisk only, for a kernel change: minutes where brunch
# spends most of its time packing system into a zip nobody flashes. Built
# through the same lunch and toolchain as the full build, so the image is
# the one brunch would have put in the zip.
do_bootimage() {
    echo "==> m bootimage ($VER)"
    check_kernel_toolchain || return 1
    cd "$BUILD_DIR"
    source build/envsetup.sh
    lunch "lineage_${DEVICE}-userdebug" || return 1

    local OUT="$BUILD_DIR/out/target/product/$DEVICE"
    local stamp
    stamp=$(mktemp)
    trap 'rm -f "$stamp"' RETURN

    m bootimage || return 1

    # As with the zip: success is a boot.img written by this run, not one
    # left over from an earlier one
    if [ "$OUT/boot.img" -nt "$stamp" ]; then
        echo "==> boot.img: $(ls -lh "$OUT/boot.img" | awk '{print $5, $NF}')"
        strings "$OUT/kernel" 2>/dev/null | grep -m1 '^Linux version' | sed 's/^/    /'
    else
        echo "==> build reported success but did not write boot.img" >&2
        return 1
    fi
}

# Modules by directory, for a change that lives in a few of them (the audio
# HAL is device/xiaomi/mocha/hidl/audio and, for libaudiohalcm,
# device/xiaomi/mocha/configmgr). mmm builds what those directories define
# and installs it into out/; it does not repack any image, and files a
# product copies (PRODUCT_COPY_FILES, such as the audio XMLs) are not its
# business. What it installed is listed at the end, as the files to push.
do_mmm() {
    if [ $# -eq 0 ]; then
        echo "==> mmm needs at least one directory, relative to $BUILD_DIR" >&2
        return 1
    fi
    echo "==> mmm $* ($VER)"
    cd "$BUILD_DIR"
    local d
    for d in "$@"; do
        if [ ! -d "$d" ]; then
            echo "==> ERROR: no directory $BUILD_DIR/$d" >&2
            return 1
        fi
    done
    source build/envsetup.sh
    lunch "lineage_${DEVICE}-userdebug" || return 1

    local OUT="$BUILD_DIR/out/target/product/$DEVICE"
    local stamp
    stamp=$(mktemp)
    trap 'rm -f "$stamp"' RETURN

    mmm "$@" || return 1

    echo "==> installed by this run:"
    find "$OUT/system" "$OUT/vendor" "$OUT/root" "$OUT/recovery/root" \
         -newer "$stamp" -type f 2>/dev/null \
        | sed "s|^$OUT/|    |" | sort
}

do_status() {
    echo "==> last build ($VER)"
    local OUT="$BUILD_DIR/out/target/product/$DEVICE"
    local ZIP=$(ls -t "$OUT"/lineage-$VER-*.zip 2>/dev/null | head -1)
    if [ -n "$ZIP" ]; then
        ls -lh "$ZIP"
    else
        echo "  none"
    fi
    if [ -f "$ERROR_FILE" ]; then
        echo
        echo "  last run FAILED, $ERROR_FILE (last lines):"
        tail -3 "$ERROR_FILE"
    fi
}

#==============================================================================
# Entry point — arguments when given, menus otherwise (single-shot either way)
#==============================================================================

usage() {
    cat <<EOF
usage: $(basename "$0") [<version> <action>]

  version   17.1 | 18.1 | 19.1
  action    manifest | sync | post-sync | clean | installclean | build
            | bootimage | mmm <dir>... | vintf | full | status
            manifest = install manifests/mocha-<ver>.xml as the local manifest
                       (sync does this first, so it is only needed on its own
                       when adding a repo without a full sync)
            installclean = drop the installed images and staging, keep the
                       object files. Use after a module leaves
                       PRODUCT_PACKAGES: the file it already installed
                       survives in out/ otherwise and ships in the image
            vintf    = check the assembled device manifest against the
                       framework matrices of the last build. The build does
                       not do this for us here; see do_vintf for why
            bootimage = the kernel and boot.img only, no zip
            mmm      = build and install the modules of the given
                       directories (relative to the tree), and list what
                       was installed. The audio HAL is
                       device/xiaomi/mocha/hidl/audio device/xiaomi/mocha/configmgr
            full     = sync -> post-sync -> build

Without arguments the interactive menus below are shown, so this stays usable
by hand. With arguments nothing is prompted, which is what lets it run over
ssh or from a build agent, where there is no terminal to answer a prompt.
EOF
}

select_version() {
    case "$1" in
        17.1|171) config_171 ;;
        18.1|181) config_181 ;;
        19.1|191) config_191 ;;
        *) echo "unknown version: $1" >&2; return 1 ;;
    esac
}

do_full() {
    do_sync && do_post_sync && do_build
}

# Single dispatch point for both the argument form and the menu, so the
# error-file behaviour is identical however the script was started.
run_action() {
    local action="$1"
    shift
    case "$action" in
        manifest)  do_manifest ;;
        sync)      do_sync ;;
        post-sync) do_post_sync ;;
        clean)     do_clean ;;
        installclean) do_installclean ;;
        vintf)     do_vintf ;;
        build)     do_build ;;
        bootimage) do_bootimage ;;
        mmm)       do_mmm "$@" ;;
        full)      do_full ;;
        status)    do_status ;;
        *) echo "unknown action: $action" >&2; return 1 ;;
    esac
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
esac

if [ $# -gt 0 ]; then
    # Only mmm takes more than the action: its directories
    if [ $# -lt 2 ] || { [ $# -gt 2 ] && [ "$2" != mmm ]; }; then
        usage >&2
        exit 1
    fi
    select_version "$1" || exit 1
    run_action "${@:2}"
    exit $?
fi

cat <<EOF

==================  LineageOS (mocha)  ===================
  1) 17.1
  2) 18.1
  3) 19.1
  q) quit
==========================================================
EOF
read -p "> " ver_ans
case "$ver_ans" in
    1) config_171 ;;
    2) config_181 ;;
    3) config_191 ;;
    q|Q|"") echo "bye"; exit 0 ;;
    *) echo "unknown: $ver_ans"; exit 1 ;;
esac

cat <<EOF

================  LineageOS $VER (mocha)  ================
  1) repo sync
  2) post-sync patches
  3) clean (make clobber)
  4) installclean (drop images, keep objects)
  5) build (brunch)
  6) full chain: sync -> post-sync -> build
  7) status (last ROM)
  8) install local manifest only
  9) verify VINTF manifest
 10) boot.img only (m bootimage)
 11) modules by directory (mmm)
  q) quit
==========================================================
EOF
read -p "> " ans
case "$ans" in
    1) run_action sync ;;
    2) run_action post-sync ;;
    3) run_action clean ;;
    4) run_action installclean ;;
    5) run_action build ;;
    6) run_action full ;;
    7) run_action status ;;
    8) run_action manifest ;;
    9) run_action vintf ;;
    10) run_action bootimage ;;
    11) read -p "directories: " -a dirs
        run_action mmm "${dirs[@]}" ;;
    q|Q|"") echo "bye" ;;
    *) echo "unknown: $ans"; exit 1 ;;
esac
