# 已 Root 原厂系统部署

支持 Google、Xiaomi/Redmi/POCO Android 11–13（API 30–33）arm64。应用只运行
`su -c id -u` 确认返回 `0`，随后尝试设备实际提供的模块安装命令；不会识别 ROOT 管理器
名称或推断能力。

1. 安装 `out/release/VCamES-<version>.apk`。
2. 点击“授权 ROOT 并部署”，在 ROOT 授权界面允许。
3. 重启设备。
4. 模块没有已签名精确设备包时会显示 `NEEDS_SIGNED_EXACT_DEVICE_PACK`；这是预期的安全
   状态，不代表通用 adapter 存在。
5. 已通过发布门禁的设备包必须包含 `vcames-camera-adapter`、`profile.json`、
   `profile.sig` 和由构建器生成的 runtime projection。

模块 Action 只显示模块状态、daemon/proxy/adapter PID、BootGuard、最近错误和恢复命令。
不要关闭 SELinux；不要把 APK转换为 system UID 应用。
