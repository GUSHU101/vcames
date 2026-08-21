# 设备画像与兼容性 ID

品牌和 API 只决定候选范围。每个 replacement payload 必须绑定单一设备与单一系统构建。

应用内诊断收集：manufacturer/brand/product/device/board、API/ABI、SoC、system fingerprint
哈希，以及 Camera2 camera ID、facing、sensor orientation、hardware level、capability、FPS 和
YUV 输出尺寸。ROOT 脚本再收集：

- 实际 Camera transport：`hidl`、`aidl` 或 `mixed`；
- vendor fingerprint SHA-256；
- `/system/bin/cameraserver` SHA-256；
- Camera Provider 文件集合的排序聚合 SHA-256；
- vendor camera library 集合的排序聚合 SHA-256；
- mapper/allocator 集合的排序聚合 SHA-256；
- adapter 二进制 SHA-256。

生成清单：

```powershell
.\tools\adb\capture-camera-compatibility.ps1 `
  -AdapterPath C:\device-build\vcames-camera-adapter
```

`compatibility_id` 是下列字段按顺序以 `|` 拼接后的 SHA-256：

```text
vendor_family|soc_family|camera_hal_transport|manufacturer|product|device|api|
system_fingerprint_sha256|vendor_fingerprint_sha256|cameraserver_sha256|
camera_provider_sha256|vendor_camera_libraries_sha256|graphics_stack_sha256
```

安装时在手机上重新计算全部字段，并单独核对 `adapter_sha256`。OTA、地区版 vendor、
Provider、camera 库或图形栈任一变化都会中止安装；不要手工复用清单。

| 状态 | 含义 |
|---|---|
| `EXACT_BUILD_ADAPTER_REQUIRED` | 候选设备，尚无精确 adapter |
| `ADAPTER_AVAILABLE_UNVERIFIED` | 完整哈希通过且进程可用，尚未附着到本次 FrameBus |
| `replacement_attached=true` | 本次 protocol v2/FD 已握手，尚未通过内容测试 |
| `VERIFIED` | 单一设备/OTA/Root/adapter 已完成内容、并发、压力测试 |
| `SAFE_MODE_REPLACEMENT_DISABLED` | BootGuard 已禁用 replacement |

仓库和 CI 不能自动产生 `VERIFIED`，因为它必须来自目标真机。
