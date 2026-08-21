# AOSP / 定制 ROM 集成

## 源码与产品

```bash
git clone --recurse-submodules https://github.com/GUSHU101/vcames \
  vendor/gushu101/vcames
```

将 `aosp/product/vcames.mk` 加入产品继承，将 `aosp/BoardConfigVcames.mk` 加入目标设备
`BoardConfig.mk`。当前通用 external 示例使用分支匹配的 HIDL 2.4 service；若目标 API 33
产品树使用 AIDL external provider，应同时替换 package、VINTF fragment、init 和 SELinux，
不能混用实例声明。

## 内核和启动

在目标 Google/Xiaomi/Samsung 的同一 kernel build 中接入 `third_party/v4l2loopback`，并将
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
m VCamES vcamesd android.hardware.camera.provider@2.4-external-service
m bootimage vendorimage systemextimage vendor_dlkmimage
```

运行 `.\tools\adb\verify-vcames.ps1` 检查厂商范围、API 30–33、daemon domain、V4L2、
external provider 和 AVC denial，再用 Camera2 内容测试确认画面。

若需要替换原前后 ID，应在该产品的实际 HIDL/AIDL Camera provider 中实现 adapter/proxy，
遵守 [adapter v2 协议](FRONT_BACK_REPLACEMENT.md)，并按 [真机门槛](VALIDATION_PLAN.md) 验收。
