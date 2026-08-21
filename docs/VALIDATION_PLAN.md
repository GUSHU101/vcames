# Google / Xiaomi 真机验收门槛

每个 `vendor + model/device + SoC + OTA fingerprint + root manager + adapter` 组合独立验收。
进程启动或协议握手不等于画面替换成功。

最低测试：

1. 安装、重启、升级、卸载、Root 安全模式救援；SELinux 始终 Enforcing；
2. external/front/back/both 各启动、停止、切换 100 次；
3. 720p30、1080p30，本地 MP4 与 MJPEG，0/90/180/270°、镜像、hold/blank；
4. Camera2 预览和测试图内容比对、JPEG 拍照、MediaRecorder、CameraX、系统相机、第三方应用；
5. 校验原 camera ID/facing/metadata 不变，secure/RAW/depth 明确 OEM passthrough 或拒绝；
6. 前后台、锁屏、来电、daemon/adapter/来源崩溃与恢复；杀死 adapter 后确认进程退避重启、
   daemon 重新附着只读 FrameBus，且 OEM fallback 期间不泄漏旧帧；
7. 前后并发/逻辑多摄、不同 aspect ratio/stride、重复配置 stream；
8. 30 分钟稳定运行，观察内存、FD、温度、PTS 单调、无旧帧回放；
9. 三次 adapter 失败进入 BootGuard，external 救援仍可用；
10. 三个厂商至少分别覆盖 API 30、31/32、33 的目标组合后再发布相应兼容清单。
11. 用 `fcntl(F_GETFL)` 验证 adapter 收到的 FrameBus FD 为只读，写入返回 `EBADF`，并验证
    GROW/SHRINK/SEAL；畸形 header、slot、stride、format 全部失败关闭。
12. 从域名、IPv4、IPv6 和 IPv4-mapped IPv6 测试私网 MJPEG；解析到公网或 DNS 重绑定到
    公网地址时必须拒绝连接。
13. Root 模块确认不含 `allow untrusted_app_all magisk:unix_stream_socket connectto`，代理进程
    位于 `vcames_proxy` 域；非 controller 应用 UID 不能通过代理。
14. 对目标 ROM 只打包一种 Provider transport，并用 VINTF/服务查询确认 HIDL 2.4、HIDL 2.7
    或 AIDL v1 声明与实际二进制一致。

报告必须记录 compatibility ID、所有构件 SHA-256、系统/vendor fingerprint、Camera transport、
Root 管理器版本、实际内容截图/哈希和失败日志。未完成的组合只能标 `UNVERIFIED`。
