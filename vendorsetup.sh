export FOX_BUILD_DEVICE=mocha
export TARGET_ARCH=arm
# No MIUI on this board, so none of OrangeFox's MIUI handling.
export FOX_VANILLA_BUILD=1
# Size. Kernel, dtb and ramdisk have to fit the 20 MiB partition. Even with
# these two left out a gzipped ramdisk did not, so the ramdisk is LZMA
# (OF_USE_LZMA_COMPRESSION in tools/build_lineage.sh; the kernel unpacks it
# since SmokeR24.1-stable enables CONFIG_RD_LZMA).
export FOX_EXCLUDE_NANO_EDITOR=1
export FOX_REMOVE_AAPT=1
export OF_MAINTAINER="arttttt"
# The partitions under the names this board has. OrangeFox looks for them as
# recovery and system: the installer zip under /dev/block/by-name, foxstart.sh
# under /dev/block/bootdevice/by-name. Here they are SOS and APP, and there is
# no bootdevice link, so the zip would write the image nowhere and foxstart.sh
# would not find the ROM's build.prop. The controller is named as in
# recovery.fstab.
export FOX_RECOVERY_INSTALL_PARTITION=/dev/block/platform/sdhci-tegra.3/by-name/SOS
export FOX_RECOVERY_SYSTEM_PARTITION=/dev/block/platform/sdhci-tegra.3/by-name/APP
