# VCamES

VCamES 2.4 是面向已 Root 的 Google、Xiaomi/Redmi/POCO Android 11–13
（API 30–33）arm64 原厂系统的前/后摄像头替换工程。不使用 Xposed/Zygisk，不申请
system UID，不修改目标应用，也不会关闭 SELinux。

项目只保留一条产品链路：普通 APK 获得用户明确授予的 uid 0 权限后部署轻量 Root
Bridge；`vcamesd` 随即降权到 Android system UID，经受限 socket proxy 接收本地视频或
私网 MJPEG，再通过只读 FrameBus v2 把 NV21 帧交给当前设备和 OTA 专用的 Camera
adapter。adapter 必须与已签名、已验收的 Profile v1 的 `compatibility_id` 完全一致。
不匹配、断流或 adapter 故障时替换失效并回到 OEM 相机。

## 核心能力

- 前置、后置、前后双摄三个目标；保留 OEM camera ID、facing 和 metadata；
- 本地 MP4 循环播放与私网 HTTP MJPEG；明文网络源仅允许本机、局域网、链路本地、
  ULA 或 CGNAT 地址；
- FrameBus v2：4 槽 memfd、sequence lock、PTS/arrival、只读定长 FD；
- `SO_PEERCRED` 双重 UID 校验、SELinux enforcing、daemon 降权；
- `DeviceProbe` 生成完整设备/Camera2/ROOT 哈希事实，`ProfileResolver` 只做精确匹配，
  不按品牌或 SoC 猜测“候选兼容”；
- BootGuard 连续三次 adapter 故障进入安全模式；模块 Action 只显示状态、PID、最近错误
  和恢复命令；
- 单一 Android 应用、单一 Python 构建器、单一用户 APK。PowerShell/Bash 仅为薄包装。

主产品已移除 system/root 双 flavor、`android.uid.system`、ROOT 管理器品牌识别、
external/V4L2、手工设备节点、手工宽高/FPS、hold-last、stale/JPEG 质量参数和重复的
`compatibility.properties` 输入流程，并移除了不再参与产品构建的 AOSP/external 与
v4l2loopback 历史目录。

## 构建

```powershell
$env:JAVA_HOME = 'C:\Program Files\Android\openjdk\jdk-21.0.8'
$env:ANDROID_HOME = 'C:\Android\Sdk'
$env:ANDROID_NDK_HOME = 'C:\Android\Sdk\ndk\27.2.12479018'

.\gradlew.bat :app:assembleDebug :app:lintDebug
.\tools\root\build-root-module.ps1 -Api 30
```

统一构建器输出：

- `out/release/VCamES-2.4.0.apk`：唯一面向用户的 APK，内置 Root Bridge；
- `out/developer/VCamES-Root-API30.zip`：仅供开发和诊断的模块产物。

为一台已完成验收的设备打包 adapter：

```powershell
.\tools\root\build-root-module.ps1 -Api 33 `
  -ReplacementAdapter C:\device-build\vcames-camera-adapter `
  -Profile C:\device-build\profile.json `
  -ProfileSignature C:\device-build\profile.sig `
  -ProfilePublicKey C:\device-build\release-public.pem
```

构建器拒绝非 `VERIFIED` Profile、API 不一致、缺失签名、adapter 哈希不一致或不完整的
精确设备哈希。仓库当前 Profile catalog 为空，因此公开构建会诚实显示
`NEEDS_SIGNED_EXACT_DEVICE_PACK`，不会宣称任何手机/OTA 已完成替换验证。

## 使用边界

- APK 安装后点击“授权 ROOT 并部署”，重启，再配置本地视频或私网 MJPEG；
- Android 13 的通知权限只会在选择本地视频时按上下文请求，拒绝不会阻断核心配置；
- secure/protected、RAW、depth 输出必须由 adapter 拒绝并保持 OEM 路径；
- 不提供身份验证绕过、活体检测规避、反检测或静默录制能力；
- ROOT 只提供部署权限，不能自动制造跨设备通用 Camera HAL adapter。

详见 [精简架构](docs/ARCHITECTURE.md)、[Profile v1](docs/PROFILE_SCHEMA.md)、
[Root 原厂系统部署](docs/ROOT_STOCK.md)、[前后摄替换协议](docs/FRONT_BACK_REPLACEMENT.md)、
[发布门禁](docs/RELEASE_GATES.md) 和 [2.4 精简记录](docs/LEAN_REFACTOR_2_4.md)。

架构和 Windows MJPEG 兼容性参考
[VCamLab-2.0](https://github.com/GUSHU101/VCamLab-2.0)。项目自有代码采用 Apache-2.0；
第三方依赖许可证见 [NOTICE](NOTICE)。
