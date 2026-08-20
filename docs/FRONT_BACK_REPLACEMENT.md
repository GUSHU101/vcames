# 前置/后置摄像头替换适配器

## 两条相机路径

`external` 是跨构建的通用路径：VCamES 把 MJPEG 写到 V4L2 loopback，AOSP External Camera
Provider 注册一个新外置 camera ID。AOSP 文档也明确说明 external USB camera 并不替代
手机内置 HAL。

`front`、`back` 和 `both` 必须保留 Pixel 原有 camera ID、facing、静态 metadata 和 capture
request/result 语义，只替换输出 buffer。这属于 Camera HAL/provider proxy 工作，不能由普通
APK 或通用 v4l2loopback 节点完成。

Android 13 起 Camera HAL 新开发使用稳定 AIDL，同时框架仍兼容既有 HIDL HAL。具体 Pixel
原厂构建使用哪套 provider/vendor 组件，必须从目标系统 VINTF、服务注册与 ELF 依赖实测，
不能只按 Android 大版本猜测。

参考：
[AOSP Camera HAL](https://source.android.com/docs/core/camera/camera3)、
[External USB cameras](https://source.android.com/docs/core/camera/external-usb-cameras)、
[VINTF manifests](https://source.android.com/docs/core/architecture/vintf/objects)。

## daemon 激活协议

设备专用二进制安装为 `bin/vcames-camera-adapter`，由 Root Bridge 以以下参数启动：

```text
vcames-camera-adapter --serve --socket vcames-camera-adapter --frame-device /dev/video100
```

它必须监听名为 `vcames-camera-adapter` 的 Linux abstract `AF_UNIX` socket，只允许 Root 模式
UID 0 或 ROM 模式 UID 1000 的 daemon peer；daemon 也只信任 UID 0、1000 或 cameraserver
UID 1047 的适配器。协议为 UTF-8 行文本，以单独一行 `.` 结束：

```text
ACTIVATE
target=both
device=/dev/video100
width=1280
height=720
fps=30
.
```

只有适配器已经把目标 camera pipeline 安全切换到虚拟帧时，才能返回 `OK\n`。错误应返回
稳定、可读的单行原因。停止/切回 external 时 daemon 发送：

```text
DEACTIVATE
.
```

适配器退出、拒绝或 2 秒内不应答时，`vcamesd` 拒绝启动 front/back/both 模式。

## 精确构建绑定

先在目标手机连接 ADB，并允许 shell 的 Magisk ROOT 请求：

```powershell
.\tools\adb\capture-camera-compatibility.ps1
```

生成文件包含：

```properties
device=redfin
api=34
fingerprint_sha256=<ro.build.fingerprint 的无换行 SHA-256>
cameraserver_sha256=</system/bin/cameraserver 的 SHA-256>
```

打包时同时传入适配器和清单：

```powershell
.\tools\root\build-root-module.ps1 -Api 34 `
  -ReplacementAdapter C:\pixel-build\vcames-camera-adapter `
  -CompatibilityManifest .\out\camera-compatibility.properties `
  -KernelModule C:\pixel-build\v4l2loopback.ko
```

模块安装器会在写入 `/data/adb/modules` 前比较全部四项。OTA、设备或 cameraserver 任一变化
都会中止安装。清单不是“兼容声明”的替代品：适配器本身仍需对该构建做 Camera CTS/VTS、
前后切换、预览/录像/拍照、并发 camera、旋转和长时间稳定性测试。

## 推荐实现方式

定制 ROM 应在目标 Android 分支实现 AIDL/HIDL Camera Provider proxy，并在构建期加入 VINTF、
init 与 SELinux；这是最可维护的前后替换方案。Root 原厂兼容层只能是按完整 OTA 构建维护的
适配器，并且保持 SELinux Enforcing。VCamES 不提供通用 ptrace/ShadowHook 注入器，也不会
在 ABI 不匹配时尝试启动。

状态含义：

| 状态 | 能力 |
|---|---|
| `READY_EXTERNAL_ONLY` | 仅 external |
| `READY_REPLACEMENT_ONLY` | 仅 front/back/both |
| `READY_EXTERNAL_AND_REPLACEMENT` | 两条路径都可用 |
| `REPLACEMENT_ADAPTER_START_FAILED` | 已打包适配器但启动失败 |
