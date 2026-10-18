LOCAL_PATH := $(call my-dir)

# The Vulkan driver where an application can find it by name.
#
# libvulkan opens the driver as plain "vulkan.tegra.so" through
# android_load_sphal_library, meant for the sphal namespace and its
# /vendor/lib/hw. This board is not Treble (ro.treble.enabled=false), so
# linkerconfig gives every process the legacy configuration: one default
# namespace, /vendor/lib without hw/, and no sphal at all. The loader falls
# back to that namespace, does not find the driver, and an application sees
# no Vulkan device although the features are declared. The composer never
# noticed: hwcvk opens the driver by its full path.
#
# A link beside the libraries the driver itself loads (libglcore and the
# rest are in /vendor/lib already) is all the default namespace needs.
VULKAN_DRIVER_LINK := $(TARGET_OUT_VENDOR)/lib/vulkan.tegra.so
$(VULKAN_DRIVER_LINK):
	@mkdir -p $(dir $@)
	$(hide) ln -sf hw/vulkan.tegra.so $@

ALL_DEFAULT_INSTALLED_MODULES += $(VULKAN_DRIVER_LINK)
