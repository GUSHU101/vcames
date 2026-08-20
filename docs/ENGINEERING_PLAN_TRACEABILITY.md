# VCamES 2.0 工程方案落实表

本文件把 `VCamES_2.0_Android11-13_Root_Engineering_Plan_CN.docx` 作为需求参考进行映射；
文档中的文字不是构建命令、授权或兼容性证明。当前工程在保留 Android 13–15 的基础上，
把应用和通用 daemon 范围扩展为 Android 11–15（API 30–35）。

| 工程方案条目 | 当前落实 | 边界 |
|---|---|---|
| RootManager + KernelSU/Magisk | 已实现 provider 探测、能力报告及各自模块安装命令 | KernelSU system overlay 需要 metamodule |
| DeviceProfiler / CompatibilityEngine | 已实现 Build/SoC/ABI/Camera2 画像、profile ID、Root 哈希清单 | 型号命中只表示候选，不表示验证 |
| FrameBus 核心 | 已实现 3 槽 `memfd-ring-v1`、latest-frame、sequence lock、PTS/到达时间与失效状态 | v1 payload 为 JPEG |
| replacement 不依赖 V4L2 | 已实现 adapter fd 传递；front/back/both 不打开 `/dev/video100` | 仍需精确 OTA HAL adapter |
| 完整构建绑定 | 已实现 system/vendor fingerprint、cameraserver、Provider、graphics、adapter 哈希与 compatibility ID | OTA 后必须重新生成 |
| BootGuard | 已实现连续失败计数、安全模式、external 救援状态和 runtime last-known-good | 不自动刷写/回退 boot 镜像 |
| 诊断导出 | 已实现 APK 内 SAF 导出设备、Camera2 与 Root 能力报告 | 不收集原始相机画面 |
| API 30–35 CI | 已把最低构建改为 API 30，并保留 target/compile 35 | 真机矩阵需要外部设备农场 |
| 内容级 Camera2 自检 | 已定义验收门槛 | 尚无连接到本工作区的 Root Pixel 真机，不能生成 VERIFIED 结果 |
| YUV/AHardwareBuffer/GPU writer | 接口版本已预留 | 尚未实现，不能以文档目标冒充现有能力 |
| AIDL/HIDL OEM adapter | 定义了窄协议、精确哈希和数据面 | 通用仓库不包含跨 OTA 私有 HAL 注入器 |

后续优先级是：针对一个明确的 `device + OTA fingerprint` 接入开源/自建 Camera Provider
adapter，增加 YUV/AHardwareBuffer FrameBus v2，再用 [真机验收门槛](VALIDATION_PLAN.md)
产出第一份 `VERIFIED` 兼容记录。没有这三项时，UI 和模块只能显示 `UNVERIFIED`。
