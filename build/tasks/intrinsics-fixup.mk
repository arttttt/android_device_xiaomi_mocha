# Rewrite the pre-8.0 arm intrinsic references in the vendor blobs so they
# resolve through libw (see intrinsics-fixup/fixup-intrinsics.py and shims/libw).
#
# This has to live under build/tasks/: build/core/Makefile includes
# device/*/*/build/tasks/*.mk at its very end, so INSTALLED_SYSTEMIMAGE and
# INTERNAL_SYSTEMIMAGE_FILES already exist by the time this is parsed.
#
# In a device Android.mk they do not. main.mk includes the device makefiles at
# :427 and build/core/Makefile only at :915, where those variables are defined
# (:1525 and :1951), so `$(INSTALLED_SYSTEMIMAGE): intrinsics-fixup` expanded to
# an empty target and the edge was never created. Deferring the prerequisites
# with .SECONDEXPANSION did not help either -- kati lists it in
# kUnsupportedBuiltinTargets (build/kati/dep.cc) and merely warns. The rule was
# dead on both sides, which is why the blobs shipped unpatched and SurfaceFlinger
# aborted on `EGLContext creation failed`.
#
# Only TARGET_OUT_VENDOR is scanned: every blob that needs this is installed
# there, and walking all of TARGET_OUT would drag in unrelated system libraries
# plus dangling symlinks such as app/LatinIME/lib/arm/libjni_latinime.so.

# Both consumers of the staged tree have to be hooked. `mka systemimage` builds
# INSTALLED_SYSTEMIMAGE, but `brunch` does not: it goes straight to the
# target-files package, which depends on FULL_SYSTEMIMAGE_DEPS and never
# produces system.img at all. Hooking only the former left the blobs unpatched
# in every ROM zip.

# The work is hung on a stamp file rather than on a phony target. kati in Q
# refuses the latter outright -- "real file system.img depends on PHONY target
# intrinsics-fixup" -- and a stamp says the same thing in terms it accepts: the
# image and the target-files package depend on a file, that file depends on the
# staged tree, and the script runs when the staged tree is newer than it.

INTRINSICS_FIXUP_SCRIPT := device/xiaomi/mocha/intrinsics-fixup/fixup-intrinsics.py
INTRINSICS_FIXUP_STAMP := $(PRODUCT_OUT)/obj/PACKAGING/intrinsics_fixup_intermediates/stamp

$(INTRINSICS_FIXUP_STAMP): $(INTRINSICS_FIXUP_SCRIPT) $(INTERNAL_SYSTEMIMAGE_FILES)
	@mkdir -p $(dir $@)
	@python3 $(INTRINSICS_FIXUP_SCRIPT) $(TARGET_OUT_VENDOR)
	$(hide) touch $@

$(INSTALLED_SYSTEMIMAGE): $(INTRINSICS_FIXUP_STAMP)
$(BUILT_TARGET_FILES_PACKAGE): $(INTRINSICS_FIXUP_STAMP)
