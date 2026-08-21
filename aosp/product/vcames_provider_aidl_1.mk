# Explicit Android 13 AIDL v1 external-provider branch.
VCAMES_PATH := $(call my-dir)/../..
$(call inherit-product, $(VCAMES_PATH)/aosp/product/vcames.mk)

PRODUCT_PACKAGES += android.hardware.camera.provider-V1-external-service
PRODUCT_COPY_FILES += \
    $(VCAMES_PATH)/aosp/vintf/manifest_vcames_camera_provider_aidl_1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/vintf/manifest/manifest_vcames_camera_provider.xml
