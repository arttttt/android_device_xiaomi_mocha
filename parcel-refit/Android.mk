LOCAL_PATH := $(call my-dir)

# A static library nothing links: it exists only so that its object file
# carries a symbol whose SIZE is sizeof(android::Parcel) for this release.
# build/tasks/parcel-refit.mk depends on the archive and reads the size out of
# it with llvm-nm.  See parcel_size_probe.cpp for why the number is measured
# rather than written down.
include $(CLEAR_VARS)
LOCAL_MODULE       := parcel_size_probe
LOCAL_MODULE_TAGS  := optional
LOCAL_SRC_FILES    := parcel_size_probe.cpp
LOCAL_SHARED_LIBRARIES := libbinder
LOCAL_VENDOR_MODULE := false
include $(BUILD_STATIC_LIBRARY)
