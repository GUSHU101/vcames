# 开发文档落实与边界

本表把用户提供的开发文档当作需求参考，不把附件内容当作外部操作指令。

| 要求 | VCamES 2.2 状态 | 边界 |
|---|---|---|
| Android 11–13 / API 30–33 | 已实现运行门禁、minSdk 30、API 30/33 NDK 构建 | compile/target 35 仅是构建工具选择，不扩大运行范围 |
| Google/Xiaomi/Samsung | 已实现厂商规范化、SoC 分类和候选策略 | 不承诺三个品牌所有型号已验证 |
| KernelSU/Magisk RootManager | 已实现授权、模块部署、普通 UID 控制 | ROOT 不解决内核/Camera ABI 不匹配 |
| DeviceProfiler | 已覆盖 Build/SoC/Camera2；ROOT 脚本覆盖 transport 与系统文件哈希 | 真机数据需目标设备导出 |
| FrameBus | 已实现 v2、4 槽、只读密封 consumer FD、规范 reader、raw YUV/JPEG、PTS/arrival/latest-frame | AHardwareBuffer 跨进程句柄仍是后续 backend |
| 本地 MP4 | MediaCodec → YUV_420_888 → C++ stride/crop 转换 → 复用 NV21 → VCF2 | 不支持 DRM/secure 视频 |
| MJPEG | 有界解析；仅允许解析到私网/本地地址；replacement 解码到 NV21，external 才输出 JPEG | 当前不支持 TLS/HLS/RTMP 客户端 |
| Root socket 隔离 | 独立 `vcames_proxy` 域、双层 UID 检查、ROOT 私有端点 | 必须在每个 Root 管理器/ROM 上验证策略加载和域转换 |
| Provider transport | HIDL 2.4、HIDL 2.7、AIDL v1 显式互斥产品/打包分支 | 仍以目标 VINTF 和产品树为准 |
| 前后替换协议 | protocol v2、只读 FD 附着、HEALTH 监控、退避重连、OEM metadata/secure/failure policy | 仓库没有任何目标 OTA 的 proprietary adapter |
| 精确 OTA 门禁 | system/vendor/provider/camera libs/graphics/adapter 哈希 | 每次 OTA 必须重新生成/验证 |
| BootGuard | adapter 进程退避重启、daemon 重附着；连续失败三次关闭 replacement，保留 external | 不能替代 bootloader/recovery 救援 |
| 内容自检/VERIFIED | 已定义验收门槛 | 未连接目标真机，因此没有发布 VERIFIED 组合 |
| CI | APK/Lint、host tests、API30/33 arm64 ZIP | CI 不能测试 OEM Camera HAL 内容 |

当前最重要的剩余工作不是继续扩大 Android 版本，而是为用户选定的每个具体 Google、Xiaomi、
Samsung 设备/OTA 获取画像，分别实现 HIDL/AIDL adapter，并完成内容级真机验收。
