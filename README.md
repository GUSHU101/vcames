# VCamES

VCamES 是面向 Pixel 4–Pixel 6、Android 13–15 的系统级虚拟相机，支持定制 ROM
原生集成，以及满足内核/Provider 前提的已 Root 原厂系统。
它不使用 Xposed，也不向目标应用注入代码。通用模式由系统守护进程写入
`/dev/video100`，再由 AOSP External Camera Provider 通过 Camera2/CameraX 暴露；前置、
后置或双摄替换则要求与单一 Pixel OTA 指纹完全匹配的 Camera HAL 适配器。

> standalone Root APK 可以申请 Magisk 授权并安装内置 Bridge，但它不能凭空生成与当前
> 内核/Camera HAL 匹配的二进制。构建 APK 时仍须提供目标原厂构建所需的 payload。

## 已实现

- Android 13–15 控制应用：本地视频循环、HTTP MJPEG、分辨率、FPS、旋转、镜像、
  中心裁切、断流保持、开机恢复和运行状态。
- `vcamesd` 原生守护进程：低延迟 latest-frame 队列、丢弃积压帧、MJPEG 重连、
  JPEG 安全限制、V4L2 输出和仅 UID 0/1000 可用的控制/推帧协议。
- AOSP 产品包：平台签名 `system_ext` 应用、HIDL External Camera Provider、VINTF、
  `external_camera_config.xml` 与 SELinux 策略。
- Pixel 4/4a/5/5a（4.14/4.19）和 Pixel 6/6 Pro/6a（5.10 GKI）的内核接入说明。
- Windows FFmpeg MJPEG 发送脚本、ADB 设备验收脚本、Gradle/CMake 测试和 CI。
- Root Bridge：普通签名 APK、受 UID 限制的 Root 守护进程、Magisk 模块模板、
  应用内 ROOT 授权/模块安装、standalone APK、v4l2loopback/Provider 可选 payload。
- `external/front/back/both` 目标选择；前后替换使用独立适配器 socket，并在安装时校验
  设备、API、完整 fingerprint 和 cameraserver 的 SHA-256。

## 数据路径

```text
本地 MP4 / Windows OBS / HTTP MJPEG
                 │
          VCamES App / vcamesd
                 │ JPEG
          /dev/video100 (V4L2)
                 │
       ┌─────────┴─────────┐
 AOSP External Provider   精确构建 Camera HAL adapter
       │ external ID       │ 原 front/back ID
       └─────────┬─────────┘
        CameraService → Camera2/CameraX 应用
```

## 快速构建

普通 Gradle 构建会生成 system/root 两个控制端，但 APK 自身不会创造内核相机设备：

```powershell
$env:JAVA_HOME = 'C:\Program Files\Android\openjdk\jdk-21.0.8'
$env:ANDROID_HOME = 'C:\Program Files (x86)\Android\android-sdk'
.\gradlew.bat :app:assembleSystemDebug :app:assembleRootDebug `
  :app:lintSystemDebug :app:lintRootDebug
```

AOSP 集成的入口如下：

```make
# device/<vendor>/<device>/device.mk
$(call inherit-product, vendor/gushu101/vcames/aosp/product/vcames.mk)

# device/<vendor>/<device>/BoardConfig.mk
VCAMES_PATH := vendor/gushu101/vcames
include $(VCAMES_PATH)/aosp/BoardConfigVcames.mk
```

然后完成匹配设备的内核模块接入并构建系统：

```bash
source build/envsetup.sh
lunch <your_pixel_product>-userdebug
m VCamES vcamesd android.hardware.camera.provider@2.4-external-service
```

详细步骤见 [AOSP 集成](docs/AOSP_INTEGRATION.md) 和
[Pixel 支持矩阵](docs/PIXEL_SUPPORT.md)。

## 已 Root 原厂系统

先运行只读兼容性检查：

```powershell
.\tools\adb\check-root-stock.ps1
```

只有 `/dev/video100` 和 `camera.provider ... external/0` 都可用时，Magisk-only 部署才是
直接可用状态。否则需为当前 `uname -r` 提供完全匹配的 v4l2loopback，或提供同 Android
版本的 External Provider/早期 VINTF 接入。打包入口：

```powershell
.\tools\root\build-root-module.ps1 -Api 35 `
  -KernelModule C:\pixel-kernel\v4l2loopback.ko `
  -ProviderBinary C:\aosp-out\android.hardware.camera.provider@2.4-external-service
```

构建器同时生成 `VCamES-Root-standalone.apk`。安装后点击“授权 ROOT 并部署”，应用会把内置
Bridge 交给 Magisk，完成后重启。前后摄像头模式还需传入 `-ReplacementAdapter` 和
`-CompatibilityManifest`，详见 [前后摄像头替换](docs/FRONT_BACK_REPLACEMENT.md)。

Root 方案保持 SELinux enforcing，不使用 Zygisk/Xposed。详细前提和失败状态见
[原厂 Root 部署](docs/ROOT_STOCK.md)。

## 视频源

- 手机本地视频：应用内选择文件后启动；使用 MediaCodec 解码并通过受限本地 socket 推帧。
- 局域网 MJPEG：填写 `http://电脑IP:8888/live.mjpg`。
- Windows/OBS：使用 [start-mjpeg.ps1](tools/windows/start-mjpeg.ps1)，或继续使用
  VCamLab-2.0 已有的 Windows 控制中心所输出的同一 MJPEG 地址。

本项目当前不解码 HLS。使用 VCamLab Windows 控制中心时请选择 MJPEG 模式。

## 重要边界

- 通用模式会把它作为一枚 **external camera** 暴露。遵循 Android API 的应用通常可见；
  主动拒绝外置相机、只接受固定前/后物理 ID，或带企业安全策略的应用可能不会使用它。
- UI 和 daemon 已支持前置/后置/双摄目标，但仓库不包含跨构建通用注入器。只有在打包了
  对当前 Pixel OTA 精确编译并签名验证的适配器时，这些模式才会启动。
- 不包含身份验证绕过、活体检测规避、反检测或静默录制功能。请只在获得授权的测试、
  演示、直播和隐私保护场景使用，并让参与者知晓视频源。
- 解锁 bootloader、刷写系统/内核会清除数据且存在无法启动风险。保留原厂 boot 镜像和
  可用的 fastboot 恢复路径。

## 来源与许可

架构和 Windows MJPEG 兼容性参考了同作者的
[VCamLab-2.0](https://github.com/GUSHU101/VCamLab-2.0)。参考 APK 仅做静态互操作分析；
没有复制其私有二进制、资源或注入实现，详见 [参考 APK 深度分析](docs/REFERENCE_APK.md)
和 [`项目分析.txt` 复核](docs/PROJECT_ANALYSIS_REVIEW.md)。

VCamES 自有代码采用 Apache-2.0。`stb` 和可选 `v4l2loopback` 子模块按各自许可证发布，
详见 [NOTICE](NOTICE)。
