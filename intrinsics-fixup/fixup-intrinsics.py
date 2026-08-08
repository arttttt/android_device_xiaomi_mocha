#!/usr/bin/env python3
"""Shim pre-8.0 arm intrinsic references in installed blobs via libw.

Scans each given root for *.so files that both depend on libm.so and
reference one of the __aeabi_* symbols that were unhidden in Android 8.0.
Matching files have their DT_NEEDED entry and the affected symbol names
rewritten in place. All substitutions are the same length, so byte
offsets are preserved and no patchelf is needed.

Idempotent: once a file is patched the source strings are gone, so
subsequent runs skip it.
"""

import os
import sys

SUBS = [
    (b"libm.so",          b"libw.so"),
    (b"__aeabi_uldivmod", b"s_aeabi_uldivmod"),
    (b"__aeabi_ldivmod",  b"s_aeabi_ldivmod"),
    (b"__aeabi_d2lz",     b"s_aeabi_d2lz"),
    (b"__aeabi_d2ulz",    b"s_aeabi_d2ulz"),
    (b"__aeabi_l2d",      b"s_aeabi_l2d"),
    (b"__aeabi_ul2d",     b"s_aeabi_ul2d"),
    (b"__aeabi_f2lz",     b"s_aeabi_f2lz"),
    (b"__aeabi_f2ulz",    b"s_aeabi_f2ulz"),
    (b"__aeabi_l2f",      b"s_aeabi_l2f"),
    (b"__aeabi_ul2f",     b"s_aeabi_ul2f"),
]

AEABI_MARKERS = [src for src, _ in SUBS[1:]]

SHIM = "libw.so"


def needs_fixup(data):
    if b"libm.so" not in data:
        return False
    return any(m in data for m in AEABI_MARKERS)


def patch(data):
    for src, dst in SUBS:
        data = data.replace(src, dst)
    return data


def process(path):
    # Never rewrite the shim itself. libw.so carries the s_aeabi_* thunks, which
    # branch to the real __aeabi_* it links from libgcc, and it depends on libm.so
    # so the blobs keep reaching libm through it. Patching it renames the symbols
    # it is supposed to provide and points its own DT_NEEDED at itself.
    if os.path.basename(path) == SHIM:
        return False
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        # os.walk lists dangling symlinks, which cannot be opened. Staging has a
        # few, e.g. app/LatinIME/lib/arm/libjni_latinime.so.
        return False
    if not needs_fixup(data):
        return False
    new = patch(data)
    if new == data:
        return False
    with open(path, "wb") as f:
        f.write(new)
    return True


def main(argv):
    for root in argv[1:]:
        if not os.path.isdir(root):
            continue
        for dirpath, _, filenames in os.walk(root):
            for name in filenames:
                if not name.endswith(".so"):
                    continue
                path = os.path.join(dirpath, name)
                if process(path):
                    rel = os.path.relpath(path, root)
                    print("  [fixup-libw] %s" % rel)


if __name__ == "__main__":
    main(sys.argv)
