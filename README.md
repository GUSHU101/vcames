# VCamES

VCamES 2.3 是面向 Google、Xiaomi/Redmi/POCO 的 Android 11–13
（API 30–33）arm64 系统级虚拟相机工程。它支持定制 ROM 原生集成和已 Root 原厂系统，
不使用 Xposed/Zygisk，也不把代码注入目标应用。

项目把两类能力严格分开：

- `external`：`vcamesd → /dev/video100 → AOSP External Camera Provider`，增加外置相机 ID；
- `front/back/both`：保留 OEM 原 camera ID、facing 和 metadata，仅由当前设备/OTA 专用的
  HIDL/AIDL Camera adapter 替换非安全输出 buffer。

普通 APK、ROOT 权限或 v4l2loopback 本身都不能把 external ID 变成原前后摄像头。
仓库提供安全的数据平面、安装/哈希门禁和 adapter 协议，但不包含跨厂商通用的
`cameraserver` 私有 ABI 注入器。没有精确 adapter 时，前后替换会失败关闭。

## 已实现

- 单 APK 请求 KernelSU/Magisk ROOT、部署 Bridge；daemon 降到 system UID，普通应用先经
  独立 `vcames_proxy` SELinux 域与 UID 门禁，再访问 ROOT 私有端点；
- 本地 MP4 使用 MediaCodec 解码 YUV_420_888；stride/crop 转换移入 C++，复用 NV21 缓冲区，
  通过 `VCF2` 本地协议推送；
- 私网 HTTP MJPEG 具备解析上限、latest-frame、断线指数退避和超时保护，并按解析后的
  IPv4/IPv6 地址拒绝公网明文连接；
- FrameBus v2：4 槽 memfd、sequence lock、PTS/arrival、stride/format/rotation 元数据；
  adapter 仅获得只读、定长密封 FD，并可复用规范化只读 reader；
- replacement 数据平面首选 NV21；只有 external/V4L2 路径需要 JPEG 编码；
- `external/front/back/both`、旋转、镜像、中心裁切、FPS、hold/blank；
- 设备画像：厂商、SoC、Camera HIDL/AIDL 实测、Camera2 ID/capability/尺寸/FPS；
- 精确绑定 system/vendor fingerprint、cameraserver、Provider、vendor camera 库、
  mapper/allocator 和 adapter SHA-256；OTA 后自动拒绝旧适配器；
- daemon 每 2 秒检查 adapter 健康并重新附着 FrameBus；模块进程监控采用退避重启，
  BootGuard 连续失败三次禁用 replacement，并保留 external 救援链路；
- External Provider 构建显式区分 HIDL 2.4、HIDL 2.7 和 AIDL v1，不再向通用包塞入固定声明；
- API 30 与 API 33 arm64 ROOT 构建、system/root APK、Lint、原生测试和 CI。
- 单一 `version.properties` 同步 APK、Root 模块与 daemon/FrameBus/Profile 协议契约；
- Profile v1、Ed25519 离线签名工具与 fail-closed 目录门禁；当前目录为空，不宣称任何 OTA 已 VERIFIED；
- UI 输出 READY_UNVERIFIED/LIMITED/SAFE_MODE 分层状态，并导出不含用户媒体的 `diagnostics.zip`。

## 数据路径

```text
本地 MP4 ──MediaCodec/YUV──┐
                           ├─ vcamesd latest-frame ─┬─ JPEG → V4L2 → external ID
私网 MJPEG ──有界解码──────┘                       └─ NV21 FrameBus v2（只读 FD）
                                                         │
                                    精确 OTA Camera adapter（可选）
                                                         │
                                       原 front/back ID + OEM metadata
```

## 快速构建

```powershell
$env:JAVA_HOME = 'C:\Program Files\Android\openjdk\jdk-21.0.8'
$env:ANDROID_HOME = 'C:\Program Files (x86)\Android\android-sdk'
.\gradlew.bat :app:assembleSystemDebug :app:assembleRootDebug `
  :app:lintSystemDebug :app:lintRootDebug

.\tools\root\build-root-module.ps1 -Api 33 `
  -NdkPath C:\Android\Sdk\ndk\27.2.12479018
```

