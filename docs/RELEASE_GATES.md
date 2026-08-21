# 发布门禁

面向用户只发布一个内置 Root Bridge 的 APK。模块 ZIP、诊断和中间控制产物只进入 developer
artifact。

发布必须同时满足：

1. Android assemble/lint、host native tests、API 30 arm64 daemon 构建全部通过；
2. shell 语法、模块 ZIP、APK 内置资产和 Profile catalog 通过 CI；
3. 无 system flavor/shared UID、external/V4L2、Xposed/Zygisk 或 ROOT 品牌猜测；
4. 设备包 Profile 为 canonical `VERIFIED`，Ed25519 签名有效，所有哈希和
   `compatibility_id` 精确匹配；
5. 真机完成前/后摄内容、方向、分辨率、并发、应用切换、断流、adapter kill、重启和 OTA
   负向测试；
6. 不包含 OEM 私有 blob、密钥、用户媒体、账号或凭据。

仓库 catalog 为空时只能发布通用控制/诊断构建，状态必须 fail closed，不能宣称摄像头替换
已验证。
