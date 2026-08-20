# 已 Root 原厂系统部署

## 支持定义

Root Bridge 支持仍保留 Google system/vendor 分区内容、通过 Magisk 获得 Root 的 Pixel
4–6 Android 13–15。它是“条件式支持”，不是承诺任意 Root 原厂内核都能加载第三方模块。

完整链路需要同时满足：

1. `/dev/video100` 来自与当前内核完全匹配的 v4l2loopback；
2. `android.hardware.camera.provider ... external/0` 已在 hwservicemanager 注册；
3. Root 控制 APK 以普通应用 UID 安装；
4. SELinux 保持 Enforcing，模块的最小规则允许该 App domain 连接 Magisk daemon；
5. `vcamesd` 写入 MJPEG 后，CameraService 能枚举外置相机。

Root 不能在用户空间绕过内核的 vermagic、`CONFIG_MODVERSIONS`、GKI KMI 或模块签名强制。
它也不能保证在 Magisk late_start 时才出现的 VINTF fragment 会被已经启动的
hwservicemanager 重新读取。

## 1. 预检

连接 ADB，并在 Magisk 中允许 shell Root 请求：

```powershell
.\tools\adb\check-root-stock.ps1
```

结果含义：

| 状态 | 含义 | 处理 |
|---|---|---|
| `READY` | loopback 与 external/0 已存在 | 可构建不含额外 payload 的模块 |
| `NEEDS_V4L2LOOPBACK` | 没有 `/dev/video100` | 提供精确匹配 `.ko` 或 custom boot kernel |
| `NEEDS_COMPATIBLE_KERNEL_MODULE` | `insmod` 被拒绝 | 核对 dmesg 中签名、KMI、vermagic、未知符号错误 |
| `NEEDS_EXTERNAL_CAMERA_PROVIDER` | external/0 未注册 | 提供同 API/vendor Provider，并解决早期 VINTF 声明 |
| `NEEDS_CONTROLLER_APK` | 5 分钟内未找到普通 UID 控制端 | 手动安装 ZIP 旁的 Root controller APK 后重启 |
| `DAEMON_START_FAILED` | Root daemon 未启动 | 查看 `/data/adb/vcames/root-service.log` |

## 2. 准备 payload

### v4l2loopback

模块必须来自目标设备正在运行的同一 kernel build。Pixel 6 GKI 设备优先使用 custom kernel
已集成驱动，或把模块纳入该 kernel 的 vendor_dlkm/KMI/签名流程。不要用 `--force`、修改
内核内存或关闭签名检查规避错误。

如果 custom kernel 已经提供 `/dev/video100`，打包时不传 `-KernelModule`。

### External Camera Provider

Provider 必须按设备 Android 分支构建：API 33/34/35 分别使用相应 Android 13/14/15
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

输出位于 `out/root/`：Magisk ZIP 和可手动安装的 Root controller APK。正式分发时应给
Root APK 使用稳定的自有签名；CI debug APK 的签名不保证跨构建一致。

## 4. 安装与恢复

在 Magisk 中安装 ZIP 并重启。安装脚本会尝试安装普通 UID 控制 APK，不会卸载签名冲突
的旧版本。重启后在模块页面执行 Action，确认状态 `READY`，再打开 VCamES Root。

出现启动问题时，可从可用的 Root ADB/救援环境禁用模块：

```bash
touch /data/adb/modules/vcames_root_bridge/disable
```

然后重启。模块不会删除控制 APK 数据；恢复后由用户自行卸载 APK。

## 安全边界

- 不调用 `setenforce 0`，不加载 Xposed/Zygisk 库，不 hook Camera 私有符号。
- daemon 虽由 Root 启动，但只接受 UID 0、1000 和安装时解析出的一个控制 App UID。
- HTTP MJPEG 是明文，仅用于可信局域网；不在 URL 中放账号密码。
- 可选 Provider 在 Magisk domain 中运行属于兼容路径，不如 ROM 原生 `hal_camera_default`
  集成稳定；生产/长期使用仍推荐 AOSP 原生方案。

参考：[Magisk 模块与启动脚本](https://topjohnwu.github.io/Magisk/guides.html)、
[AOSP 可加载内核模块](https://source.android.com/docs/core/architecture/kernel/loadable-kernel-modules)、
[AOSP GKI 模块](https://source.android.com/docs/core/architecture/kernel/modules)。
