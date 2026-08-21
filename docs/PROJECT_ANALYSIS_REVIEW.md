# 项目分析复核

旧逻辑把 Pixel 型号和 Android 11–15 当作主要兼容轴，并把 JPEG/V4L2 external 链路与
“替换前后摄像头”混为一谈。该方向已纠正：

- 运行范围收窄到 Google/Xiaomi/Samsung、API 30–33；
- 适配选择改为厂商 + SoC + HIDL/AIDL 实测 + 精确构建哈希；
- external 只代表新增外置 ID，不能声明替换原前后摄；
- front/back/both 必须由专用 adapter 接管 OEM 输出 buffer，且无 adapter 时失败关闭；
- 本地视频保持 NV21 原始帧；MJPEG 在 replacement 路径只解码一次，不再 JPEG round-trip；
- adapter 必须确认 FrameBus FD 已附着，不能用空的 ACTIVATE/OK 模拟实现；
- CI 同时覆盖 API 30 和 API 33 的 arm64 daemon/模块构建。

仍成立的限制：内核模块必须匹配目标 kernel/KMI/签名；stock VINTF 可能早于 Root late_start
缓存；不同 OTA 的 camera/provider/mapper ABI 不兼容；没有目标手机与 vendor 构建资料就不能
生成真正的前后替换 adapter 或 `VERIFIED` 结论。
