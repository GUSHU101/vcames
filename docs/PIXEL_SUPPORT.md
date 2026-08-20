# Pixel 4–6 支持矩阵

VCamES 的应用层统一要求 API 33–35；差异集中在 ROM 和内核。

| 设备 | 代号 | 常见内核 | Android 13 | Android 14 | Android 15 |
|---|---|---:|---|---|---|
| Pixel 4 / 4 XL | `flame` / `coral` | 4.14 | 原厂/AOSP | 自定义 ROM | 自定义 ROM |
| Pixel 4a | `sunfish` | 4.14 | 原厂/AOSP | 自定义 ROM | 自定义 ROM |
| Pixel 4a 5G | `bramble` | 4.19 | 原厂/AOSP | 原厂/AOSP | 自定义 ROM |
| Pixel 5 | `redfin` | 4.19 | 原厂/AOSP | 原厂/AOSP | 自定义 ROM |
| Pixel 5a | `barbet` | 4.19 | 原厂/AOSP | 原厂/AOSP | 自定义 ROM |
| Pixel 6 / 6 Pro | `oriole` / `raven` | 5.10 GKI | 原厂/AOSP | 原厂/AOSP | 原厂/AOSP |
| Pixel 6a | `bluejay` | 5.10 GKI | 原厂/AOSP | 原厂/AOSP | 原厂/AOSP |

“原厂/AOSP”表示 Google 为该组合发布过官方系统基础，但 VCamES 仍然要求重新构建并刷入
修改后的系统和内核；不能在锁定、未修改的原厂镜像上仅安装 APK。

## Pixel 4/5（4.14/4.19）

这两代通常使用设备专用、非 GKI 内核。将 `third_party/v4l2loopback` 作为外部模块加入
对应 kernel manifest/build，使用相同 clang、defconfig 和内核源码构建。模块必须与最终
boot/vendor 镜像中的内核完全匹配。将 `kernel/vcames_defconfig.fragment` 合并进 defconfig，
并把 `.ko`、modules.load 和 `kernel/modules.options.vcames` 安装进 vendor 模块目录。
选择内核树内构建时，用 `kernel/Kconfig.vcames` 和 `kernel/Makefile.vcames` 接入配置，
并把 submodule 内的驱动源/头文件放在同一 media 驱动目录。

## Pixel 6（5.10 GKI）

优先使用该 ROM 自己的 Pixel 6 kernel/build manifest，把 v4l2loopback 纳入
`vendor_dlkm` 模块构建和签名流程。不要把其他系统构建出的 `.ko` 直接复制过来：GKI
KMI、符号白名单、模块签名和内核 release 任一不匹配都会导致 `insmod` 失败。

如果外部模块引用的符号不在目标 KMI 白名单中，必须在该 ROM 的内核构建中更新允许列表，
或者把驱动集成为同一内核构建的一部分；不要关闭模块签名验证来绕过错误。

## 固定约定

- loopback 设备号：100；相机 ID 偏移：100，因此 CameraService 通常显示外置 ID `200`。
- 模块参数：`video_nr=100 card_label=VCamES exclusive_caps=0 max_buffers=4`。守护进程自身
  按目标 FPS 重复最后一帧，因此无需依赖 `sustain_framerate` 运行时 V4L2 控件。
- 把 `/dev/video100 0660 system camera` 合并到设备的 `vendor/ueventd.rc`。
- 模块必须在 `class core` 前载入；可按设备 init 结构改写
  `aosp/init/init.vcames-kernel.rc.example`。
