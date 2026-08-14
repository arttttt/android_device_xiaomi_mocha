#!/bin/bash
#
# Drive the recents->app transition and capture per-frame stage timings.
#
# Each cycle: open Settings, go home, open recents, reset gfxinfo, tap the
# recents card (this is the measured transition), then dump `gfxinfo
# framestats` for the launcher and SystemUI into OUTDIR. The reset just
# before the tap means every dump contains only the transition's frames.
#
# The dumps are raw; tools/frametrace_parse.py turns them into per-frame
# stage tables. Keep the raw files - they can be re-analyzed later.
#
# Usage: frametrace.sh OUTDIR [CYCLES]
#
# Tap point 765,930 is the center of the recents card on the 1536x2048
# panel with a single (Settings) task present. Re-check it if the recents
# layout or the seeded app changes.

set -eu

OUTDIR="${1:?usage: frametrace.sh OUTDIR [CYCLES]}"
CYCLES="${2:-8}"

PKGS="com.android.launcher3 com.android.systemui"
TAP_X=765
TAP_Y=930

mkdir -p "$OUTDIR"

adb shell input keyevent KEYCODE_WAKEUP
sleep 1

for i in $(seq 1 "$CYCLES"); do
    tag=$(printf 'run%02d' "$i")
    echo "== $tag =="

    # Seed recents with Settings and return to the launcher.
    adb shell am start -n com.android.settings/.Settings >/dev/null
    sleep 2
    adb shell input keyevent KEYCODE_HOME
    sleep 1.5
    adb shell input keyevent KEYCODE_APP_SWITCH
    sleep 2

    # Everything from here on is the transition under test.
    for pkg in $PKGS; do
        adb shell dumpsys gfxinfo "$pkg" reset >/dev/null
    done
    adb shell input tap "$TAP_X" "$TAP_Y"
    sleep 1.3

    for pkg in $PKGS; do
        short=${pkg##*.}
        adb shell dumpsys gfxinfo "$pkg" framestats > "$OUTDIR/${tag}_${short}.txt"
    done
done

echo "done: $CYCLES cycles in $OUTDIR"
