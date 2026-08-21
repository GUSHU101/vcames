# Profile v1 契约

Profile 是“设备 + OTA + Camera 栈 + 适配产物”的精确绑定，不是按品牌或 Android 大版本匹配。公开 schema 位于 `profiles/schema/profile-v1.schema.json`，目录 schema 位于 `profiles/schema/catalog-v1.schema.json`。

`compatibility_id` 沿用 `compatibility.properties` 的冻结算法：依次连接 `vendor_family | soc_family | camera_hal_transport | manufacturer | product | device | api | system_fingerprint_sha256 | vendor_fingerprint_sha256 | cameraserver_sha256 | camera_provider_sha256 | vendor_camera_libraries_sha256 | graphics_stack_sha256`，使用 UTF-8、竖线分隔并计算小写 SHA-256。字段顺序或归一化规则变更必须升级 schema，不能静默改变。

Profile v1 至少记录：

- Google/Xiaomi 厂商族、SoC、API、model/product/device/region；
- Build ID、安全补丁、system/vendor fingerprint 哈希；
- 实测 HIDL/AIDL/mixed transport 与 Provider instance；
- cameraserver、Provider、vendor camera、graphics、adapter 和发布产物哈希；
- external/front/back 能力位以及对应的 VERIFIED 验收报告。

签名流程：

```bash
python3 tools/compatibility-builder/profile_sign.py canonicalize profile.json profile.canonical.json
python3 tools/compatibility-builder/profile_sign.py sign profile.canonical.json /offline/private.pem profile.sig
python3 tools/compatibility-builder/validate_profiles.py profiles/catalog.json --public-key release-public.pem
```

使用 Ed25519 离线私钥；私钥不得进入仓库、CI 日志或安装包。非空目录必须同时签名每个 Profile 与目录本身。验证器拒绝重复 JSON key、越界路径、错误哈希、未验收条目和包含 OEM 私有资源的声明。
