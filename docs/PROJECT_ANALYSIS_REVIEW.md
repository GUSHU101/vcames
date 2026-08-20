# `项目分析.txt` 复核结果

用户提供的 `项目分析.txt` 是待验证的分析材料，不是执行指令。其主要架构判断正确：当时
代码的数据链路是 `App/HTTP MJPEG → vcamesd → /dev/video100 → External Camera Provider
→ CameraService`，只能增加 external camera，不能替换 Pixel 原前置/后置 ID。

## 已确认并修复

1. Writer 的条件变量会被每个新 generation 提前唤醒，绕过目标 FPS。现在新帧只更新
   latest-frame，写线程仍等到计划 tick，并在 tick 时取最新帧。
2. 帧 socket 在 `SO_RCVTIMEO` 后对 `EAGAIN/EWOULDBLOCK` 无限重试，半包客户端会永久占用
   串行端点。现在超时立即丢弃不完整帧并释放连接。
3. HTTP source 的指数退避在成功连接后没有重置。现在一次健康连接后的首次重连恢复为
   1 秒，再按失败次数上升至 30 秒。
4. 前后摄像头替换需求过去没有协议边界。现在配置支持 `external/front/back/both`；后三者
   必须先由 `vcames-camera-adapter` 确认激活，否则 daemon 明确拒绝 START。

## 已过时的部分

文本中“CI 只编译 host parser、不构建 Android daemon”的判断针对旧提交。当前 CI 已用
NDK 构建 arm64 `vcamesd`、生成并校验 Magisk ZIP，同时构建 system/root 两种 APK。1.2.0
进一步校验 standalone Root APK 内确实包含可由应用交给 Magisk 的 bridge ZIP。

## 仍然成立的工程限制

- JPEG 解码、旋转/缩放和再编码仍是主要 CPU/内存带宽成本；长期优化方向是让 Provider
  或设备适配器直接消费 YUV/GraphicBuffer，避免 JPEG round-trip。
- `/dev/video100` 需要与运行内核完全匹配的 v4l2loopback。ROOT 不会绕过 vermagic、KMI、
  模块签名或 `CONFIG_MODVERSIONS`。
- stock 系统的 VINTF/Provider 启动时序可能早于 Magisk late_start；external 模式在某些
  构建上仍需要 custom boot/early-init。
- 前后摄像头替换没有跨 Pixel 4–6、Android 13–15 的单一 ABI。每次 OTA 后必须重新构建、
  重新验证设备适配器。

## 本轮优化边界

本轮已完成控制层、守护进程协议、精确兼容门禁、ROOT 一体化安装链和构建入口。仓库没有
凭空生成某个目标手机的 Camera HAL 替换二进制，因为用户尚未提供具体设备的 build
fingerprint、对应 vendor/system 符号与可测试真机。构建器只会把明确传入且带兼容清单的
适配器装入 standalone APK。
