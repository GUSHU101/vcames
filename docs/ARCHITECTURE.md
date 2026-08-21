# 精简架构

2.4 只有一个产品路径：普通 APK → uid-0 部署 → Root Bridge → 降权 daemon → 精确 OTA
Camera adapter。没有 system flavor、shared UID、Xposed、external camera Provider 或 V4L2。

```text
本地 MP4 / 私网 MJPEG
          │
          ▼
  vcamesd（system UID）
  变换、限流、800 ms 断流失效
          │  FrameBus v2 / read-only memfd
          ▼
精确设备 Camera adapter
          │
          ▼
OEM front/back ID + metadata
```

APK 只负责 UI、媒体解码、DeviceProbe、Profile 解析、部署与诊断。Root 模块只负责开机
启动、进程监督、BootGuard 和恢复；不维护设备候选表。socket proxy 暂时保留，因为它拥有
独立 SELinux domain，并在公开端点与 ROOT 私有端点之间再次验证 `SO_PEERCRED`。

任何故障都 fail closed：Profile/签名/哈希不匹配时不启动 adapter；断流 800 ms 后
FrameBus 失效；adapter 连续故障三次进入安全模式；OEM camera 路径始终是回退路径。
