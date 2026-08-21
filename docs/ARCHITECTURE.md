# 架构与安全边界

## 两条系统路径

`external` 使用公开 AOSP External Camera Provider：`vcamesd` 把 JPEG 写入
`/dev/video100`，Provider 注册新的外置 camera ID。它不替代 OEM 前后摄像头。

`front/back/both` 使用精确构建 Camera adapter。adapter 必须保留原 camera ID、facing、
静态 metadata、capture request/result 语义，仅替换普通预览/录像/拍照输出 buffer。
secure/protected、RAW、depth 或未知 stream 必须拒绝虚拟帧并回退 OEM pipeline。

运行范围是 Google/Xiaomi、Android 11–13（API 30–33）、arm64。HIDL/AIDL 由
目标 VINTF、服务注册和 ELF 集合实测，不能按品牌或 Android 版本猜测。

## 组件

| 组件 | 权限/位置 | 职责 |
|---|---|---|
| 控制 APK | system 变体或普通 Root 变体 | 配置、SAF 选片、MediaCodec、ROOT 部署、状态；不持有 INTERNET 权限 |
| `vcamesd` | `system_ext` domain 或 Root 模块降权后的 system UID | 来源、变换、限流、FrameBus、V4L2 |
| `vcames-socket-proxy` | Root 模块专用 `vcames_proxy` 域，随后降到 system UID | 校验控制 APK UID，仅转发两个公开端点到 ROOT 私有 socket |
| v4l2loopback | kernel/vendor_dlkm | external 模式的 `/dev/video100` |
| External Provider | vendor HAL | V4L2 capture → 新 external camera ID |
| 精确 Camera adapter | ROM/vendor 或单 OTA Root payload | FrameBus → 原 front/back 输出 buffer |

## 帧协议

- `VCF2`：本地 MediaCodec 生产者发送 format/width/height/stride/size/PTS + NV21 payload；
- `VCF1`：只为旧 JPEG producer 保留的兼容入口；
- FrameBus v2：4 个 16 MiB 槽，`VCFBUS2\0`，latest-frame，sequence lock，包含
  PTS、arrival、format、stride、rotation 和 flags；
- 当前发布格式支持 JPEG/NV21/NV12/I420/RGBA，replacement 首选 NV21；
- memfd 通过 `SCM_RIGHTS` 传递；producer 保留可写映射，adapter 只能收到重新打开的
  `O_RDONLY` FD。文件带 `F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL`，reader 会校验访问模式、
  seal、精确映射尺寸、header、槽边界、payload、format/stride 和写 epoch。

系统镜像路径由 SELinux + daemon `SO_PEERCRED` 直接限制。Root 路径的公开 socket 归
`vcames_proxy` 域，普通应用不再获得连接任意 Magisk-domain socket 的权限；代理和 daemon
都会检查 UID 0、1000 或安装时记录的控制 APK UID。adapter socket 还要做
`SO_PEERCRED`。协议 v2 按 GET_INFO/PROBE/ATTACH_BUS/ACTIVATE/HEALTH 分阶段确认，必须同时
确认 `frame_transport=attached`、`pipeline=active` 与 `health=ready`，不能只以“进程存在”
宣布成功。

Root 模块只以 UID 0 完成节点权限、监听端点和部署前提；`vcamesd` 随后调用
`setgroups(camera, inet) → setgid(system) → setuid(system) → no_new_privs`，失败则退出，不让媒体链长期
运行在 UID 0。SELinux 仍保持 Enforcing。

## 延迟与失败

来源只保留最新 generation；写线程按目标 FPS 取最新帧并统计丢弃。本地 YUV stride/crop
转换在 C++ 完成并复用 Java 输出数组。HTTP 只连接解析后的私网/本地地址，重连为 1–30 秒
指数退避；半包 socket 2 秒超时；输入不超过 16 MiB/3840×2160。`hold_last=false` 时
FrameBus 发布序号归零，adapter 应回退 OEM 或停止虚拟输出。连续三次 adapter 启动失败进入
BootGuard 安全模式。daemon 还会每 2 秒执行 HEALTH，在 adapter 重启后重新传递只读 FD 并
完成 ATTACH/ACTIVATE；重连采用 1–30 秒退避。
