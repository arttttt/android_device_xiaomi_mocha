#!/bin/bash
# LineageOS (mocha) — unified sync / post-sync / clean / build menu.
# Version (14.1 / 15.1) is chosen via the first menu. Each version has its
# own config and its own post-sync patches — patches are per-system, never
# shared, even when they look similar.

export LC_ALL=C

#==============================================================================
# 14.1
#==============================================================================

config_141() {
    VER="14.1"
    V=141
    BUILD_DIR="/home/artem/DATA/projects/android/7.1.2"
    LOG="$HOME/build_lineage_14.1.log"
    REPO_INIT_URL="https://github.com/LineageOS/android.git"
    REPO_INIT_BRANCH="cm-14.1"
    REPO_INIT_FLAGS=""
    DEVICE_TREE_BRANCH="cm-14.1"

    # Old JDK 8u252 (last release before TLS 1.3 / stricter SSL defaults).
    export JAVA_HOME="$BUILD_DIR/prebuilts/jdk/linux-x86/jdk8u252-b09"
    export PATH="$JAVA_HOME/bin:$BUILD_DIR/prebuilts/python/linux-x86/2.7.5/bin:$PATH"

    # Kernel toolchain — prebuilts/gcc/.../arm-linux-androideabi-4.9 from
    # the LOS 14.1 era miscompiles silently on modern glibc (2.34+, Manjaro
    # 2026). Force linaro-4.9.4 which is portable across host versions.
    # Override-friendly: set the vars in env before invoking this script
    # to pin a different toolchain.
    : "${KERNEL_TOOLCHAIN:=/home/artem/Projects/toolchain/linaro-4.9.4/bin}"
    : "${TARGET_KERNEL_CROSS_COMPILE_PREFIX:=arm-linux-gnueabihf-}"
    export KERNEL_TOOLCHAIN TARGET_KERNEL_CROSS_COMPILE_PREFIX
}

post_sync_141() {
    echo "==> post-sync patches (14.1)"
    patch_zlib_141 || return 1
    patch_fmradio_141 || return 1
    echo "==> post-sync OK"
}

# Python 2.7.5 zlib.so — prebuilt has no zlib module; without it the final
# packaging step fails. Compile from source against the system 32-bit libz.
patch_zlib_141() {
    local PY="$BUILD_DIR/prebuilts/python/linux-x86/2.7.5"
    if "$PY/bin/python" -c "import zlib" 2>/dev/null; then
        echo "  zlib.so: import works"
        return 0
    fi
    echo "  zlib.so: building (32-bit, gcc -m32 + system libz)"
    local TMP=$(mktemp -d)
    ( cd "$TMP" \
      && curl -fsSL https://www.python.org/ftp/python/2.7.5/Python-2.7.5.tgz -o py.tgz \
      && tar xzf py.tgz Python-2.7.5/Modules/zlibmodule.c \
      && gcc -m32 -shared -fPIC \
             -I"$PY/include/python2.7" \
             -DUSE_ZLIB_CRC32 -O2 \
             Python-2.7.5/Modules/zlibmodule.c \
             -lz \
             -o "$PY/lib/python2.7/lib-dynload/zlib.so" )
    rm -rf "$TMP"
    "$PY/bin/python" -c "import zlib; zlib.compress(b'x')" \
        && echo "  zlib.so: OK" \
        || { echo "  zlib.so: FAIL" >&2; return 1; }
}


