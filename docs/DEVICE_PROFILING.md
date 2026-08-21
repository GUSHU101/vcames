# DeviceProbe

应用侧 `DeviceProbe` 只收集事实：设备/build、ABI、system fingerprint 哈希以及 Camera2
ID、facing、capability、FPS 和 YUV 尺寸。ROOT 模块的 `device-probe.sh` 再收集 vendor
fingerprint、内核版本、cameraserver、Provider、vendor camera 库、graphics 栈和 HAL transport 哈希。

ROOT probe 按冻结顺序连接以下值并计算小写 SHA-256：

`vendor_family | soc_family | camera_hal_transport | manufacturer | product | device | api |
system_fingerprint_sha256 | vendor_fingerprint_sha256 | kernel_release_sha256 | cameraserver_sha256 |
camera_provider_sha256 | vendor_camera_libraries_sha256 | graphics_stack_sha256`

结果是唯一的 `compatibility_id`。项目不再产生 provisional `profile_id`，也不输出
`external_candidate`、`replacement_candidate` 或 ROOT 品牌字段。
