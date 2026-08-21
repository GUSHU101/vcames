# 发布门禁

单个 `compatibility_id` 只有全部满足以下条件才能标记 `VERIFIED`：

1. 设备身份：model/product/device、API、Build ID、安全补丁、region 与 system/vendor fingerprint 哈希完全一致。
2. 产物身份：daemon、proxy、adapter、Provider、内核模块和发布 ZIP/APK 的 SHA-256 已记录并可复现。
3. 安全门禁：SELinux enforcing；普通应用只能访问 public proxy；ROOT private socket 不向 `untrusted_app` 暴露；不使用 Xposed/Zygisk 注入。
4. 功能门禁：Camera2 枚举；external 或前/后目标的真实画面；NV21/YUV 格式、方向、镜像、裁切、PTS、断流策略；通话/录像/前后台切换。
5. 稳定性门禁：冷启动、热重启、daemon/proxy/provider/adapter 故障恢复，至少 8 小时持续运行和 100 次启动/停止；无 cameraserver 循环崩溃。
6. OTA 门禁：任何 fingerprint、Camera ELF、graphics stack 或 kernel/KMI 变化都会使旧 Profile 失效，回退到 fail-closed。
7. 隐私门禁：`diagnostics.zip` 不含用户视频、帧、凭据或账号数据；报告已脱敏。

CI 只证明源码可构建、schema 合法和主机测试通过，不能替代真机验收。当前 Profile catalog 为空，因此项目状态是“工程链路已实现、产品设备尚未 VERIFIED”。
