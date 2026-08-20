# VCamES Root Bridge

这是面向已 Root 原厂 Pixel 4–6、Android 13–15 的条件式系统无修改部署模块。

模块不会关闭 SELinux，也不包含 Xposed/Zygisk 注入。它需要：

1. 与当前 `uname -r`、KMI 和模块签名完全匹配的 v4l2loopback；
2. 能在当前 stock VINTF 下注册的匹配版本 External Camera Provider；
3. 普通签名的 `io.github.gushu101.vcames` Root 控制 APK。

重启后在 Magisk 的模块页面执行 Action。`READY_EXTERNAL_ONLY`、
`READY_REPLACEMENT_ONLY` 或 `READY_EXTERNAL_AND_REPLACEMENT` 分别表示对应链路可用。
其他状态及处理方式见项目的 `docs/ROOT_STOCK.md`。
