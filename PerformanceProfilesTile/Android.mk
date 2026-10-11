LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    $(call all-java-files-under, src)

LOCAL_PRIVATE_PLATFORM_APIS := true

LOCAL_RESOURCE_DIR := \
    $(LOCAL_PATH)/res

LOCAL_AAPT_FLAGS := \
    --auto-add-overlay

# Platform-signed for SystemProperties, which is hidden API, and for the
# platform_app domain the sepolicy grants the property to. It needs no
# privileged permission, so it is not a priv-app.
LOCAL_CERTIFICATE := platform
LOCAL_MODULE_TAGS := optional
LOCAL_SYSTEM_EXT_MODULE := true

LOCAL_PACKAGE_NAME := PerformanceTile

include $(BUILD_PACKAGE)
