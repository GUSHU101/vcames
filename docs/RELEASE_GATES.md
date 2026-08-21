# Release gates

正式设备包必须全部满足：

1. Android assemble/lint、host tests、Python tests、API 30/34 arm64 daemon build通过；
2. Provider overlay 分别在目标 Android 11、12、13、14 精确源码树编译；
3. Profile v2、报告和 FFmpeg 许可清单是 canonical JSON，Ed25519 签名与所有哈希有效；
4. FFmpeg 实机能力包含清单要求的协议、demuxer、常用视频 decoder 和 MJPEG encoder，且构建没有 GPL/nonfree 开关；
5. 启动前 `lshal -ip` Server PID、OEM Provider 路径/哈希、`legacy/0`、ID 0 BACK、ID 1 FRONT 均匹配；接管后仍是 0/1 且 facing/orientation 正确；
6. Camera1/Camera2/CameraX、预览、拍照、录像、视频通话以及 0/1 同时打开均通过；
7. Provider 被 kill 时立即恢复原厂 Provider；daemon/proxy/V4L2 失败三次进入 SAFE_MODE 并恢复 OEM；
8. SELinux 保持 Enforcing，控制器包名/UID/base.apk SHA-256 均匹配且其他 UID 不能通过 proxy，URL 不经过 shell，禁用本地文件协议；
9. 冷启动、热重启、模块禁用/卸载、100 次相机循环和 8 小时稳定性通过；
10. OTA、内核、cameraserver、HAL 库、Provider、FFmpeg 任一哈希变化均拒绝旧设备包。

未经对应 Pixel 5 原厂 OTA 真机报告的产物不得标记 VERIFIED。
