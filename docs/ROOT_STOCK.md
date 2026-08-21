# 已 Root 原厂系统部署

## 支持定义

Root Bridge 的运行候选范围为 Google、Xiaomi/Redmi/POCO、Samsung，Android 11–13
（API 30–33），`arm64-v8a`，KernelSU 或 Magisk。它不承诺任意型号/OTA 已经验证。

ROOT 只提供部署权限，不能绕过内核 vermagic、KMI、`CONFIG_MODVERSIONS`、模块签名、
vendor Camera ABI 或启动阶段已缓存的 VINTF。
Root service 完成设备节点与监听准备后，daemon 必须成功降到 system UID + camera/inet groups，
否则以 `DAEMON_START_FAILED` 退出。

## 预检

```powershell
.\tools\adb\check-root-stock.ps1
```

必须保持 SELinux Enforcing。脚本会显示厂商、SoC、API、ROOT 管理器、camera provider 服务、
`/dev/video100` 和当前 Bridge 状态。

## 两种 payload

external 模式要求：

1. 与当前内核构建完全匹配的 v4l2loopback，或已经集成该驱动的可信 custom boot；
2. 当前 vendor/VINTF 能注册的 External Camera Provider。

replacement 模式不依赖 `/dev/video100`，但必须提供精确 OTA adapter 和兼容性清单。
不传 adapter 的“一体化 Root APK”不能替换 OEM 前后相机。

Google、Xiaomi、Samsung 的内核模块目录、签名链、Provider 名称和启动时序各不相同；
应以目标 ROM 的 kernel manifest、VINTF、service list 与 vendor ELF 为准，不能跨机复制 `.ko`
或 adapter。KernelSU system/vendor overlay 还可能要求设备的 metamodule 支持。

## 构建和安装

```powershell
.\tools\root\build-root-module.ps1 -Api 30 `
  -NdkPath C:\Android\Sdk\ndk\27.2.12479018

.\tools\root\build-root-module.ps1 -Api 33 `
  -NdkPath C:\Android\Sdk\ndk\27.2.12479018 `
  -KernelModule C:\device-build\v4l2loopback.ko `
  -ProviderBinary C:\device-build\external-camera-provider `
  -ReplacementAdapter C:\device-build\vcames-camera-adapter `
  -CompatibilityManifest .\out\camera-compatibility.properties
```

输出包括 Bridge ZIP、controller APK 和 standalone Root APK。安装 standalone APK 后点击
“授权 ROOT 并部署”，在 KernelSU/Magisk 确认授权，然后重启。应用仅调用 Root 管理器的
`su`/模块安装入口，不使用 Xposed/Zygisk。

## 状态与恢复

| 状态 | 说明 |
|---|---|
| `READY_EXTERNAL` | external 基础链路存在 |
| `ADAPTER_AVAILABLE_UNVERIFIED` | 精确 adapter 进程可用，尚未证明 FrameBus/Camera pipeline 已附着 |
| daemon `replacement_attached=true` | 本次启动已完成 protocol v2/FD 握手，仍需内容自检 |
| `NEEDS_V4L2LOOPBACK_OR_ADAPTER` | 两条系统路径均无可用 payload |
| `NEEDS_EXTERNAL_CAMERA_PROVIDER` | V4L2 节点存在但 Provider 未注册 |
| `REPLACEMENT_ADAPTER_START_FAILED` | adapter 启动或协议握手失败 |
| `SAFE_MODE_REPLACEMENT_DISABLED` | 连续失败触发 BootGuard |

如果设备相机或启动异常，先在 Root 管理器禁用/删除 `vcames` 模块并重启；必要时使用其安全
模式或 recovery 删除 `/data/adb/modules/vcames`。保留原 boot/vendor_boot/vendor_dlkm 和
可用的 fastboot/Odin/厂商恢复路径。
