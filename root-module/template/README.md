# VCamES Root Bridge

适用于已 Root 的 Google、Xiaomi/Redmi/POCO Android 11–13（API 30–33）arm64
原厂系统。应用只验证 uid 0 与模块安装能力，不识别或猜测 ROOT 管理器品牌；不使用
Xposed/Zygisk，不关闭 SELinux。
`vcamesd` 建立端点后会降权到 Android system UID，并仅保留 camera/inet 补充组；降权失败
会直接退出。

控制 APK 只连接专用 `vcames_proxy` SELinux 域；代理先校验 `SO_PEERCRED`，再转发到
ROOT 私有端点。模块不会向普通应用开放 Magisk 域的其他 Unix socket。

- 只提供 front/back/both；external/V4L2 已从主产品移出；
- 当前 OTA 专用 adapter 必须匹配已签名的 Profile v1 与 `compatibility_id`；
- adapter 通过 FrameBus v2 的只读、定长密封 memfd 接收 NV21/JPEG 最新帧，必须确认
  protocol v2、FD attached 与 `memory_access=read-only`；
- 无精确 adapter 时前后替换不可用并回退 OEM 相机；ROOT 授权不会自动产生 Camera HAL
  兼容性。

重启后在 Root 管理器执行模块 Action 查看状态。任何 `UNVERIFIED` 状态都必须完成真机
Camera2 内容、并发和压力测试后才能发布为 `VERIFIED`。