不传 payload 会生成可安装的一体化 Root APK 和 Bridge ZIP，但只具备 daemon/控制层；
external 仍要求匹配内核的 v4l2loopback 与可注册的 Provider，前后替换仍要求精确 adapter。

## 已 Root 原厂系统工作流

1. 运行 `.\tools\adb\check-root-stock.ps1`，确认厂商、API、ROOT、SELinux 和现有相机链路。
2. external 模式准备与当前 `uname -r`/KMI/签名完全匹配的 `.ko` 和相应 Provider。
3. front/back/both 模式先针对目标 system/vendor 构建 adapter，再运行：

```powershell
.\tools\adb\capture-camera-compatibility.ps1 `
  -AdapterPath C:\device-build\vcames-camera-adapter

.\tools\root\build-root-module.ps1 -Api 33 `
  -ReplacementAdapter C:\device-build\vcames-camera-adapter `
  -CompatibilityManifest .\out\camera-compatibility.properties
```

若同时打包 external Provider，必须再传
`-ProviderBinary <文件> -ExternalProviderTransport hidl-2.4|hidl-2.7|aidl-1`；没有明确 transport
时构建会拒绝继续。

4. 安装 `out/root/VCamES-Root-standalone.apk`，点击“授权 ROOT 并部署”，重启。
5. 通过 Camera2 内容测试与压力测试后，才能把单一组合标记为 `VERIFIED`。

详见 [产品支持范围](docs/PRODUCT_SUPPORT.md)、[Profile v1](docs/PROFILE_SCHEMA.md)、
[发布门禁](docs/RELEASE_GATES.md)、[厂商/SoC 支持策略](docs/VENDOR_SUPPORT.md)、[原厂 Root 部署](docs/ROOT_STOCK.md)、
[前后摄像头替换](docs/FRONT_BACK_REPLACEMENT.md)、[设备画像](docs/DEVICE_PROFILING.md) 和
[真机验收门槛](docs/VALIDATION_PLAN.md)。

## AOSP/定制 ROM

```make
# 三选一；每个分支会继承公共 vcames.mk
$(call inherit-product, vendor/gushu101/vcames/aosp/product/vcames_provider_aidl_1.mk)
VCAMES_PATH := vendor/gushu101/vcames
include $(VCAMES_PATH)/aosp/BoardConfigVcames.mk
```

产品树必须按 Android 11/12/12L/13 分支和目标厂商实际 Camera transport 选择 HIDL/AIDL
Provider，并在同一内核构建中接入 v4l2loopback。详见 [AOSP 集成](docs/AOSP_INTEGRATION.md)。

## 视频源与边界

- 手机本地视频：SAF 选择后循环播放；不会上传媒体。
- Windows/OBS：使用 [start-mjpeg.ps1](tools/windows/start-mjpeg.ps1)，或 VCamLab-2.0
  输出的 MJPEG URL；当前不解码 HLS。
- HTTP MJPEG 是明文，仅允许解析到回环、RFC1918/ULA、链路本地或 CGNAT 地址；公网源会拒绝。
- secure/protected、RAW、depth 输出必须由 adapter 拒绝并回退 OEM 相机。
- 不提供身份验证绕过、活体检测规避、反检测或静默录制功能。

## 参考与许可

架构和 Windows MJPEG 兼容性参考
[VCamLab-2.0](https://github.com/GUSHU101/VCamLab-2.0)。用户提供 APK 仅做离线静态分析，
没有执行、上传、复制其私有二进制或 hook 实现，见 [参考 APK 分析](docs/REFERENCE_APK.md)。
新增 PDF 的工程建议落实/延期边界见 [PDF 参考审阅](docs/REFERENCE_PDF_REVIEW.md) 和
[深度审计整改记录](docs/DEEP_AUDIT_REMEDIATION.md)。本轮产品化整改见
[Google/Xiaomi 产品化指南整改记录](docs/PRODUCTIZATION_GUIDE_REMEDIATION.md)，资源边界见
[资源与许可边界](docs/LICENSED_RESOURCES.md)。

自有代码采用 Apache-2.0；第三方依赖按各自许可证发布，见 [NOTICE](NOTICE)。
