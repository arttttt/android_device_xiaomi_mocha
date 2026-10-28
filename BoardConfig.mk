DEVICE_PATH := device/xiaomi/mocha

# Architecture
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a15
TARGET_BOARD_PLATFORM := tegra
TARGET_BOOTLOADER_BOARD_NAME := mocha
TARGET_NO_BOOTLOADER := true

# Binder API
# As on 19.1: the kernel is built without ANDROID_BINDER_IPC_32BIT, and 12.1
# only links libbinder with 64-bit binder -- its arm32 version script names
# the symbols of that ABI, and with the 32-bit one the link fails on them.
TARGET_USES_64_BIT_BINDER := true

# Kernel
# loglevel=4 for the same reason as on 19.1: at the default console level the
# kernel's pin-debug spam goes out over synchronous 115200-baud printk.
# androidboot.hardware is not here on purpose. ro.hardware=tn8 comes from the
# kernel's own device tree -- the firmware/android node in tegra124-mocha.dts --
# and init reads it from there, so init.recovery.tn8.rc is found without it.
BOARD_KERNEL_CMDLINE := vpr_resize androidboot.selinux=permissive loglevel=4
BOARD_KERNEL_BASE := 0x10000000
BOARD_RAMDISK_OFFSET := 0x02000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_TAGS_OFFSET := 0x00000100

# The kernel is built from source exactly as 19.1 builds it: vendor/twrp on
# 12.1 carries the same kernel.mk and BoardConfigKernel.mk as vendor/lineage
# on 19.1, so the same settings mean the same thing here.
TARGET_KERNEL_SOURCE := kernel/xiaomi/mocha
TARGET_KERNEL_CONFIG := tegra12_android_defconfig
# This 3.10 kernel is built with GCC. Lineage 19.1 made clang the default
# (vendor/lineage 6d88852d) and keeps the toolchain below only for binutils,
# so without this the kernel came out of clang without anyone asking.
TARGET_KERNEL_CLANG_COMPILE := false
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

# The bootloader does not take a dtb appended to the kernel, so the dtb goes
# into the image as a section of its own, after the ramdisk, by mkbootimg.mk.
# The mkbootimg of 12.1 refuses --dt, so the one used is LineageOS 19.1's,
# which writes it the way this bootloader reads it.
BOARD_CUSTOM_BOOTIMG_MK := $(DEVICE_PATH)/mkbootimg.mk
BOARD_CUSTOM_MKBOOTIMG := $(DEVICE_PATH)/prebuilt/mkbootimg

# Partitions
#BOARD_SYSTEMIMAGE_PARTITION_SIZE := 671088640 # 640 Mb stock partition table
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1337564160 # 1.2 Gb
BOARD_USERDATAIMAGE_PARTITION_SIZE := 13742637056
BOARD_CACHEIMAGE_PARTITION_SIZE := 402653184
BOARD_BOOTIMAGE_PARTITION_SIZE := 20971520
BOARD_PERSISTIMAGE_PARTITION_SIZE := 16777216
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 20971520
BOARD_FLASH_BLOCK_SIZE := 131072

# Filesystems
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := f2fs
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true
TARGET_USES_MKE2FS := true

# Recovery
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/recovery/root/system/etc/recovery.fstab
BOARD_HAS_NO_SELECT_BUTTON := true
RECOVERY_SDCARD_ON_DATA := true

# TWRP
TW_THEME := portrait_hdpi
# The panel's backlight is the lp855x, registered as lcd-backlight with a
# maximum of 255 in the driver. The path this tree used to name,
# leds/button-backlight, is the backlight of the capacitive keys.
TW_BRIGHTNESS_PATH := "/sys/class/backlight/lcd-backlight/brightness"
TW_MAX_BRIGHTNESS := 255
TW_DEFAULT_BRIGHTNESS := 128
# CPU-therm is the first zone in the device tree, but the number a zone gets
# depends on the order the drivers register them; check thermal_zone*/type on
# the device.
TW_CUSTOM_CPU_TEMP_PATH := /sys/class/thermal/thermal_zone1/temp
TW_EXTERNAL_STORAGE_PATH := "/external_sd"
TW_EXTERNAL_STORAGE_MOUNT_POINT := "external_sd"
TW_NO_USB_STORAGE := true
# TWRP's own init.recovery.usb.rc writes the gadget to android_usb, which this
# kernel no longer has. The one in recovery/root replaces it.
TW_EXCLUDE_DEFAULT_USB_INIT := true