# FMRadio: backport upstream commit 5a56adcb3169 ("jni: Add broadcom FM to
# the guard") — wraps include $(BUILD_SHARED_LIBRARY) in
# FMRadio/jni/fmr/Android.mk with ifneq ($(BOARD_HAVE_BCM_FM),true) so its
# libfmjni doesn't collide with the broadcom variant. Committed locally
# (no fork, no push — sync wipes it, post-sync re-applies).
patch_fmradio_141() {
    local FMR="$BUILD_DIR/packages/apps/FMRadio"
    local FMR_MK="$FMR/jni/fmr/Android.mk"
    local FMR_SUBJ='jni: backport upstream guard to skip libfmjni when BOARD_HAVE_BCM_FM=true'
    cd "$FMR" || return 1
    if git log --format=%s -50 | grep -qF "$FMR_SUBJ"; then
        echo "  FMRadio: commit already present"
        return 0
    fi
    if [ ! -f "$FMR_MK" ]; then
        echo "  FMRadio: $FMR_MK missing — skipping"
        return 0
    fi
    if ! grep -q "BOARD_HAVE_BCM_FM" "$FMR_MK"; then
        echo "  FMRadio: editing Android.mk"
        FMR_MK="$FMR_MK" python3 - <<'PYEOF' || return 1
import os, sys
path = os.environ['FMR_MK']
with open(path) as f: data = f.read()
old = 'ifneq ($(BOARD_USES_QCOM_HARDWARE),true)\ninclude $(BUILD_SHARED_LIBRARY)\nendif\n'
new = ('ifneq ($(BOARD_USES_QCOM_HARDWARE),true)\n'
       'ifneq ($(BOARD_HAVE_BCM_FM),true)\n'
       'include $(BUILD_SHARED_LIBRARY)\n'
       'endif # BOARD_HAVE_BCM_FM\n'
       'endif\n')
if old not in data:
    sys.stderr.write('  FMRadio: expected block not found — upstream changed\n')
    sys.exit(1)
with open(path, 'w') as f: f.write(data.replace(old, new))
PYEOF
    fi
    echo "  FMRadio: committing locally"
    git add jni/fmr/Android.mk
    git commit -m "$FMR_SUBJ

Locally backports LineageOS/android_packages_apps_FMRadio commit
5a56adcb3169 (\"jni: Add broadcom FM to the guard\"), which never
made it to cm-14.1. Without this, the MTK V4L2 libfmjni from this
app collides with the Broadcom V4L2 libfmjni from hardware/broadcom/fm
on mocha (BOARD_HAVE_BCM_FM=true) — same LOCAL_MODULE name.

Local-only — no fork, not pushed."
    echo "  FMRadio: OK"
}

#==============================================================================
# 15.1
#==============================================================================

config_151() {
    VER="15.1"
    V=151
    BUILD_DIR="/home/artem/DATA/projects/android/8.1.0"
    LOG="$HOME/build_lineage_15.1.log"
    REPO_INIT_URL="https://github.com/LineageOS/android.git"
    REPO_INIT_BRANCH="lineage-15.1"
    REPO_INIT_FLAGS="--git-lfs"
    DEVICE_TREE_BRANCH="lineage-15.1"

    # OpenJDK 8 from prebuilts (LOS 15.1 requires 1.8).
    export JAVA_HOME="$BUILD_DIR/prebuilts/jdk/jdk8/linux-x86"
    export PATH="$JAVA_HOME/bin:$BUILD_DIR/prebuilts/python/linux-x86/2.7.5/bin:$PATH"

    # Kernel toolchain — device BoardConfig pins androideabi-4.9 via ?=,
    # so an env override wins. Use linaro-4.9.4 (portable across host
    # glibc versions), same scheme as 14.1 but owned by this config.
    : "${KERNEL_TOOLCHAIN:=/home/artem/Projects/toolchain/linaro-4.9.4/bin}"
    : "${TARGET_KERNEL_CROSS_COMPILE_PREFIX:=arm-linux-gnueabihf-}"
    export KERNEL_TOOLCHAIN TARGET_KERNEL_CROSS_COMPILE_PREFIX
}

# NOTE: no FMRadio / broadcom-fm patches on 15.1 — the BCM guard
# (FMRadio 036cbf2) and the clang fix (fm f7e8514) are committed in the
# arttttt fork branches lineage-15.1, which the local manifest checks
# out. 14.1 keeps its FMRadio post-sync patch — its fork branch does
# not carry the guard.

post_sync_151() {
    echo "==> post-sync patches (15.1)"
    patch_zlib_151 || return 1
    patch_lfs_151 || return 1
    patch_trees_151 || return 1
    echo "==> post-sync OK"
}

# Patches for upstream projects we do not fork. They live in the device tree
# under patches/, in a directory named after the project they apply to, so
# patches/frameworks/base/*.patch goes to $BUILD_DIR/frameworks/base. A sync
# resets those projects, so this re-applies after every one.
#
# Idempotent: a patch that reverse-applies is already in, and is skipped.
patch_trees_151() {
    local root="$BUILD_DIR/device/xiaomi/mocha/patches"
    if [ ! -d "$root" ]; then
        echo "  patches: $root missing — sync the device tree first" >&2
        return 1
    fi
    local p proj
    for p in $(find "$root" -name '*.patch' | sort); do
        proj=$(dirname "${p#$root/}")
        local dir="$BUILD_DIR/$proj"
        if [ ! -d "$dir/.git" ]; then
            echo "  $proj: not a git project, skipping $(basename "$p")" >&2
            continue
        fi
        if git -C "$dir" apply --check --reverse -p1 "$p" >/dev/null 2>&1; then
            echo "  $proj: $(basename "$p") already applied"
        elif git -C "$dir" apply -p1 "$p" 2>/dev/null; then
            echo "  $proj: applied $(basename "$p")"
        else
            echo "  $proj: FAILED to apply $(basename "$p")" >&2
            return 1
        fi
    done
}

# Python 2.7.5 zlib.so — the AOSP prebuilt at prebuilts/python/linux-x86/2.7.5
# ships without a zlib module. build_image.py / signapk / etc. all
# `import gzip` -> `import zlib` and bomb out late with
# `ImportError: No module named zlib`. Build the missing 32-bit .so from
# upstream Python 2.7.5 source against the system libz.
patch_zlib_151() {
    local PY="$BUILD_DIR/prebuilts/python/linux-x86/2.7.5"
    if "$PY/bin/python" -c "import zlib" 2>/dev/null; then
        echo "  zlib.so: import works"
        return 0
    fi
    echo "  zlib.so: building (32-bit, gcc -m32 + system libz)"
    local TMP=$(mktemp -d)
    ( cd "$TMP" \
      && curl -fsSL https://www.python.org/ftp/python/2.7.5/Python-2.7.5.tgz -o py.tgz \
      && tar xzf py.tgz Python-2.7.5/Modules/zlibmodule.c \
      && gcc -m32 -shared -fPIC \
             -I"$PY/include/python2.7" \
             -DUSE_ZLIB_CRC32 -O2 \
             Python-2.7.5/Modules/zlibmodule.c \
             -lz \
             -o "$PY/lib/python2.7/lib-dynload/zlib.so" )
    rm -rf "$TMP"
    "$PY/bin/python" -c "import zlib; zlib.compress('x')" \
        && echo "  zlib.so: OK" \
        || { echo "  zlib.so: FAIL" >&2; return 1; }
}

# Git LFS pull for chromium-webview prebuilts. `repo sync` only pulls LFS
# pointer files (~134 B each); without the real .apk the build fails at
# packaging webview with:
#   target Prebuilt: webview ... FAILED
#   java.util.zip.ZipException: error in opening zip file
# Requires git-lfs installed system-wide.
patch_lfs_151() {
    local LFS_PROJECTS=(
        external/chromium-webview/prebuilt/arm
        external/chromium-webview/prebuilt/arm64
        external/chromium-webview/prebuilt/x86
        external/chromium-webview/prebuilt/x86_64
    )
    if ! command -v git-lfs >/dev/null 2>&1; then
        echo "  git-lfs: not installed — install with 'sudo pacman -S git-lfs'" >&2
        return 1
    fi
    local proj
    for proj in "${LFS_PROJECTS[@]}"; do
        local apk="$BUILD_DIR/$proj/webview.apk"
        if [ ! -f "$apk" ]; then
            echo "  $proj: no webview.apk, skipping"
            continue
        fi
        local size=$(stat -c %s "$apk")
        if [ "$size" -lt 1000 ]; then
            echo "  $proj: LFS pointer ($size B) — pulling"
            ( cd "$BUILD_DIR/$proj" && git lfs pull )
            local newsize=$(stat -c %s "$apk")
            echo "    -> $newsize B"
        else
            echo "  $proj: already $((size / 1024 / 1024)) MB"
        fi
    done
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
    repo sync -j$(nproc) --force-sync
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

do_build() {
    echo "==> brunch $DEVICE ($VER)"
    echo "    KERNEL_TOOLCHAIN = $KERNEL_TOOLCHAIN"
    echo "    KERNEL_PREFIX    = $TARGET_KERNEL_CROSS_COMPILE_PREFIX"
    if [ ! -x "$KERNEL_TOOLCHAIN/${TARGET_KERNEL_CROSS_COMPILE_PREFIX}gcc" ]; then
        echo "==> ERROR: kernel gcc not found at $KERNEL_TOOLCHAIN/${TARGET_KERNEL_CROSS_COMPILE_PREFIX}gcc" >&2
        return 1
    fi
    "$BUILD_DIR/prebuilts/sdk/tools/jack-admin" kill-server 2>/dev/null || true
    cd "$BUILD_DIR"
    source build/envsetup.sh
    brunch "$DEVICE" 2>&1 | tee "$LOG"

    local OUT="$BUILD_DIR/out/target/product/$DEVICE"
    local ZIP=$(ls -t "$OUT"/lineage-$VER-*.zip 2>/dev/null | head -1)
    if [ -n "$ZIP" ]; then
        echo "==> ROM: $(ls -lh "$ZIP" | awk '{print $5, $NF}')"
    else
        echo "==> ROM not built — see $LOG" >&2
        return 1
    fi
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
    if [ -f "$LOG" ]; then
        echo
        echo "  log $LOG (last lines):"
        tail -3 "$LOG"
    fi
}

#==============================================================================
# Entry point — arguments when given, menus otherwise (single-shot either way)
#==============================================================================

usage() {
    cat <<EOF
usage: $(basename "$0") [<version> <action>]

  version   14.1 | 15.1
  action    manifest | sync | post-sync | clean | build | full | status
            manifest = install manifests/mocha-<ver>.xml as the local manifest
                       (sync does this first, so it is only needed on its own
                       when adding a repo without a full sync)
            full     = sync -> post-sync -> build

Without arguments the interactive menus below are shown, so this stays usable
by hand. With arguments nothing is prompted, which is what lets it run over
ssh or from a build agent, where there is no terminal to answer a prompt.
EOF
}

select_version() {
    case "$1" in
        14.1|141) config_141 ;;
        15.1|151) config_151 ;;
        *) echo "unknown version: $1" >&2; return 1 ;;
    esac
}

