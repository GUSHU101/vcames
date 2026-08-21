# 前后摄替换协议

adapter 是设备 + OTA 专用组件，必须保留 OEM camera ID、facing、静态 metadata、时间戳
和客户端协商边界。secure/protected、RAW、depth 等不能安全替换的流必须拒绝并回到 OEM。

启动协议固定为 adapter protocol 2：`GET_INFO` → `PROBE(require_exact_build=1)` →
`ATTACH_BUS` → `ACTIVATE` → `HEALTH`。FrameBus FD 为只读、定长、密封 memfd；adapter
响应必须确认 `memory_access=read-only`、`metadata=preserved` 和
`failure_policy=oem-passthrough`。

daemon 每两秒健康检查并重新附着。源断流超过 800 ms 会 invalidate FrameBus，而不是无限
保持最后一帧。adapter 连续三次启动/运行故障后 BootGuard 写入安全模式标记，不再尝试替换。
