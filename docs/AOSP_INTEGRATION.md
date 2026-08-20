# AOSP / LineageOS 集成

## 1. 放入源码树

```bash
git clone --recurse-submodules https://github.com/GUSHU101/vcames \
  vendor/gushu101/vcames
```

将 `aosp/product/vcames.mk` 加入产品继承，将 `aosp/BoardConfigVcames.mk` 加入设备
`BoardConfig.mk`。产品 fragment 会安装应用、守护进程、Android 13–15 的 HIDL 2.4
External Camera Provider、feature XML、VINTF fragment 和 Provider 配置。

## 2. 内核

按 [Pixel 支持矩阵](PIXEL_SUPPORT.md) 集成 `third_party/v4l2loopback`。构建后确认：

```bash
adb shell 'zcat /proc/config.gz | grep -E "VIDEO_DEV|V4L2LOOPBACK"'
adb shell 'cat /proc/modules | grep v4l2loopback'
adb shell 'ls -lZ /dev/video100'
```

把 `kernel/modules.options.vcames` 合并到设备实际读取的 modules.options。不同 Pixel
device tree 的模块变量名称并不统一，应使用该树已有的 `BOARD_VENDOR_KERNEL_MODULES`、
`BOARD_VENDOR_RAMDISK_KERNEL_MODULES` 或 vendor_dlkm 清单，而不是硬编码复制路径。

## 3. 启动与 ueventd

确保模块在 `class core` 之前加载。将
`aosp/init/ueventd.vcames.rc.example` 的规则合并到设备实际使用的 vendor ueventd 文件。
不要直接把 `.example` 文件加入 `PRODUCT_PACKAGES`；先按目标 ROM 的 modprobe 路径和
SELinux domain 调整。

## 4. SELinux

`aosp/BoardConfigVcames.mk` 添加 system_ext private policy。保持 enforcing；以下检查不应
产生 denial：

```bash
adb shell getenforce
adb shell ps -AZ | grep vcamesd
adb shell logcat -b all -d | grep 'avc: denied' | grep -E 'vcames|video100'
```

不要采用 `setenforce 0`。如果设备树为 camera/video 节点定义了更具体 type，应把
`vcamesd.te` 的 `video_device` 换成该公开 type，并保持最小权限。

## 5. 构建和刷写

```bash
source build/envsetup.sh
lunch <product>-userdebug
m VCamES vcamesd android.hardware.camera.provider@2.4-external-service
m bootimage vendorimage systemextimage vendor_dlkmimage
```

分区名取决于 ROM。先备份数据和原 boot 镜像，使用该 ROM 官方刷写流程，不要跨设备刷
boot/vendor_boot/vendor_dlkm。

## 6. 验收

启动系统后运行：

```powershell
.\tools\adb\verify-vcames.ps1
```

通过标准包括：API 33–35、SELinux enforcing、`vcamesd` domain 正确、video100 同时具备
V4L2 output/capture 能力、`external/0` Provider 注册、CameraService 列出外置相机，
且日志没有相关 SELinux denial。

## Android 15 说明

Android 13 起 Camera HAL 推荐 AIDL，但 Android 13–15 仍保留 HIDL Provider 支持；本项目
为了一个产品 fragment 覆盖三代系统，使用 AOSP 的
`android.hardware.camera.provider@2.4-external-service`。如果目标 ROM 已完全迁移到 AIDL
External Provider，可在产品 fragment 中替换为该 ROM 的
`android.hardware.camera.provider-V1-external-service`，同时改用对应 AIDL VINTF 声明。
