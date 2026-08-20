# Pixel 4–6 真机验收门槛

每个 `device + OTA fingerprint + root manager + adapter` 组合独立验收。基础启动成功不等于
相机可用；只有测试应用从 Camera2 收到预期测试图案并核对时间戳/尺寸，才算内容级通过。

最低验收集：

1. 安装、重启、卸载和救援模式；SELinux 全程 Enforcing，无新增 denial 洪泛。
2. external/front/back/both 各模式启动、停止、重复切换 100 次。
3. 720p30、1080p30；本地视频和 MJPEG；旋转、镜像、断流 hold/blank 行为。
4. Camera2 预览、JPEG 拍照、MediaRecorder 录像、CameraX、系统相机和至少两个第三方应用。
5. 前后台、锁屏、来电/音频焦点、应用崩溃、adapter 崩溃和 daemon 崩溃恢复。
6. 30 分钟稳定运行与内存/FD/温度观察；帧 PTS 单调、无旧帧回放。
7. 连续三次 adapter 启动失败进入 `SAFE_MODE_REPLACEMENT_DISABLED`，external 模式仍可救援。

测试报告必须记录 `compatibility_id`、模块/APK/adapter SHA-256、系统 fingerprint、失败日志和
是否实际观察到目标内容。未经以上测试的组合只能标为 `UNVERIFIED`。
