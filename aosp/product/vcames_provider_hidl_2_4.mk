# Explicit Android 11/12 HIDL 2.4 external-provider branch.
VCAMES_PATH := $(call my-dir)/../..
$(call inherit-product, $(VCAMES_PATH)/aosp/product/vcames.mk)

PRODUCT_PACKAGES += android.hardware.camera.provider@2.4-external-service
PRODUCT_COPY_FILES += \
    $(VCAMES_PATH)/aosp/vintf/manifest_vcames_camera_provider_hidl_2_4.xml:$(TARGET_COPY_OUT_VENDOR)/etc/vintf/manifest/manifest_vcames_camera_provider.xml

PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.camera.external.hal3TrebleMinorVersion=6
