#
# Copyright (C) 2014 The CyanogenMod Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

PRODUCT_AAPT_CONFIG += xlarge large
TARGET_SCREEN_HEIGHT := 2048
TARGET_SCREEN_WIDTH := 1536
TARGET_TEGRA_VERSION := t124

$(call inherit-product-if-exists, vendor/nvidia/shield/mocha.mk)

# Audio
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/media/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    $(LOCAL_PATH)/media/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml
    
PRODUCT_PACKAGES += \
    audio.a2dp.default \
    audio.usb.default \
    audio.r_submix.default \
    libaudio-resampler \
    libaudiospdif \
    libstagefrighthw \
    libtinycompress \
    libtinyalsa \
    tinycap \
    tinymix \
    tinyplay \
    xaplay \
    enctune.conf

# tinyhal, the open-source audio HAL this board switched to (BoardConfig sets
# BOARD_USES_TINYHAL_AUDIO). audio.primary.tegra comes from hidl/audio,
# libaudiohalcm from configmgr, and the *_mocha tools from our tinyalsa fork --
# the HAL links libtinyalsa_mocha, not the upstream libtinyalsa above.
PRODUCT_PACKAGES += \
    audio.primary.tegra \
    libaudiohalcm \
    libtinyalsa_mocha \
    tinycap_mocha \
    tinymix_mocha \
    tinypcminfo_mocha \
    tinyplay_mocha

# tinyhal's mixer configuration, including the fm_in path that routes FM audio.
# configmgr/audio_config.c builds the name as "/vendor/etc/audio.%s.xml".
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/media/audio.mocha.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio.mocha.xml

# aptXHD
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/aptXHD/libaptX_encoder.so:$(TARGET_OUT_VENDOR_SHARED_LIBRARIES)/libaptX_encoder.so \
    $(LOCAL_PATH)/aptXHD/libaptXHD_encoder.so:$(TARGET_OUT_VENDOR_SHARED_LIBRARIES)/libaptXHD_encoder.so

# Bluetooth
# Stays in vendor/etc, where a vendor config belongs since Android 8, even
# though this board has no separate vendor partition and it lands inside
# /system. Both readers are pointed at it instead: libbt through
# VENDOR_LIB_CONF_FILE in bluetooth/vnd_mocha.txt, brcm-uim-sysfs in its own
# source. Neither can find the pre-Treble /etc/bluetooth/ default any more.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/bluetooth/bt_vendor.conf:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth/bt_vendor.conf

# The secure world's image, carried so that installing the ROM installs it.
#
# The destination is relative to the product output directory, and that exact
# path is what makes it into the package: the build copies $(PRODUCT_OUT)/install
# into the target files as INSTALL/, and the package builder keeps INSTALL/*,
# where it becomes install/ inside the zip. That is the path releasetools.py
# hands to package_extract_file.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/firmware/tos-psci-0.1.img:install/firmware-update/tos-psci-0.1.img

# Graphics
#
# Our own allocator, which answers to gralloc.tegra and opens the vendor blob
# beside it. The blob is installed as gralloc.nvidia so that the name it used
# to own is free for the wrapper to take.
PRODUCT_PACKAGES += \
    gralloc.tegra

# Camera
PRODUCT_PACKAGES += \
    camera.tegra \
    imx179_primax.json \
    ov5693_sunny.json

# Comm Permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml
    
# Custom tiles
PRODUCT_PACKAGES += \
    ChargerTile \
    PerformanceTile
    
# Filesystem management tools
PRODUCT_PACKAGES += \
    fsck.f2fs mkfs.f2fs

# FM radio (BCM4354 V4L2 path through hardware/broadcom/fmradio)
PRODUCT_PACKAGES += \
    FMRadio \
    brcm-uim-sysfs \
    libfmradio.v4l2-fm \
    libfmjni

# Hardware composer, ours, from hardware/nvidia/hwcomposer. The HIDL pair
# that carries it to SurfaceFlinger is declared with the other interfaces in
# hidl/hidl.mk.
PRODUCT_PACKAGES += \
    hwcomposer.tegra

