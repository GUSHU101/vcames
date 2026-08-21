# Profile v2 contract

Schema 位于 `profiles/schema/profile-v2.schema.json`。Profile 绑定 Pixel 5 设备/build/内核、HAL transport、
cameraserver、原厂 Provider/vendor camera/graphics 栈、v4l2loopback、全局 Provider、FFmpeg 和许可清单。

`camera_hal` 固定 `legacy/0`、HIDL 2.4、`global-front-back`、ID 0/1、`/dev/video100`，并记录精确 OEM
init service、HAL Server 可执行文件绝对路径及 SHA-256。运行时通过 `lshal -ip`、`/proc/<pid>/exe` 和哈希
三重核验后才允许停止原厂 Provider。`capabilities.global_front/global_back` 必须为 true。构建器从 Profile 生成只读
`profile.runtime.properties`，不接受手写 projection。

Profile 与非空 catalog 使用离线 Ed25519 签名；私钥不得进入仓库、CI、APK 或模块。
