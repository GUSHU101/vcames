# VCamES Pixel 5 Runtime 3.2

这是 APK 之外由用户自行安装的 Pixel 5/redfin 原厂 Android 11–14 运行时。它加载精确
v4l2loopback，校验 FFmpeg，停止匹配的 OEM Provider，并用 `legacy/0` replacement Provider 全局替换
camera 0/1。APK 不安装本模块或 ROOT 管理器。

没有完整 signed device pack 时状态为 `NEEDS_SIGNED_EXACT_DEVICE_PACK`。Provider 或核心链路失败会
进入 SAFE_MODE 并恢复 OEM Provider。必须保持 SELinux Enforcing，禁止跨 OTA 复用。
