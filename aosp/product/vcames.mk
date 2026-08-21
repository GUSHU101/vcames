# VCamES system-image common fragment for Android 11-13 (API 30-33).
# Inherit exactly one vcames_provider_*.mk fragment for external/0.
VCAMES_PATH := $(call my-dir)/../..

PRODUCT_PACKAGES += \
    VCamES \
    vcamesd

PRODUCT_COPY_FILES += \
    $(VCAMES_PATH)/aosp/config/external_camera_config.xml:$(TARGET_COPY_OUT_VENDOR)/etc/external_camera_config.xml \
    frameworks/native/data/etc/android.hardware.camera.external.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.external.xml
