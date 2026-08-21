# Google / Xiaomi / Samsung 支持策略

运行范围固定为 Android 11–13（API 30–33）和 `arm64-v8a`。厂商名只决定是否进入候选范围，
不直接决定 Camera adapter；真正的策略键是：

```text
vendor family + SoC family + HIDL/AIDL 实测 + device/product + API + 完整构建哈希
```

| 厂商族 | 首批 SoC 路径 | 候选状态 | 必须实测 |
|---|---|---|---|
| Google | Qualcomm、Tensor | candidate | VINTF/provider transport、OEM camera ID、vendor buffer/mapper |
| Xiaomi/Redmi/POCO | Qualcomm、MediaTek | candidate | MIUI/原厂 vendor provider、camera lib 集合、buffer layout |
| Samsung | Exynos、Qualcomm | candidate | One UI provider、Exynos/Qualcomm 分支、gralloc/mapper |

“candidate”只代表画像工具和构建门禁接受该组合，不代表 replacement 已实现或验证。
同一品牌、型号甚至 Android 版本相同，也可能因地区版 SoC、月度 OTA、vendor blob 或 Root
方案不同而需要不同 adapter。

## 状态原则

- `EXACT_BUILD_ADAPTER_REQUIRED`：范围内，但没有当前构建专用 adapter；
- `ADAPTER_AVAILABLE_UNVERIFIED`：adapter 进程可用，但尚未证明已接管本次输出；
- daemon `replacement_attached=true`：FrameBus v2 已握手，仍不等于相机内容正确；
- `VERIFIED`：指定设备/OTA/Root/adapter 通过内容、并发和压力测试；
- 系统或 vendor 哈希变化后，旧清单立即失效并失败关闭。

不以“Pixel 型号白名单”或“Android 大版本”替代上述探测，也不承诺三个厂商的所有型号。
