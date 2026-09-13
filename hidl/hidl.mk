# Audio
# The service keeps its 2.0 name and is the right one anyway: it registers
# whatever it can find, newest first --
#
#     bool fail = registerPassthroughServiceImplementation<audio::V5_0::IDevicesFactory>() != OK &&
#                 registerPassthroughServiceImplementation<audio::V4_0::IDevicesFactory>() != OK &&
#                 registerPassthroughServiceImplementation<audio::V2_0::IDevicesFactory>() != OK;
#
# so it was serving 2.0 only because 2.0 was the only implementation beside
# it. compatibility_matrix.4.xml asks for 5.0 with optional="false". The
# wrapper still opens a legacy audio_hw_device through libhardware and only
# refuses one older than AUDIO_DEVICE_API_VERSION_MIN, which is 2.0 --
# audio.primary.tegra declares exactly that, so it goes on working unchanged.
PRODUCT_PACKAGES += \
    android.hardware.audio@5.0-impl \
    android.hardware.audio@2.0-service \
    android.hardware.audio.effect@5.0-impl \

# Bluetooth
PRODUCT_PACKAGES += \
    libbt-vendor \
    android.hardware.bluetooth@1.0-impl \
    android.hardware.bluetooth@1.0-service

# Camera
# The legacy wrapper: it loads camera.tegra through libhardware and exposes it
# over HIDL, which is the only way cameraserver reaches a camera HAL on O.
# The device impls it needs (camera.device@1.0/3.2/3.3-impl) come in as its
# shared_libs, so they need no entry of their own.
PRODUCT_PACKAGES += \
    android.hardware.camera.provider@2.4-impl \
    android.hardware.camera.provider@2.4-service

# DRM HAL
PRODUCT_PACKAGES += \
    android.hardware.drm@1.0-impl \
    android.hardware.drm@1.0-service

# Gatekeeper
# The service carries no crypto: it finds an implementation through
# hw_get_module_by_class() and exposes it over HIDL. Ours is the software
# module built from gatekeeper/, installed as gatekeeper.tegra.
PRODUCT_PACKAGES += \
    android.hardware.gatekeeper@1.0-impl \
    android.hardware.gatekeeper@1.0-service

# Graphics
# The composer pair works like the camera one above: the passthrough impl
# loads hwcomposer.tegra through libhardware and the service exposes it over
# HIDL, which is the only way SurfaceFlinger reaches a composer here. The
# module itself is declared with the other hardware modules in device.mk.
PRODUCT_PACKAGES += \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.allocator@2.0-service \
    android.hardware.graphics.mapper@2.0-impl-2.1 \
    android.hardware.graphics.composer@2.2-service

# Health HAL
# The generic service, not our own: it reads the battery through
# libbatterymonitor and /sys/class/power_supply, which is all this board
# offers anyway. It declares "overrides: healthd", so the framework's own
# healthd stops being installed -- and that is the point. With health@1.0
# from us and healthd from the system, the device served IHealth twice:
#
#     DM,FC android.hardware.health@1.0::IHealth/default   (ours)
#     FM    android.hardware.health@2.0::IHealth/backup    (healthd)
#
# The framework was reading the battery through its own fallback while our
# HAL sat beside it answering an interface Q no longer asks for.
PRODUCT_PACKAGES += \
    android.hardware.health@2.0-service

# Keymaster
PRODUCT_PACKAGES += \
    android.hardware.keymaster@3.0-impl \
    android.hardware.keymaster@3.0-service

# Light
PRODUCT_PACKAGES += \
    android.hardware.light@2.0-service.mocha

# Memtrack
PRODUCT_PACKAGES += \
    android.hardware.memtrack@1.0-impl \
    android.hardware.memtrack@1.0-service

# Power
PRODUCT_PACKAGES += \
    android.hardware.power@1.0-service.mocha

# Renderscript
PRODUCT_PACKAGES += \
    android.hardware.renderscript@1.0-impl

# Sensors
PRODUCT_PACKAGES += \
    android.hardware.sensors@1.0-impl \
    android.hardware.sensors@1.0-service

# Thermal
PRODUCT_PACKAGES += \
    android.hardware.thermal@1.0-service-nvidia

# USB HAL
# The AOSP default service finds its ports by walking the Type-C sysfs class,
# and mocha has a micro-USB socket, so it reports none. A device with no ports
# has no data role, and Settings greys out the whole USB Preferences screen:
# UsbDetailsFunctionsController.refresh() disables the function list unless
# dataRole == DATA_ROLE_DEVICE, and that role comes from a port status that
# was never there. LineageOS keeps a service for exactly this shape of device
# -- one fixed UFP port, sink power, device data, nothing switchable. Naming
# only UFP among the supported modes is what makes the "USB controlled by"
# section disappear rather than sit there greyed: the framework derives the
# supported role combinations from that field, and a port that cannot be a
# host leaves a single data role, which Settings hides.
PRODUCT_PACKAGES += \
    android.hardware.usb@1.0-service.basic

# Vibrator
PRODUCT_PACKAGES += \
    android.hardware.vibrator@1.0-service.mocha

# WiFi
PRODUCT_PACKAGES += \
    android.hardware.wifi@1.0-service \
    wificond

# The device manifest is declared in BoardConfig.mk through
# DEVICE_MANIFEST_FILE, not copied into place from here.
#
# It used to be copied, to /vendor/manifest.xml, which is where Android 8 and
# 9 read it from. Q reads /vendor/etc/vintf/manifest.xml and nothing else, so
# every vendor interface on this board was undeclared: hwservicemanager
# answered "Cannot find entry ... in either framework or device manifest" for
# each one in turn, and the framework treated running services as absent. Wifi
# is where it showed -- HalDeviceManager kept saying "isWifiStarted called but
# mWifi is null" while android.hardware.wifi@1.0-service sat there running.
#
# DEVICE_MANIFEST_FILE installs it where Q looks and, more usefully, checks it
# against the framework compatibility matrix at build time. A version
# understated here now fails the build instead of silently hiding a HAL.
