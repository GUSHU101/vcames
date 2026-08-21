# Architecture

VCamES 3.2 的替换边界位于 CameraService 与 Camera HAL 之间。Pixel 5 精确运行时用 HIDL 2.4
`legacy/0` 替换 Provider 报告 `device@3.4/legacy/0` 和 `device@3.4/legacy/1`，元数据分别是 BACK/90°
和 FRONT/270°，底层都打开 `/dev/video100`。因此 CameraService 对应用保留系统惯用 ID 0/1。
两路各报告 resource cost 50，允许前后 ID 同时读取同一虚拟画面。

网络流由 `vcamesd` 使用 `posix_spawn` 启动精确打包的 FFmpeg，stdout 只允许 MJPEG image2pipe。
每帧完成大小、JPEG 边界、分辨率、旋转、镜像和超时检查后写入 v4l2loopback。本地媒体由 APK 的
MediaExtractor/MediaCodec 解码成 NV21，经仅允许控制器 UID 的 socket proxy 发送。
HAL 输出固定为 1280×720@30；网络或本地输入统一缩放，避免 Provider 枚举能力后再改变 V4L2 格式。

运行时顺序为：精确 Profile/哈希校验 → FFmpeg 协议/demuxer/decoder 能力校验 → 从 `lshal -ip` 取得
原厂 HAL Server PID 并比对 `/proc/<pid>/exe` 路径与 SHA-256 → 核验 legacy/0 和 ID 0/1 → 加载
v4l2loopback → 停止 Profile 指定的原厂 init service → daemon 写入并确认 720p 占位首帧 → 注册替换
Provider → 启动 UID proxy。停止播放时回到持续黑帧，断流时持续最后一帧，避免 HAL 读端无帧阻塞。
Provider 丢失立即触发 OEM 恢复；daemon/proxy 连续失败三次也进入 SAFE_MODE 并恢复 OEM。

替换 Provider 把修改后的 HIDL 2.4 Provider 与 ExternalCameraDevice/Session/Utils 编入同一精确 OTA
二进制，不加载原厂未修改的 external-camera 实现库。

APK、运行时和设备包相互分离。APK 没有模块安装代码，运行时也不会生成或猜测 OTA 资产。
公开 socket proxy 只接受预期包 UID；运行时还把 `base.apk` SHA-256 与同一次构建生成的
`bridge.properties` 绑定，包名抢占或 APK 更新都会先拒绝接管。
