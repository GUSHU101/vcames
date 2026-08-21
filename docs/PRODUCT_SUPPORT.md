# Product support

| 项目 | 范围 |
|---|---|
| 设备 | Google Pixel 5 / redfin，仅原厂系统身份 |
| Android | 11–14 / API 30–34 |
| ABI | arm64-v8a |
| 替换 | 全局 camera 0（后置）+ camera 1（前置） |
| 系统层 | v4l2loopback + HIDL 2.4 `legacy/0` replacement Provider |
| 网络源 | HTTP(S)/HLS/DASH、RTMP 系列、RTSP(S)、SRT、RIST、RTP/SRTP、UDP/TCP、MMS |
| 本地源 | Android 文件选择器 + MediaCodec |

支持声明以签名、精确 OTA 的 VERIFIED Profile 为准。空 catalog 表示目前没有已发布的真机验证设备包。
APK 只检测现有 ROOT 权限，不安装 KernelSU、Magisk 或模块。
