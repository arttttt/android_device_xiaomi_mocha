export FOX_BUILD_DEVICE=mocha
export TARGET_ARCH=arm
# No MIUI on this board, so none of OrangeFox's MIUI handling.
export FOX_VANILLA_BUILD=1
# Size. The ramdisk has to fit, gzipped, in what the 20 MiB partition leaves
# after the kernel and the dtb -- about 14 MB -- and LZMA is not an option: the
# kernel is built without CONFIG_RD_LZMA and would not unpack it.
export FOX_EXCLUDE_NANO_EDITOR=1
export FOX_REMOVE_AAPT=1
export OF_MAINTAINER="arttttt"
