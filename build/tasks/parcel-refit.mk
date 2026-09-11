# Widen the android::Parcel stack slots in the NVIDIA blobs, to whatever this
# release's Parcel actually measures.  See parcel-refit/refit-parcels.py for
# what is rewritten and why, and parcel-refit/parcel_size_probe.cpp for where
# the size comes from.
#
# Like intrinsics-fixup this has to live under build/tasks/: build/core/Makefile
# includes device/*/*/build/tasks/*.mk at its very end, so INSTALLED_SYSTEMIMAGE
# and INTERNAL_SYSTEMIMAGE_FILES already exist here and do not in a device
# Android.mk.  The work hangs on a stamp rather than a phony target because kati
# in Q rejects a real file depending on a phony one.
#
# Both consumers of the staged tree are hooked: `mka systemimage` builds
# INSTALLED_SYSTEMIMAGE, while `brunch` goes straight to the target-files
# package and never produces system.img at all.

PARCEL_REFIT_SCRIPT := device/xiaomi/mocha/parcel-refit/refit-parcels.py
PARCEL_REFIT_PROBE  := $(call intermediates-dir-for,STATIC_LIBRARIES,parcel_size_probe)/parcel_size_probe.a
PARCEL_REFIT_STAMP  := $(PRODUCT_OUT)/obj/PACKAGING/parcel_refit_intermediates/stamp

# Ordered after intrinsics-fixup rather than beside it: both rewrite files in
# the staged vendor tree, and make is free to run independent rules at the same
# time.  Two processes writing one blob is not a race worth discovering in a
# ROM zip.
$(PARCEL_REFIT_STAMP): $(PARCEL_REFIT_SCRIPT) $(PARCEL_REFIT_PROBE) \
                       $(INTERNAL_SYSTEMIMAGE_FILES) $(INTRINSICS_FIXUP_STAMP)
	@mkdir -p $(dir $@)
	$(hide) python3 $(PARCEL_REFIT_SCRIPT) --probe $(PARCEL_REFIT_PROBE) \
	        $(TARGET_OUT_VENDOR)
	$(hide) touch $@

$(INSTALLED_SYSTEMIMAGE): $(PARCEL_REFIT_STAMP)
$(BUILT_TARGET_FILES_PACKAGE): $(PARCEL_REFIT_STAMP)
