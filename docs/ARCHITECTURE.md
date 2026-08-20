# 架构与安全边界

## External 与前后替换

Android 11–15 的 CameraService 只能通过相机 HAL 获得系统相机。VCamES 选择 AOSP 已有的
External Camera Provider：它监听 `/dev/video*`，把符合 V4L2 capture 规范的节点注册为
Camera HAL 3.x 外置相机。v4l2loopback 同时提供 output 端给 `vcamesd`、capture 端给 Provider。

这条路径不会修改应用进程，不依赖 Xposed，也不使用参考 APK 中的私有符号 hook。
`front/back/both` 由一个设备构建专用的 Camera HAL adapter 保留原 camera ID 和 metadata，
仅替换输出 buffer；通用 daemon 通过受限 socket 激活它，并以 `SCM_RIGHTS` 传递
`memfd-ring-v1` FrameBus，不持有 cameraserver 私有 ABI。

## 组件

| 组件 | 分区/权限 | 职责 |
|---|---|---|
| `VCamES` | `system_ext/priv-app`、平台签名、UID 1000 | 配置、SAF 选片、MediaCodec 本地解码、状态 UI |
| `vcamesd` | `system_ext/bin`、独立 SELinux domain | HTTP MJPEG、变换、限流、V4L2 写入 |
| v4l2loopback | kernel/vendor_dlkm | `/dev/video100` 环形帧设备 |
| External Provider | vendor HAL | V4L2 capture → Camera HAL 3.6 |
| Camera adapter（可选） | vendor HAL/精确 Root payload | 原前后 camera ID → FrameBus 最新帧 |

## 控制协议

三个 Linux abstract `AF_UNIX` socket：`vcamesd` 接收文本命令，`vcamesd_frames` 接收
`VCF1` 魔数和 big-endian 长度前缀 JPEG。服务端使用 `SO_PEERCRED` 拒绝 UID 0/1000
之外的连接；SELinux 仅允许 `system_app` 连接 `vcamesd` domain。可选
`vcames-camera-adapter` 接收 ACTIVATE/DEACTIVATE，只应接受 UID 0/系统相机 UID。

FrameBus v1 是 3 槽、latest-frame、sequence-lock 共享内存环：header 固定 4 KiB，槽保存
sequence、PTS、到达时间、宽高、格式和 payload 长度。当前格式为 JPEG；YUV/AHardwareBuffer
是后续兼容版本，不能在未更新 adapter 的情况下静默改变 wire format。

## 延迟和故障处理

来源线程只保留最新帧，写入线程不会回放积压；超出的 generation 计为 dropped。
HTTP 断线采用 1–30 秒指数退避，成功连接后重置为 1 秒。`hold_last=false` 时超过阈值关闭 sink；开启时以目标
FPS 重复最后一帧。单帧上限 16 MiB，分辨率上限 3840×2160。

## 启动顺序

内核节点必须在 `class core` 之前创建。`vcamesd` 属于 `class core`，启动后先写入
1280×720 中性 JPEG，固定 loopback 的 MJPEG format；External Provider 属于 `class hal`，
随后枚举已配置节点。这个顺序避免 Provider 在 format 未设定时忽略 `/dev/video100`。
