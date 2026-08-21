# Google / Xiaomi / Samsung 真机验收门槛

每个 `vendor + model/device + SoC + OTA fingerprint + root manager + adapter` 组合独立验收。
进程启动或协议握手不等于画面替换成功。

最低测试：

1. 安装、重启、升级、卸载、Root 安全模式救援；SELinux 始终 Enforcing；
2. external/front/back/both 各启动、停止、切换 100 次；
3. 720p30、1080p30，本地 MP4 与 MJPEG，0/90/180/270°、镜像、hold/blank；
4. Camera2 预览和测试图内容比对、JPEG 拍照、MediaRecorder、CameraX、系统相机、第三方应用；
5. 校验原 camera ID/facing/metadata 不变，secure/RAW/depth 明确 OEM passthrough 或拒绝；
6. 前后台、锁屏、来电、daemon/adapter/来源崩溃与恢复；
7. 前后并发/逻辑多摄、不同 aspect ratio/stride、重复配置 stream；
8. 30 分钟稳定运行，观察内存、FD、温度、PTS 单调、无旧帧回放；
9. 三次 adapter 失败进入 BootGuard，external 救援仍可用；
10. 三个厂商至少分别覆盖 API 30、31/32、33 的目标组合后再发布相应兼容清单。

报告必须记录 compatibility ID、所有构件 SHA-256、系统/vendor fingerprint、Camera transport、
Root 管理器版本、实际内容截图/哈希和失败日志。未完成的组合只能标 `UNVERIFIED`。
