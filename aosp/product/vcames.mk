# VCamES system image product fragment for Android 11-15.
VCAMES_PATH := $(call my-dir)/../..

PRODUCT_PACKAGES += \
    VCamES \
    vcamesd \
    android.hardware.camera.provider@2.4-external-service

PRODUCT_COPY_FILES += \
    $(VCAMES_PATH)/aosp/config/external_camera_config.xml:$(TARGET_COPY_OUT_VENDOR)/etc/external_camera_config.xml \
    $(VCAMES_PATH)/aosp/vintf/manifest_vcames_camera_provider.xml:$(TARGET_COPY_OUT_VENDOR)/etc/vintf/manifest/manifest_vcames_camera_provider.xml \
    frameworks/native/data/etc/android.hardware.camera.external.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.external.xml

# HAL 3.6 is implemented by the branch-matching HIDL external provider.
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.camera.external.hal3TrebleMinorVersion=6
