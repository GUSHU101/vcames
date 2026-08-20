# 已 Root 原厂系统部署

## 支持定义

Root Bridge 支持仍保留 Google system/vendor 分区内容、通过 KernelSU 或 Magisk 获得 Root
的 Pixel 4–6 Android 11–15。它是“条件式支持”，不是承诺任意 Root 原厂内核都能加载
第三方模块或适配任意 OTA。

完整链路需要同时满足：

1. external 模式：`/dev/video100` 与 `camera.provider ... external/0` 同时存在；
2. replacement 模式：精确 OTA adapter 通过完整哈希校验并接收 memfd FrameBus；
3. Root 控制 APK 以普通应用 UID 安装，并由 KernelSU/Magisk 明确授权；
4. SELinux 保持 Enforcing，daemon 以 `SO_PEERCRED` 再校验单一控制 App UID；
5. replacement 必须通过内容级 Camera2 自检才可标为 `VERIFIED`。

Root 不能在用户空间绕过内核的 vermagic、`CONFIG_MODVERSIONS`、GKI KMI 或模块签名强制。
它也不能保证在 Magisk late_start 时才出现的 VINTF fragment 会被已经启动的
hwservicemanager 重新读取。

## 1. 预检

连接 ADB，并在 KernelSU/Magisk 中允许 shell Root 请求：

```powershell
.\tools\adb\check-root-stock.ps1
```

结果含义：

| 状态 | 含义 | 处理 |
|---|---|---|
| `READY_EXTERNAL` | loopback 与 external/0 已存在 | 可用 external 模式 |
| `READY_REPLACEMENT_UNVERIFIED` | 精确 adapter 与 FrameBus 已启动 | 先做真机内容自检 |
| `READY_EXTERNAL_AND_REPLACEMENT_UNVERIFIED` | 两条基础链路均启动 | replacement 仍需自检 |
| `NEEDS_V4L2LOOPBACK` | 没有 `/dev/video100` | 提供精确匹配 `.ko` 或 custom boot kernel |
| `NEEDS_COMPATIBLE_KERNEL_MODULE` | `insmod` 被拒绝 | 核对 dmesg 中签名、KMI、vermagic、未知符号错误 |
| `NEEDS_EXTERNAL_CAMERA_PROVIDER` | external/0 未注册 | 提供同 API/vendor Provider，并解决早期 VINTF 声明 |
| `NEEDS_CONTROLLER_APK` | 5 分钟内未找到普通 UID 控制端 | 手动安装 ZIP 旁的 Root controller APK 后重启 |
| `DAEMON_START_FAILED` | Root daemon 未启动 | 查看 `/data/adb/vcames/root-service.log` |
| `REPLACEMENT_ADAPTER_START_FAILED` | 精确构建适配器未启动 | 禁用模块并检查适配器日志/ABI |
| `SAFE_MODE_REPLACEMENT_DISABLED` | replacement 连续失败三次 | 修复适配器后手动清除安全模式标记 |
| `READY_EXTERNAL_SAFE_MODE_REPLACEMENT_DISABLED` | external 仍可用，replacement 已进安全模式 | 保留救援链路并修复 adapter |
| `READY_EXTERNAL_REPLACEMENT_ADAPTER_CRASHED` | external 仍可用，adapter 本次崩溃 | 查看日志并修复，重复失败会进安全模式 |

## 2. 准备 payload

### v4l2loopback

模块必须来自目标设备正在运行的同一 kernel build。Pixel 6 GKI 设备优先使用 custom kernel
已集成驱动，或把模块纳入该 kernel 的 vendor_dlkm/KMI/签名流程。不要用 `--force`、修改
内核内存或关闭签名检查规避错误。

如果 custom kernel 已经提供 `/dev/video100`，打包时不传 `-KernelModule`。

### External Camera Provider

Provider 必须按设备 Android 分支构建：API 30–35 分别使用相应 Android 11–15
源码和 vendor 接口。HIDL 二进制通常为：

```text
android.hardware.camera.provider@2.4-external-service
```

