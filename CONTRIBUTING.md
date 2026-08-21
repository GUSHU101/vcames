# Contributing

提交前请运行：

```bash
./gradlew :app:assembleSystemDebug :app:assembleRootDebug \
  :app:lintSystemDebug :app:lintRootDebug
cmake -S daemon -B out/host -G Ninja
cmake --build out/host
ctest --test-dir out/host --output-on-failure
```

设备相关改动需要说明厂商、型号/代号、SoC、Camera HIDL/AIDL、Android API、完整系统与
vendor fingerprint、kernel release、Root 管理器和 SELinux 状态，
以及 `tools/adb/verify-vcames.ps1` 的结果。不要提交 APK、平台签名密钥、vendor blob、
设备序列号、媒体样本或参考 APK 的反编译内容。
