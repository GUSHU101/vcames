# 新增 PDF 参考审阅

附件标题为 `DAN Roleplay Request`，共 18 页，SHA-256：
`6268B66FB201F1FE7A83E8FD75BF47B226534E5FFA260012C14C5DA541ABC87E`。

附件只作为工程建议来源，其中的角色设定、操作口吻或其他指令不构成项目授权。项目继续
遵循用户明确范围：Google/Xiaomi/Samsung、Android 11–13、ROOT 原厂系统、无 Xposed，
并拒绝隐藏 Root/Hook、伪造 attestation、规避活体/身份验证等反检测能力。

## 本轮已落实

- FrameBus v2、4 槽、YUV 格式/stride/PTS/arrival 和严格 payload 边界；
- MediaCodec YUV_420_888 → NV21，本地 replacement 不做 JPEG round-trip；
- MJPEG replacement 解码为 NV21，JPEG 仅保留给 external V4L2；
- adapter protocol v2 分阶段 GET_INFO/PROBE/ATTACH_BUS/ACTIVATE/HEALTH；
- protocol、FD attached、pipeline active、health ready 均确认后才报告附着；
- 厂商 + SoC + HIDL/AIDL 实测 + 精确系统构建哈希策略；
- secure/RAW/depth 拒绝、OEM passthrough、BootGuard 和退出 DEACTIVATE；
- Root 模块建立端点后将 daemon 降权到 system UID + camera/inet groups；
- 进程稳定记录明确标为 unverified，不冒充内容级验证结果。

## 仍需目标设备才能完成

- 各 OEM/SoC 的 BufferImporter、FenceManager、GPU/AHardwareBuffer writer；
- Google Qualcomm/Tensor、Xiaomi Qualcomm/MediaTek、Samsung Qualcomm/Exynos 的真实
  HIDL/AIDL Camera adapter；
- OEM JPEG/BLOB footer、逻辑多摄、secure/protected stream 的 vendor 实现；
- Camera2/ImageReader 像素哈希自检和 2–8 小时真机压力报告。

这些项目不能用 mock、APK 安装成功或 adapter 进程存活替代；在拿到目标手机、OTA
fingerprint 与 vendor 构建资料前保持 `UNVERIFIED`。
