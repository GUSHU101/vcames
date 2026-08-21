# VCamES 3.2

VCamES 是只面向 **Google Pixel 5（redfin）原厂 Android 11–14 / API 30–34** 的系统级虚拟摄像头项目。
手机由用户提前使用 KernelSU/Magisk 等方案取得 ROOT；控制器 APK 只请求并检测 `su` 是否返回 uid 0，
不会 ROOT 手机、安装 ROOT 管理器或把运行时模块写入设备。

## 系统级全局替换

3.2 不再使用应用 Hook、包名白名单或 CameraService Injection。独立运行时停止精确 Profile 记录的
原厂相机 Provider 服务，并在 CameraService 下方接管已经声明的 HIDL 实例
`android.hardware.camera.provider@2.4::ICameraProvider/legacy/0`：

```text
网络流 / 本地视频 → FFmpeg / MediaCodec → v4l2loopback(/dev/video100)
                                             ↓
                                  legacy/0 replacement Provider
                                      ├── camera 0 / BACK
                                      └── camera 1 / FRONT
                                             ↓
                                         CameraService
                                             ↓
                              Camera1 / Camera2 / CameraX 应用
```

前置和后置默认同时替换，不需要选择目标摄像头或应用。Provider 异常、注册失败、V4L2 丢失或守护进程
连续失败时，运行时退出接管并重新启动原厂 Provider。SELinux 必须始终保持 Enforcing。

Provider 注册前，daemon 会先把 `/dev/video100` 固定预热为 1280×720@30 并写入黑帧；收到流后统一缩放、
补边到这一稳定格式，断流时保留最后一帧。这样 CameraService 枚举期间就能得到稳定能力，避免因流分辨率
变化或首帧未到导致前后摄像头消失。

## 输入协议

网络输入由独立的 Android FFmpeg 可执行文件解复用/解码，支持：

- HTTP/HTTPS（包括 HLS、DASH、MJPEG）；
- RTMP/RTMPS/RTMPE/RTMPT/RTMPTE/RTMPTS；
- RTSP/RTSPS、SRT、RIST；
- RTP/SRTP、UDP/TCP MPEG-TS；
- MMSH/MMST。

`file`、`concat`、`data`、`subfile`、`unix` 等输入协议被显式禁用；URL 作为单独 argv 传给 FFmpeg，
不经过 shell。本地媒体只通过 Android Storage Access Framework 选择并由应用解码。

## 两个独立产物

1. `VCamES-3.2.0.apk`：普通控制器，仅检测设备/ROOT/运行时，选择媒体并显示诊断。
2. `VCamES-Pixel5-Runtime-APIxx.zip`：用户自行通过 ROOT 管理器安装的精确 OTA 运行时。

公开仓库不包含可跨 OTA 使用的 `.ko`、Provider 或 FFmpeg 二进制。`profiles/catalog.json` 在没有完成
Pixel 5 真机门禁前保持空；因此源码实现不是对任意 OTA 已验证可用的声明。

## 构建

控制器：

```bash
./gradlew :app:assembleDebug :app:lintDebug
```

在与目标原厂 OTA 完全一致的 AOSP/vendor 构建树中生成 Provider：

```bash
python3 tools/aosp/pixel5-global-provider/apply_global_provider.py /path/to/aosp
m vcames-global-camera-provider
```

打包精确运行时：

```powershell
.\tools\root\build-root-module.ps1 -Api 30 `
  -KernelModule C:\pixel5-build\v4l2loopback.ko `
  -ExternalCameraProvider C:\pixel5-build\vcames-global-camera-provider `
  -Ffmpeg C:\pixel5-build\ffmpeg `
  -FfmpegManifest C:\pixel5-build\ffmpeg.LICENSE.json `
  -FfmpegLicense C:\pixel5-build\COPYING.LGPLv2.1 `
  -Profile C:\pixel5-build\profile.json `
  -ProfileSignature C:\pixel5-build\profile.sig `
  -ProfilePublicKey C:\pixel5-build\release-public.pem
```

Profile v2 同时绑定 system/vendor 指纹、内核、cameraserver、原厂 Camera HAL 栈、`lshal` Server 对应的
OEM Provider 路径/哈希、替换 Provider、FFmpeg 和许可清单哈希。Android 11、12、13、14 必须分别构建并
验收，不能跨 OTA 复用。

## 自检

```bash
cmake -S daemon -B out/host && cmake --build out/host
ctest --test-dir out/host --output-on-failure
python3 -m unittest discover -s tools/root/tests -v
python3 -m unittest discover -s tools/compatibility-builder/tests -v
python3 -m unittest discover -s tools/aosp/tests -v
python3 tools/compatibility-builder/validate_profiles.py profiles/catalog.json
```

真机发布门槛见 [docs/RELEASE_GATES.md](docs/RELEASE_GATES.md)。
