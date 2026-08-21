# 资源与许可边界

公开仓库只存放自有源码、接口定义、公开设备元数据、哈希、脱敏验收结果和许可允许再分发的第三方代码。

以下内容禁止提交或打包到通用发行物：

- 从 Pixel 5 原厂系统镜像或设备提取的 proprietary camera/graphics ELF、firmware、配置或密钥；
- OEM 私有头文件、符号数据库或未经许可的反编译源码；
- 签名私钥、Root 管理器凭据、用户视频帧、账号和网络凭据；
- 来源不明的 `.ko` 或 Provider 二进制。

FFmpeg 只作为独立运行时可执行文件分发。设备包必须附带 canonical `ffmpeg.LICENSE.json` 和
`licenses/FFmpeg-LGPL-2.1.txt`，记录
`LGPL-2.1-or-later`、HTTPS 源码地址、revision、完整 build configuration、协议/demuxer/decoder/filter/encoder 和
二进制 SHA-256。构建器拒绝 `--enable-gpl`、`--enable-nonfree`、哈希不匹配或能力不完整的产物。

`tools/aosp/pixel5-global-provider` 是对 Apache-2.0 AOSP Camera HAL 源码的可复现补丁工具；它不包含
Pixel proprietary camera HAL。Provider 必须在有权使用目标 vendor 环境的外部构建树中生成。

目标设备专用资源必须由有权使用它们的构建环境在仓库外提供。公开 Profile 仅记录资源哈希和来源类别，且 `contains_proprietary_oem_files` 必须为 `false`。第三方通知继续记录在根目录 `NOTICE`。
