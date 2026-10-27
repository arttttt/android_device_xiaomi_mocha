LOCAL_PATH := device/xiaomi/mocha

PRODUCT_CHARACTERISTICS := tablet

# FunctionFS on this 3.10 kernel has no AIO path. Both adbd and TWRP's MTP
# server read this and take the compat one (MtpServer picks
# MtpFfsCompatHandle).
PRODUCT_PROPERTY_OVERRIDES += \
    sys.usb.ffs.aio_compat=1
