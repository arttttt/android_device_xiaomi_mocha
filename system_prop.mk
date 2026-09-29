# AptX
PRODUCT_PROPERTY_OVERRIDES += \
    persist.bt.enableAptXHD=true

# DRM
PRODUCT_PROPERTY_OVERRIDES += \
    drm.service.enabled=true

# Lineage genuine
PRODUCT_PROPERTY_OVERRIDES += \
    persist.lineage.nofool=true

# Media
PRODUCT_PROPERTY_OVERRIDES += \
    media.stagefright.thumbnail.prefer_hw_codecs=true

# OMX
PRODUCT_PROPERTY_OVERRIDES += \
    persist.media.treble_omx=false

# Radio
PRODUCT_PROPERTY_OVERRIDES += \
    ro.radio.noril=yes

# Storage
PRODUCT_PROPERTY_OVERRIDES += \
    ro.sys.sdcardfs=true

# Wifi
#
# Wi-Fi Direct has no network device of its own here. The driver offers a P2P
# device instead, which the supplicant creates beside wlan0 as p2p-dev-wlan0,
# and without a vendor Wi-Fi HAL the framework takes the name from this
# property alone. Left unset it asks for p2p0, which does not exist.
PRODUCT_PROPERTY_OVERRIDES += \
    persist.debug.wfd.enable=1 \
    wifi.direct.interface=p2p-dev-wlan0