# The loader configuration for the software codec APEX.
#
# mediaswcodec runs out of com.android.media.swcodec and uses the APEX's own
# ld.config.txt, which is written for a device with a VNDK. This board has
# none, so its sphal namespace searches three vndk-sp directories that do not
# exist and the mapper implementation Codec2 needs can never be found --
# C2AllocatorGralloc ends up with a null IMapper and media.swcodec takes
# SIGSEGV on the first software codec that wants a graphic block.
#
# The file below is that configuration with /system/${LIB} added to the sphal
# search paths. It is only a source: init bind-mounts it over the copy inside
# the APEX, because the loader looks nowhere else. See configs/ld.config.txt
# for the reasoning and initfiles/init.tegra.rc for the mount.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/ld.config.txt:$(TARGET_COPY_OUT_SYSTEM)/etc/swcodec/ld.config.txt

# Graphics shim
PRODUCT_PACKAGES += libs \
                    libshim_zw \
                    libw

# RenderScript shims
#
# The board sets OVERRIDE_RS_DRIVER := libnvRSDriver.so, and that driver does
# not load: it needs the name libLLVM.so, which AOSP now builds as
# libLLVM_android.so, and the four-parameter GraphicBufferMapper::lock, which
# AOSP has since extended. Both are answered in shims/; see the comments
# there. Without them libRS quietly loads libRSDriver.so instead and the
# setting above is decoration.
PRODUCT_PACKAGES += libLLVM \
                    libshim_rs
                    
# HIDL HALs
$(call inherit-product, device/xiaomi/mocha/hidl/hidl.mk)

# keylayout
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/keylayout/tegra-kbc.kl:system/usr/keylayout/tegra-kbc.kl \
    $(LOCAL_PATH)/keylayout/gpio-keys.kl:system/usr/keylayout/gpio-keys.kl \
    $(LOCAL_PATH)/keylayout/Vendor_0955_Product_7210.kl:system/usr/keylayout/Vendor_0955_Product_7210.kl
    
# Media config
PRODUCT_COPY_FILES += \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_video.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_video.xml \
    $(LOCAL_PATH)/media/media_codecs.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs.xml \
    $(LOCAL_PATH)/media/media_profiles_V1_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_profiles_V1_0.xml \
    $(LOCAL_PATH)/media/media_codecs_performance.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_performance.xml
    
# NVIDIA
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/permissions/com.nvidia.blakemanager.xml:system/etc/permissions/com.nvidia.blakemanager.xml \
    $(LOCAL_PATH)/permissions/com.nvidia.feature.xml:system/etc/permissions/com.nvidia.feature.xml \
    $(LOCAL_PATH)/permissions/com.nvidia.feature.opengl4.xml:system/etc/permissions/com.nvidia.feature.opengl4.xml \
    $(LOCAL_PATH)/permissions/com.nvidia.nvsi.xml:system/etc/permissions/com.nvidia.nvsi.xml
NV_ANDROID_FRAMEWORK_ENHANCEMENTS := true

# Overlay
DEVICE_PACKAGE_OVERLAYS += \
    device/xiaomi/mocha/overlay

# Permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:system/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.camera.autofocus.xml:system/etc/permissions/android.hardware.camera.autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:system/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.camera.full.xml:system/etc/permissions/android.hardware.camera.full.xml \
    frameworks/native/data/etc/android.hardware.camera.raw.xml:system/etc/permissions/android.hardware.camera.raw.xml \
    frameworks/native/data/etc/android.hardware.ethernet.xml:system/etc/permissions/android.hardware.ethernet.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.opengles.aep.xml:system/etc/permissions/android.hardware.opengles.aep.xml \
    frameworks/native/data/etc/android.hardware.vulkan.compute-0.xml:system/etc/permissions/android.hardware.vulkan.compute-0.xml \
    frameworks/native/data/etc/android.hardware.vulkan.level-1.xml:system/etc/permissions/android.hardware.vulkan.level.xml \
    frameworks/native/data/etc/android.hardware.vulkan.version-1_0_3.xml:system/etc/permissions/android.hardware.vulkan.version.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:system/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:system/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:system/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:system/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:system/etc/permissions/android.hardware.wifi.direct.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/tablet_core_hardware.xml:system/etc/permissions/tablet_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepcounter.xml:system/etc/permissions/android.hardware.sensor.stepcounter.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepdetector.xml:system/etc/permissions/android.hardware.sensor.stepdetector.xml \
    frameworks/native/data/etc/android.software.freeform_window_management.xml:system/etc/permissions/android.software.freeform_window_management.xml
    
