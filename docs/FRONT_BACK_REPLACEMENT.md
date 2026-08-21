# 前置/后置摄像头替换适配器

## 必要边界

前后替换不是 external camera。adapter 必须在 OEM Camera HAL/provider 可维护边界中保留
原 camera ID 与 metadata，并只替换允许的输出 buffer。定制 ROM 优先实现 HIDL/AIDL
Provider proxy；Root 原厂系统只能维护与完整 OTA 绑定的 adapter。

本项目不提供 ptrace/ShadowHook 通用注入器，不关闭 SELinux，也不会在 ABI/hash 不匹配时
尝试启动。Google、Xiaomi、Samsung 分别还要按 Qualcomm/Tensor/MediaTek/Exynos 与实际
HIDL/AIDL transport 分支实现。

## adapter 服务协议 v2

Root Bridge 启动：

```text
vcames-camera-adapter --serve --socket vcames-camera-adapter \
  --manifest /data/adb/modules/vcames/compatibility.properties
```

daemon 通过 Linux abstract socket 依次执行：

```text
GET_INFO → PROBE → ATTACH_BUS → ACTIVATE → HEALTH
```

`GET_INFO` 必须声明 protocol v2、API 30–33 与 OEM metadata policy；`PROBE` 必须确认当前
完整构建兼容并拒绝 secure stream。`ATTACH_BUS` 在首次 `sendmsg` 附带 FrameBus memfd：

```text
ATTACH_BUS
adapter_protocol=2
transport=memfd-ring-v2
bus_version=2
header_size=4096
slot_count=4
slot_capacity=16777216
formats=jpeg,nv21,nv12,i420,rgba8888
preferred_format=nv21
.
```

附着成功必须回复：

```text
OK
adapter_protocol=2
frame_transport=attached
bus_version=2
.
```

然后 `ACTIVATE` 提交 target/width/height/fps 与 metadata/secure/failure policy，adapter 必须
回复 `pipeline=active`、`metadata=preserved`。最后 `HEALTH` 必须同时确认
`health=ready`、`frame_transport=attached`、`pipeline=active`；任何阶段失败都会发送
`DEACTIVATE` 并停止 replacement。

它必须验证 `VCFBUS2\0`、版本、映射长度、所有槽边界、format/stride/尺寸和偶数稳定
`write_epoch`，按 `published_sequence` 只取最新帧。`published_sequence=0`、adapter 错误或
daemon 死亡时按 `failure_policy` 回退 OEM 原相机。secure/protected、RAW、depth 和未知 stream
不得写入虚拟内容。只返回 `OK` 而未确认协议/FD 的旧 adapter 会被拒绝。

停止请求为：

```text
DEACTIVATE
.
```

## 精确构建与打包

先连接目标手机、允许 ADB shell ROOT，生成清单，再同时传入 adapter：

```powershell
.\tools\adb\capture-camera-compatibility.ps1 `
  -AdapterPath C:\device-build\vcames-camera-adapter

.\tools\root\build-root-module.ps1 -Api 33 `
  -ReplacementAdapter C:\device-build\vcames-camera-adapter `
  -CompatibilityManifest .\out\camera-compatibility.properties
```

没有这两个文件时构建仍可用于 external/控制层，但 front/back/both 必须明确失败。
