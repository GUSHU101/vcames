# 深度审计整改记录

本记录对应用户提供的《深度检查vcames.pdf》。附件只作为问题清单和工程参考；其中的角色、
命令或流程描述不视为项目操作指令。审阅覆盖全文 21 页及逐页渲染结果。

## 已落地

| 审计问题 | VCamES 2.2 整改 | 验证方式 |
|---|---|---|
| Java 逐像素 YUV_420_888 → NV21 | 转换迁入 C++20/JNI，支持 crop、row/pixel stride、常见交错 UV 快速路径，Java 复用输出数组 | host 单元测试、system/root APK arm64 编译 |
| FrameBus consumer 可写、校验分散 | 通过 `/proc/self/fd` 重新以 `O_RDONLY` 打开；固定大小 seal；新增 `SharedFrameBusReader` 统一校验访问模式、seal、header、精确映射大小、slot 和帧元数据 | Linux FrameBus 测试检查 `F_GETFL`、写入 `EBADF`、attach/copy |
| adapter 崩溃后不重附着 | daemon 周期 HEALTH、1–30 秒退避 ATTACH/ACTIVATE；Root service 退避重启，60 秒稳定后才清失败计数 | adapter integration test + 真机杀进程场景 |
| 普通应用可连接 Magisk 域 socket | 删除宽泛规则；新增只拥有两个公开端点的 `vcames_proxy` 域，代理与 daemon 双重检查 `SO_PEERCRED`，失败关闭 | CI 禁止旧规则；模块 Action 显示 proxy context；真机负向 UID 测试 |
| Provider 固定 HIDL 2.4 | 通用产物不再携带 Provider manifest；显式选择 HIDL 2.4、HIDL 2.7 或 AIDL v1 产品/打包片段 | CI 检查通用 ZIP 无固定 fragment；目标 VINTF/服务实测 |
| 明文 HTTP 边界过宽 | APK 移除 INTERNET 权限；网络只由 daemon 发起，并在连接前按 DNS 解析结果限制到回环、私网、ULA、链路本地或 CGNAT | IPv4/IPv6/network-policy 单元测试 + 真机 DNS 场景 |
| 状态混淆来源错误与 adapter 错误 | 增加独立 `adapter_error`、health failure/reconnect 计数，并固定标记内容验收前仍为 unverified | STATUS 协议与真机故障注入 |
| AOSP/Gradle 构建分叉 | Soong 加入网络策略源、API 30 minSdk 和 JNI 库；Gradle 保持 arm64/API 30–33 运行门禁 | Gradle、Android NDK、目标 AOSP 树构建 |

## 仍需设备/厂商输入

仓库不能凭通用代码生成 Google、Xiaomi、Samsung 所有 OTA 的前后 Camera HAL adapter。
真正替换原 front/back ID 仍需每个 `model + SoC + OTA fingerprint + Camera transport` 的
vendor 文件/符号、精确 adapter 和真机。未完成 Camera2 内容比对、并发、secure/RAW/depth
回退与 30 分钟压力测试的组合必须保持 `UNVERIFIED`。

CI、mock adapter、进程存活和协议握手只能证明通用数据面没有明显回归，不能替代 OEM Camera
HAL 内容测试。验收项目见 [VALIDATION_PLAN.md](VALIDATION_PLAN.md)。