# Ramdisk and the board's init files. Only the fstab rides in the ramdisk; the
# rc files live in /vendor/etc/init/hw, where the second stage looks for
# init.<hardware>.rc, and ueventd's board file is named ueventd.rc in /vendor,
# which is the only name ueventd looks for there.
PRODUCT_PACKAGES += \
    fstab.tn8 \
    fstab.tn8.vendor \
    init.comms.rc \
    init.hdcp.rc \
    init.mocha.debug.rc \
    init.mocha.trace.rc \
    init.t124.rc \
    init.tegra.rc \
    init.tlk.rc \
    init.tn8.rc \
    init.tn8.usb.rc \
    init.tn8_common.rc \
    init.ussrd.rc \
    power.tn8.rc \
    power.mocha.rc \
    ueventd.rc \
    ussrd.conf \
    ussr_setup
    
# Sensors
PRODUCT_PACKAGES += \
    sensors.tegra

# System properties
-include $(LOCAL_PATH)/system_prop.mk

PRODUCT_CHARACTERISTICS := tablet

# Thermal
PRODUCT_PACKAGES += thermalhal.tn8.xml

# Vendor seccomp policy files for media components:
PRODUCT_COPY_FILES += \
    device/xiaomi/mocha/seccomp/mediacodec.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/mediacodec.policy \
    device/xiaomi/mocha/seccomp/mediaextractor.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/mediaextractor.policy

# Wifi
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/wifi/dhcpcd.conf:system/etc/dhcpcd/dhcpcd.conf \
    $(LOCAL_PATH)/wifi/p2p_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/p2p_supplicant_overlay.conf

# Wifi
# All Shield devices xurrently use broadcom wifi / bluetooth modules
$(call inherit-product-if-exists, hardware/broadcom/wlan/bcmdhd/config/config-bcm.mk)
PRODUCT_PACKAGES += \
    hostapd \
    wpa_supplicant \
    wpa_supplicant.conf

# wifi and bt macs settter
PRODUCT_PACKAGES += \
    conn_init

# VINTF
# target-level in the manifest is what picks the matrix: without it the device
# is UNSPECIFIED, which means legacy, and compatibility_matrix.legacy.xml
# accepts nearly anything -- which is how audio 2.0, health 1.0 and mapper 2.0
# went unnoticed. Level 4 is Q's own, so the device is now measured against
# what it actually runs on.
#
# This makes assemble_vintf verify the assembled vendor manifest against the
# framework matrix at build time instead of leaving the mismatch to be
# discovered on the device. Note what is deliberately NOT set:
# PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS, which would also check the
# kernel version and config against the matrix -- matrix.4 wants 4.9 or newer
# and this board runs 3.10, so that check would refuse a device that works.
PRODUCT_ENFORCE_VINTF_MANIFEST := true

# The age of the stack this board runs, not the date the tablet went on sale.
#
# AOSP defines this as the API level the device shipped with, and by that
# reading it would be 19: the Mi Pad went out on KitKat in 2014. But what it
# is used for is deciding which era's requirements apply, and the software
# here is not KitKat's. The kernel and the vendor blobs are tn8's -- the
# SHIELD Tablet -- and tn8's last release was Android 7. Twenty-four says
# that, and stops the build asking on our behalf for leniency granted to
# hardware five years older than what we actually carry.
#
# Free in both directions, which is why it can be said honestly rather than
# defensively. The thresholds in build/make/core/config.mk are 26 for
# PRODUCT_FULL_TREBLE and 28 for PRODUCT_COMPATIBLE_PROPERTY, PRODUCT_USE_VNDK
# (which would then force BOARD_VNDK_VERSION), BOARD_SYSTEMSDK_VERSIONS and
# the 64-bit binder requirement. Twenty-four clears none of them. At run time
# the two readers of ro.product.first_api_level test for greater than 28
# (ConnectivityService) and at most 29 (PackageManagerService), and 19 and 24
# fall the same side of both.
#
# Not verified: that tn8's last release was Android 7 is taken from what the
# board is, not from the blobs, which carry no version string to read.
PRODUCT_SHIPPING_API_LEVEL := 24

# Software gatekeeper
# The stock secure world runs, but its seven trusted applications do not
# include a gatekeeper and the image is signed, so there is no hardware one
# to reach. See gatekeeper/README for what this does and does not buy.
PRODUCT_PACKAGES += \
    gatekeeper.tegra

# Disable adb auth so adb works without on-device "Allow USB debugging?"
# prompt. Necessary because mocha has no UART and we may need to reach a
# half-booted device for early-boot debug without UI being available to
# tap Allow. /default.prop override (so adbd reads it before /system mounts).
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += \
    ro.adb.secure=0