若预检已经看到 `external/0`，不需要打包 Provider。否则可把匹配二进制传给构建器；模块
会在 daemon 固定 MJPEG format 后尝试启动。若仍显示 `NEEDS_EXTERNAL_CAMERA_PROVIDER`，
说明 stock VINTF 已在 Magisk 挂载前缓存，需要 custom boot 的 early-init/VINTF 方案。

## 3. 构建

要求 Android NDK、CMake、Ninja、JDK 17 和 Android SDK：

```powershell
$env:ANDROID_NDK_HOME = 'C:\Android\Sdk\ndk\<version>'
.\tools\root\build-root-module.ps1 -Api 35 `
  -KernelModule C:\build\v4l2loopback.ko `
  -ProviderBinary C:\aosp\out\external-provider
```

Linux/macOS：

```bash
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/<version>"
VCAMES_KERNEL_MODULE=/path/v4l2loopback.ko \
VCAMES_PROVIDER_BINARY=/path/external-provider \
  ./tools/root/build-root-module.sh 35
```

输出位于 `out/root/`：KernelSU/Magisk ZIP 和可手动安装的 Root controller APK。正式分发时应给
Root APK 使用稳定的自有签名；CI debug APK 的签名不保证跨构建一致。

构建器还输出 `VCamES-Root-standalone.apk`。它内置不含 controller 的同一 Bridge；安装 APK
后点击“授权 ROOT 并部署”，应用会检测 root provider，分别执行 `ksud module install` 或
`magisk --install-module` 并提示重启。没有匹配 `.ko`/Provider/adapter 的普通 Gradle APK
只负责授权和诊断。KernelSU v3 的 `system/vendor` overlay 需要兼容 metamodule。

前后摄像头替换需先生成精确兼容清单并传入适配器：

```powershell
.\tools\adb\capture-camera-compatibility.ps1 `
  -AdapterPath C:\build\vcames-camera-adapter
.\tools\root\build-root-module.ps1 -Api 34 `
  -ReplacementAdapter C:\build\vcames-camera-adapter `
  -CompatibilityManifest .\out\camera-compatibility.properties
```

完整协议见 [前后摄像头替换](FRONT_BACK_REPLACEMENT.md)。

## 4. 安装与恢复

可以直接安装 standalone APK 并在应用内授权部署，也可以在 KernelSU/Magisk 中安装 ZIP。安装脚本
不会卸载签名冲突的旧版本。重启后在模块页面执行 Action，确认对应 `READY_*` 状态，再打开
VCamES Root。

出现启动问题时，可从可用的 Root ADB/救援环境禁用模块：

```bash
touch /data/adb/modules/vcames_root_bridge/disable
```

然后重启。模块不会删除控制 APK 数据；恢复后由用户自行卸载 APK。

replacement adapter 连续三次启动/运行失败时，BootGuard 创建
`/data/adb/vcames/disable-replacement`，后续启动保留 external 能力但不再拉起 adapter。
确认精确 adapter 已修复后，按 Action 页面提示清除该标记；不要在故障二进制未更换时反复重试。

## 安全边界

- 不调用 `setenforce 0`，不加载 Xposed/Zygisk 库，不 hook Camera 私有符号。
- daemon 虽由 Root 启动，但只接受 UID 0、1000 和安装时解析出的一个控制 App UID。
- HTTP MJPEG 是明文，仅用于可信局域网；不在 URL 中放账号密码。
- 可选 Provider 在 Magisk domain 中运行属于兼容路径，不如 ROM 原生 `hal_camera_default`
  集成稳定；生产/长期使用仍推荐 AOSP 原生方案。

参考：[KernelSU 模块指南](https://kernelsu.org/guide/module.html)、
[KernelSU metamodule](https://kernelsu.org/guide/metamodule.html)、
[Magisk 模块与启动脚本](https://topjohnwu.github.io/Magisk/guides.html)、
[AOSP 可加载内核模块](https://source.android.com/docs/core/architecture/kernel/loadable-kernel-modules)、
[AOSP GKI 模块](https://source.android.com/docs/core/architecture/kernel/modules)。
