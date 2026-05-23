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

# aptXHD
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/aptXHD/libaptX_encoder.so:$(TARGET_OUT_VENDOR_SHARED_LIBRARIES)/libaptX_encoder.so \
    $(LOCAL_PATH)/aptXHD/libaptXHD_encoder.so:$(TARGET_OUT_VENDOR_SHARED_LIBRARIES)/libaptXHD_encoder.so

# Bluetooth
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/bluetooth/bt_vendor.conf:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth/bt_vendor.conf

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

# Graphics shim
PRODUCT_PACKAGES += libs \
                    libshim_zw \
                    libw
                    
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
    
# Ramdisk
PRODUCT_PACKAGES += \
    fstab.tn8 \
    init.comms.rc \
    init.hdcp.rc \
    init.mocha.debug.rc \
    init.t124.rc \
    init.tegra.rc \
    init.tlk.rc \
    init.tn8.rc \
    init.tn8.usb.rc \
    init.tn8_common.rc \
    init.ussrd.rc \
    power.tn8.rc \
    power.mocha.rc \
    ueventd.tn8.rc \
    ussrd.conf \
    ussr_setup
    
# Sensors
PRODUCT_PACKAGES += \
    sensors.tegra

# System properties
-include $(LOCAL_PATH)/system_prop.mk

PRODUCT_CHARACTERISTICS := tablet

# Thermal
PRODUCT_PACKAGES += thermal.tegra

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

# Disable adb auth so adb works without on-device "Allow USB debugging?"
# prompt. Necessary because mocha has no UART and we may need to reach a
# half-booted device for early-boot debug without UI being available to
# tap Allow. /default.prop override (so adbd reads it before /system mounts).
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += \
    ro.adb.secure=0
