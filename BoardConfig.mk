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

# This variable is set first, so it can be overridden
# by BoardConfigVendor.mk

TARGET_SPECIFIC_HEADER_PATH := device/xiaomi/mocha/include

# Architecture
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_VARIANT := cortex-a15

# Audio
BOARD_USES_GENERIC_AUDIO := false
BOARD_USES_ALSA_AUDIO := true
BOARD_USES_TINYHAL_AUDIO := true
USE_XML_AUDIO_POLICY_CONF := 1

# Binder API
TARGET_USES_64_BIT_BINDER := true

# Bluetooth
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_BCM := true
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR ?= device/xiaomi/mocha/bluetooth
BOARD_CUSTOM_BT_CONFIG := device/xiaomi/mocha/bluetooth/vnd_mocha.txt

# Board
TARGET_BOARD_PLATFORM := tegra
TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true

# Boot animation
TARGET_SCREEN_HEIGHT := 2048
TARGET_SCREEN_WIDTH := 1536

# Unlocks SurfaceFlinger's colour management, not a claim about the
# glass: the panel is sRGB-class and the composer never reports a wide
# mode, so every wide-gamut path stays off on its own per-display
# check. What this opens is the mode table -- sRGB and native, each
# with its render intents -- which SurfaceFlinger refuses to even ask
# about while this is unset.
TARGET_HAS_WIDE_COLOR_DISPLAY := true
TARGET_BOOTANIMATION_HALF_RES := true

# FM radio (Broadcom V4L2 over BT shared transport ldisc)
BOARD_HAVE_BCM_FM := true
BOARD_HAVE_BCM_FM_SYSFS := "/sys/devices/platform/bcm_ldisc/"
BOARD_BRCM_HCI_NUM := 26

# FS
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := f2fs
TARGET_EXFAT_DRIVER := sdfat
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true
TARGET_USES_MKE2FS := true

# Graphics
USE_OPENGL_RENDERER := true

# How long after a vertical blank each side of the compositor wakes up.
#
# Left unset these are one millisecond each, which is the framework's own
# default and is too tight for this hardware. The compositor's check that the
# previous frame has actually reached the panel is made at its offset, and the
# fence that answers it cannot be ready by then: between the frame boundary and
# the fence being signalled there are two scheduler hops -- the display
# controller's flip thread and the host1x threaded interrupt. The check
# therefore failed on most frames, and a failed check makes the compositor skip
# the whole refresh. Measured: forty frames a second where the panel does
# sixty, and fifty-eight once the compositor is given five milliseconds instead
# of one.
#
# The application is woken later still, so that what it draws is picked up in
# the same cycle rather than the next. The pair is Google's own on both the
# Pixel C, which is the nearest relative of this board, and the Nexus 5, which
# shares nothing with it but the year -- so it is a sensible starting point
# rather than a value tuned to one panel.
#
# Read at build time only on this release: they are compiled into
# android.hardware.configstore@1.1-service, which the compositor then asks over
# its interface. `dumpsys SurfaceFlinger | grep DispSync` says what is in force.
VSYNC_EVENT_PHASE_OFFSET_NS := 7500000
SF_VSYNC_EVENT_PHASE_OFFSET_NS := 5000000

# How many buffers the compositor's own frame gets.
#
# Unset it is two, and two is a ceiling of half the refresh rate on any frame
# the compositor has to draw itself: with only two it cannot begin the next one
# until the display lets go of the one it is showing, so drawing and scanning
# out take turns instead of overlapping.
#
# It only bites on some frames, which is why it looks like a fault in the
# animations rather than a setting. This controller has three windows. An
# ordinary screen -- the application, the status bar, the navigation bar --
# fits in them exactly and never touches the compositor's own drawing. Pulling
# the shade down or opening the drawer adds a window, a scrim and a dim layer,
# the three windows are not enough, and what does not fit is drawn by the
# compositor. So the ceiling appears exactly when something is moving.
#
# The third buffer lets the drawing of one frame overlap the display of the
# last. It costs one screen of memory. Every other board of this family sets
# this and only this one had been left on the default.
NUM_FRAMEBUFFER_SURFACE_BUFFERS := 3

# Include an expanded selection of fonts
EXTENDED_FONT_FOOTPRINT := true

# Init
TARGET_INIT_VENDOR_LIB := mocha_init
TARGET_RECOVERY_DEVICE_MODULES := mocha_init

# Kernel
# loglevel=4: the kernel prints a dozen falcon/nvhost pin-debug lines
# per msenc/vic job; at the default console loglevel every encoder job
# serializes behind ~1.3 KB of synchronous 115200-baud UART printk,
# capping video recording at ~5 fps. Level 4 keeps warnings/panics on
# the wire and INFO spam off it.
BOARD_KERNEL_CMDLINE := vpr_resize androidboot.selinux=permissive console=ttyS0,115200n8 loglevel=4
BOARD_KERNEL_BASE := 0x10000000
BOARD_RAMDISK_OFFSET := 0x02000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_TAGS_OFFSET := 0x00000100

