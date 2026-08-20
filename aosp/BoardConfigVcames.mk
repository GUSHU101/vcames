# Include this file from the device's BoardConfig.mk after VCAMES_PATH points
# at the repository root, for example:
#   VCAMES_PATH := vendor/gushu101/vcames
#   include $(VCAMES_PATH)/aosp/BoardConfigVcames.mk

SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += $(VCAMES_PATH)/aosp/sepolicy/private
