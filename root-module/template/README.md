# VCamES Root Bridge

这是面向已 Root 原厂 Pixel 4–6、Android 11–15 的条件式系统无修改部署模块，
兼容 KernelSU 与 Magisk 的模块生命周期。

模块不会关闭 SELinux，也不包含 Xposed/Zygisk 注入。它需要：

1. 与当前 `uname -r`、KMI 和模块签名完全匹配的 v4l2loopback；
2. 能在当前 stock VINTF 下注册的匹配版本 External Camera Provider；
3. 普通签名的 `io.github.gushu101.vcames` Root 控制 APK。

前/后摄像头替换适配器通过 `memfd-ring-v1` FrameBus 接收真实帧，不依赖
`/dev/video100`；但每个适配器仍必须锁定单一 OTA 的 system/vendor/provider/graphics
哈希并通过真机 Camera2 自检。KernelSU v3 的 `system/vendor` 覆盖需要设备已配置
metamodule，脚本、sepolicy 与 replacement-only 路径不依赖该覆盖。

重启后在 Root 管理器的模块页面执行 Action。`READY_EXTERNAL`、
`READY_REPLACEMENT_UNVERIFIED` 或 `READY_EXTERNAL_AND_REPLACEMENT_UNVERIFIED`
分别表示基础链路已启动；`UNVERIFIED` 只有通过真机自检和压力测试后才能升级为验证状态。
其他状态及处理方式见项目的 `docs/ROOT_STOCK.md`。
