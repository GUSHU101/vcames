# 资源与许可边界

公开仓库只存放自有源码、接口定义、公开设备元数据、哈希、脱敏验收结果和许可允许再分发的第三方代码。

以下内容禁止提交或打包到通用发行物：

- 从 Google/Xiaomi 系统镜像或设备提取的 proprietary camera/graphics ELF、firmware、配置或密钥；
- OEM 私有头文件、符号数据库或未经许可的反编译源码；
- 签名私钥、Root 管理器凭据、用户视频帧、账号和网络凭据；
- 来源不明的 `.ko`、Provider 或 adapter 二进制。

目标设备专用资源必须由有权使用它们的构建环境在仓库外提供。公开 Profile 仅记录资源哈希和来源类别，且 `contains_proprietary_oem_files` 必须为 `false`。第三方通知继续记录在根目录 `NOTICE`。
