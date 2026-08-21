# Pixel 5 support

VCamES 3.2 仅接受 `manufacturer=Google`、`brand=google`、`device=redfin`、`product=redfin*`、
arm64-v8a 和 API 30–34。其他 Pixel、其他厂商和 Android 15+ 直接拒绝。

Android 11–14 统一使用冻结的 HIDL 2.4 `legacy/0` 接管合同，但每个原厂 build 的内核模块、Provider、
FFmpeg 和系统哈希必须精确匹配。统一架构不等于二进制可跨 OTA 复用。
