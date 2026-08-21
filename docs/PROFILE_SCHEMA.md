# Profile v1 契约

Profile 是“设备 + OTA + Camera 栈 + adapter”的唯一源数据。公开 schema 位于
`profiles/schema/profile-v1.schema.json`；构建器从 Profile 生成只读 runtime projection，
不再接受人工维护的第二份兼容性清单。

Profile 至少记录设备/build、HAL transport、cameraserver/Provider/vendor camera/graphics/
adapter 哈希、front/back 能力、发布产物哈希和 VERIFIED 报告。`compatibility_id` 算法见
[DeviceProbe](DEVICE_PROFILING.md)。

Profile 与 catalog 使用离线 Ed25519 私钥签名；私钥不得进入仓库、CI 或 APK。非空 catalog
必须同时验证每个 Profile 和 catalog 签名、canonical JSON、报告、产物哈希与重复 ID。

```bash
python3 tools/compatibility-builder/profile_sign.py canonicalize profile.json profile.canonical.json
python3 tools/compatibility-builder/profile_sign.py sign profile.canonical.json /offline/private.pem profile.sig
python3 tools/compatibility-builder/validate_profiles.py profiles/catalog.json --public-key release-public.pem
```
