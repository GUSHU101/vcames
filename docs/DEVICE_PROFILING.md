# 设备画像与兼容性 ID

VCamES 2.0 把“机型支持”和“精确构建验证”分开：Pixel 代号与 API 只决定是否进入候选矩阵，
不能证明前/后摄像头适配器安全。每个 replacement payload 必须绑定以下输入：

- manufacturer、product、device、API；
- system/vendor fingerprint 的 SHA-256；
- `/system/bin/cameraserver` 的 SHA-256；
- vendor Camera Provider 文件集合的排序聚合 SHA-256；
- vendor mapper/allocator 文件集合的排序聚合 SHA-256；
- adapter 二进制自身 SHA-256。

应用内“生成并导出兼容性诊断”可在不改系统的情况下收集 Build、ABI、SoC 和 Camera2
camera ID/facing/capability。完整清单还需要 Root 读取系统二进制：

```powershell
.\tools\adb\capture-camera-compatibility.ps1 `
  -AdapterPath C:\pixel-build\vcames-camera-adapter
```

脚本把前九个设备字段用 `|` 连接后再做 SHA-256，得到 `compatibility_id`。安装器会重新
计算每个字段、adapter 自身哈希和 compatibility ID；任何 OTA、vendor 或图形栈变化都会
中止安装。不要手工复用旧清单。

兼容性状态采用以下含义：

| 状态 | 含义 |
|---|---|
| `ADAPTER_REQUIRED` | 设备在候选矩阵内，但尚无精确适配器 |
| `READY_*_UNVERIFIED` | 哈希匹配、进程和数据通道已启动，尚未通过内容级相机自检 |
| `VERIFIED` | 指定 OTA 完成 Camera2 内容、拍照/录像、并发与压力测试；需在外部发布清单记录 |
| `SAFE_MODE_REPLACEMENT_DISABLED` | BootGuard 检测到连续失败，前/后替换已禁用 |
| `READY_EXTERNAL_SAFE_MODE_REPLACEMENT_DISABLED` | external 可用，前/后替换处于安全模式 |

仓库的通用构建不会自动产生 `VERIFIED`，因为该结论必须来自目标真机和目标 OTA。
