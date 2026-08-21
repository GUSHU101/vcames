# Global front/back replacement

没有“替换目标”设置。Pixel 5 Provider 固定公开：

| CameraService ID | Facing | Orientation | Frame source |
|---|---|---:|---|
| `0` | BACK | 90° | `/dev/video100` |
| `1` | FRONT | 270° | `/dev/video100` |

所有通过标准 Camera1、Camera2 或 CameraX 打开 0/1 的应用自动进入替换链路。项目不按包名过滤，
不产生额外 `LENS_FACING_EXTERNAL` 摄像头，也不依赖 Android 12 才有的 Injection API。

并发打开 0 和 1 依赖 v4l2loopback 多读者和 AOSP external-device session 的真机行为，必须纳入每个
OTA 的验证报告；未经该测试的 Profile 不能标记 VERIFIED。