run_action() {
    case "$1" in
        manifest)  do_manifest ;;
        sync)      do_sync ;;
        post-sync) do_post_sync ;;
        clean)     do_clean ;;
        build)     do_build ;;
        full)      do_sync && do_post_sync && do_build ;;
        status)    do_status ;;
        *) echo "unknown action: $1" >&2; return 1 ;;
    esac
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
esac

if [ $# -gt 0 ]; then
    if [ $# -ne 2 ]; then
        usage >&2
        exit 1
    fi
    select_version "$1" || exit 1
    run_action "$2"
    exit $?
fi

cat <<EOF

==================  LineageOS (mocha)  ===================
  1) 14.1
  2) 15.1
  q) quit
==========================================================
EOF
read -p "> " ver_ans
case "$ver_ans" in
    1) config_141 ;;
    2) config_151 ;;
    q|Q|"") echo "bye"; exit 0 ;;
    *) echo "unknown: $ver_ans"; exit 1 ;;
esac

cat <<EOF

================  LineageOS $VER (mocha)  ================
  1) repo sync
  2) post-sync patches
  3) clean (make clobber)
  4) build (brunch)
  5) full chain: sync -> post-sync -> build
  6) status (last ROM)
  7) install local manifest only
  q) quit
==========================================================
EOF
read -p "> " ans
case "$ans" in
    1) do_sync ;;
    2) do_post_sync ;;
    3) do_clean ;;
    4) do_build ;;
    5) do_sync && do_post_sync && do_build ;;
    6) do_status ;;
    7) do_manifest ;;
    q|Q|"") echo "bye" ;;
    *) echo "unknown: $ans"; exit 1 ;;
esac
