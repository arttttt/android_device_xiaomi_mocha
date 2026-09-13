LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := stdio_vsnprintf.cpp
LOCAL_C_INCLUDES := \
    bionic/libc \
    bionic/libc/stdio \
    bionic/libc/async_safe/include
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_MODULE := libs
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := zygote_whitelist.cpp
LOCAL_C_INCLUDES := frameworks/base/core/jni \
                    system/core/base/include
ifneq ($(ZYGOTE_WHITELIST_PATH_EXTRA),)
    LOCAL_CFLAGS += -DPATH_WHITELIST_EXTRA=$(ZYGOTE_WHITELIST_PATH_EXTRA)
endif
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_MODULE := libshim_zw
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

# libLLVM.so, which AOSP now builds as libLLVM_android.so and this board's
# RenderScript blobs still ask for by the old name. Without a library
# answering to it, libnvRSDriver.so cannot be loaded at all -- the linker
# refuses on the missing DT_NEEDED entry long before any symbol is looked up.
# Measured on the device, by asking the linker to load it:
#
#     $ LD_PRELOAD=/vendor/lib/libnvRSDriver.so /system/bin/true
#     CANNOT LINK EXECUTABLE: library "libLLVM.so" not found
#
# so RenderScript has been falling back to the platform driver and
# OVERRIDE_RS_DRIVER has been decoration.
#
# Linking libLLVM_android makes every symbol it exports reachable through this
# name; llvm_accessors.cpp adds the four it does not export. See that file.
include $(CLEAR_VARS)
LOCAL_SRC_FILES := llvm_accessors.cpp
LOCAL_SHARED_LIBRARIES := libLLVM_android
LOCAL_CFLAGS := -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -Wno-unused-parameter
LOCAL_MODULE := libLLVM
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true
include $(BUILD_SHARED_LIBRARY)

# The pre-Android-8 GraphicBufferMapper::lock, attached by the linker to
# libnvRSDriver.so only -- BoardConfig.mk names the pair in
# TARGET_LD_SHIM_LIBS, and the path there must be the realpath the linker
# reports (/system/vendor/lib/...), since that is what it matches on.
include $(CLEAR_VARS)
LOCAL_SRC_FILES := rs_mapper_lock.cpp
LOCAL_SHARED_LIBRARIES := libui
LOCAL_MODULE := libshim_rs
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true
include $(BUILD_SHARED_LIBRARY)
