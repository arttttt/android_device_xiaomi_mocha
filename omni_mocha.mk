PRODUCT_RELEASE_NAME := mocha

# twrp-12.1 is the AOSP flavour of the minimal manifest: the base is
# aosp_base_telephony with vendor/twrp, not full_base_telephony with
# vendor/omni as on older TWRP -- vendor/omni has no 12.1 branch.
$(call inherit-product, $(SRC_TARGET_DIR)/product/aosp_base_telephony.mk)
$(call inherit-product, vendor/twrp/config/common.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/languages_full.mk)
$(call inherit-product, device/xiaomi/mocha/device.mk)

PRODUCT_DEVICE := mocha
PRODUCT_NAME := omni_mocha
PRODUCT_BRAND := Xiaomi
PRODUCT_MODEL := MI PAD
PRODUCT_MANUFACTURER := Xiaomi

ALLOW_MISSING_DEPENDENCIES := true
