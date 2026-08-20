# Contributing

提交前请运行：

```bash
./gradlew :app:assembleDebug :app:lintDebug
cmake -S daemon -B out/host -G Ninja
cmake --build out/host
ctest --test-dir out/host --output-on-failure
```

设备相关改动需要说明 Pixel 型号、代号、Android API、kernel release、SELinux 状态，
以及 `tools/adb/verify-vcames.ps1` 的结果。不要提交 APK、平台签名密钥、vendor blob、
设备序列号、媒体样本或参考 APK 的反编译内容。
