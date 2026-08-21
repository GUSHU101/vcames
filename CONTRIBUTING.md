# Contributing

提交前请运行：

```bash
./gradlew :app:assembleDebug :app:lintDebug
cmake -S daemon -B out/host -G Ninja
cmake --build out/host
ctest --test-dir out/host --output-on-failure
```

设备相关改动需要说明厂商、型号/代号、SoC、Camera HIDL/AIDL、Android API、完整系统与
vendor fingerprint、`compatibility_id`、ROOT uid 0 授权结果和 SELinux 状态，
以及 diagnostics.zip 中的非媒体诊断结果。不要提交 APK、平台签名密钥、vendor blob、
设备序列号、媒体样本或参考 APK 的反编译内容。
