# Exact-build Profile catalog

本目录只保存公开元数据，不保存 vendor blobs、OEM 库、私钥、用户媒体或凭据。当前 catalog 故意为空：
尚无 Pixel 5 原厂 OTA 完成全部真机 release gates。

Profile v2 是设备包唯一数据源；构建器生成 runtime projection。运行：

```bash
python3 tools/compatibility-builder/validate_profiles.py profiles/catalog.json
```

非空发布 catalog 与每个 canonical Profile 都必须使用离线 Ed25519 密钥签名。
