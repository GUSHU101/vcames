# AOSP / 定制 ROM 集成

## 源码与产品

```bash
git clone --recurse-submodules https://github.com/GUSHU101/vcames \
  vendor/gushu101/vcames
```

将 `aosp/BoardConfigVcames.mk` 加入目标设备 `BoardConfig.mk`，并只继承下面一个产品片段；
三个片段都会继承公共 `vcames.mk`，不能重复继承：

```make
# Android 11/12 HIDL 2.4
$(call inherit-product, vendor/gushu101/vcames/aosp/product/vcames_provider_hidl_2_4.mk)

# 或目标树明确提供 HIDL 2.7 external service
$(call inherit-product, vendor/gushu101/vcames/aosp/product/vcames_provider_hidl_2_7.mk)

# 或 Android 13 AIDL v1
$(call inherit-product, vendor/gushu101/vcames/aosp/product/vcames_provider_aidl_1.mk)
```

选择必须来自目标 VINTF、已存在的 provider package 和 service 注册结果，而不是只根据
Android 版本推断。片段分别复制互斥的 HIDL 2.4、HIDL 2.7 或 AIDL v1 manifest。

## 内核和启动

在目标 Google/Xiaomi 的同一 kernel build 中接入 `third_party/v4l2loopback`，并将
模块加入该树实际使用的 vendor ramdisk/vendor_dlkm 构建与签名流程。不要复制其他设备 `.ko`。

```bash
adb shell 'zcat /proc/config.gz | grep -E "VIDEO_DEV|V4L2LOOPBACK"'
adb shell 'ls -lZ /dev/video100'
```

模块需在 camera provider 枚举前创建 `/dev/video100`。将示例 ueventd/modules.options 合并
到目标设备已有配置，保持 SELinux Enforcing，按该设备公开 type 最小授权。

## 构建和验收

```bash
source build/envsetup.sh
lunch <target_product>-userdebug
m VCamES libvcames_yuv vcamesd <所选 external-provider package>
m bootimage vendorimage systemextimage vendor_dlkmimage
```

运行 `.\tools\adb\verify-vcames.ps1` 检查厂商范围、API 30–33、daemon domain、V4L2、
external provider 和 AVC denial，再用 Camera2 内容测试确认画面。

若需要替换原前后 ID，应在该产品的实际 HIDL/AIDL Camera provider 中实现 adapter/proxy，
遵守 [adapter v2 协议](FRONT_BACK_REPLACEMENT.md)，并按 [真机门槛](VALIDATION_PLAN.md) 验收。
