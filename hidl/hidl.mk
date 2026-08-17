# Audio
PRODUCT_PACKAGES += \
    android.hardware.audio@2.0-impl \
    android.hardware.audio@2.0-service \
    android.hardware.audio.effect@2.0-impl \

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

# Graphics
# The composer pair works like the camera one above: the passthrough impl
# loads hwcomposer.tegra through libhardware and the service exposes it over
# HIDL, which is the only way SurfaceFlinger reaches a composer here. The
# module itself is declared with the other hardware modules in device.mk.
PRODUCT_PACKAGES += \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.allocator@2.0-service \
    android.hardware.graphics.mapper@2.0-impl \
    android.hardware.graphics.composer@2.2-service

# Health HAL
PRODUCT_PACKAGES += \
    android.hardware.health@1.0-impl \
    android.hardware.health@1.0-service

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
    android.hardware.thermal@1.0-impl

# USB HAL
PRODUCT_PACKAGES += \
    android.hardware.usb@1.0-service

# Vibrator
PRODUCT_PACKAGES += \
    android.hardware.vibrator@1.0-service.mocha

# WiFi
PRODUCT_PACKAGES += \
    android.hardware.wifi@1.0-service \
    wificond

# HIDL
PRODUCT_COPY_FILES += \
    device/xiaomi/mocha/hidl/manifest.xml:system/vendor/manifest.xml