# Per-frame tracing in the hardware composer, compiled in but not running.
#
# It is worth carrying: it says which buffer went to which window of the
# controller, and nothing else answers that. It is not worth paying for
# unasked. A line of it is a message to another process and there are five in
# every frame, which measured here as three and a half milliseconds out of
# sixteen -- taken quietly out of the client's share of the frame, and out of
# every measurement made through it.
#
# So this only decides whether it exists. Whether it runs is decided on the
# device, and the answer is no unless asked:
#
#     setprop vendor.hwc.trace 1
TARGET_HWC_TRACE := true

TARGET_KERNEL_SOURCE := kernel/xiaomi/mocha
TARGET_KERNEL_CONFIG := tegra12_android_defconfig
# Relative to the tree root, which is where the build always runs from.
# ANDROID_BUILD_TOP used to spell this and is now a hard error, so any build
# that did not already carry KERNEL_TOOLCHAIN in its environment died in
# dumpvars with "ANDROID_BUILD_TOP is obsolete" and no product spec. The `?=`
# hid it: with the variable set outside, the right-hand side was never
# expanded and the error never appeared.
KERNEL_TOOLCHAIN ?= ./prebuilts/gcc/$(HOST_OS)-x86/arm/arm-linux-androideabi-4.9/bin
TARGET_KERNEL_CROSS_COMPILE_PREFIX ?= arm-linux-androideabi-
BOARD_KERNEL_IMAGE_NAME := zImage
BOARD_KERNEL_SEPARATED_DT := true
BOARD_MKBOOTIMG_ARGS := --ramdisk_offset $(BOARD_RAMDISK_OFFSET) --tags_offset $(BOARD_KERNEL_TAGS_OFFSET)
BOARD_CUSTOM_BOOTIMG_MK := device/xiaomi/mocha/mkbootimg.mk

#BOARD_SYSTEMIMAGE_PARTITION_SIZE := 671088640 # 640 Mb stock partition table
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1337564160 # 1.2 Gb
BOARD_USERDATAIMAGE_PARTITION_SIZE := 13742637056
BOARD_CACHEIMAGE_PARTITION_SIZE := 402653184
BOARD_BOOTIMAGE_PARTITION_SIZE := 20971520
BOARD_PERSISTIMAGE_PARTITION_SIZE := 16777216
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 20971520
BOARD_FLASH_BLOCK_SIZE := 131072

# LINEAGEHW
JAVA_SOURCE_OVERLAYS := org.lineageos.hardware|device/xiaomi/mocha/lineagehw|**/*.java

# Offmode Charging
BOARD_CHARGER_DISABLE_INIT_BLANK := true
BACKLIGHT_PATH := "/sys/class/backlight/lcd-backlight/brightness"
RED_LED_PATH := "/sys/class/leds/red/brightness"
GREEN_LED_PATH := "/sys/class/leds/green/brightness"
BLUE_LED_PATH := "/sys/class/leds/blue/brightness"

# Per-application sizes for shader cache
MAX_EGL_CACHE_SIZE := 4194304
MAX_EGL_CACHE_ENTRY_SIZE := 262144

# Recovery
TARGET_RECOVERY_DEVICE_DIRS += device/xiaomi/mocha
TARGET_RECOVERY_FSTAB := device/xiaomi/mocha/initfiles/fstab.tn8
BOARD_NO_SECURE_DISCARD := true

# RenderScript
OVERRIDE_RS_DRIVER := libnvRSDriver.so

# SELinux
BOARD_PLAT_PRIVATE_SEPOLICY_DIR += device/xiaomi/mocha/sepolicy/private
BOARD_PLAT_PUBLIC_SEPOLICY_DIR  += device/xiaomi/mocha/sepolicy/public
BOARD_SEPOLICY_DIRS             += device/xiaomi/mocha/sepolicy/vendor
                       
# ThermalHAL
TARGET_THERMALHAL_VARIANT := tegra

# Use unified vendor
TARGET_TEGRA_VARIANT := shield

# Wifi related defines
BOARD_WPA_SUPPLICANT_DRIVER      := NL80211
WPA_SUPPLICANT_VERSION           := VER_0_8_X
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_bcmdhd
BOARD_WLAN_DEVICE                := bcmdhd
BOARD_HOSTAPD_DRIVER             := NL80211
BOARD_HOSTAPD_PRIVATE_LIB        := lib_driver_cmd_bcmdhd
WIFI_DRIVER_FW_PATH_STA          := "/vendor/firmware/mocha_fw_bcmdhd.bin"
WIFI_DRIVER_FW_PATH_AP           := "/vendor/firmware/mocha_fw_bcmdhd_apsta.bin"
WIFI_DRIVER_FW_PATH_PARAM        := "/sys/module/bcmdhd/parameters/firmware_path"
#WIFI_DRIVER_MODULE_ARG           := "iface_name=wlan0"
#WIFI_DRIVER_MODULE_NAME          := "bcmdhd"
                       
# Zygote whitelist extra paths
ZYGOTE_WHITELIST_PATH_EXTRA := \"/dev/nvhost-ctrl\",\"/dev/nvmap\",
