# 2.4 深度精简记录

本轮按“删除无用户价值的第二路径和推测性配置”执行，净删除超过两千行主链路代码。

已完成：

- 合并 system/root flavor 为单一普通 APK，删除 shared UID 与 DeploymentBridge 双实现；
- RootAccess 只验证 uid 0，模块安装按实际命令成功与否判断；
- DeviceProfiler + CompatibilityEngine 收缩为 DeviceProbe + 精确 ProfileResolver；
- 删除 provisional profile ID、候选兼容性、ROOT 品牌和重复兼容性输入；
- 删除主产品 external/V4L2 后端、Provider、内核模块打包、外置 feature XML 与全部 UI 参数；
- 守护进程固定 800 ms 断流失效，adapter 故障回退 OEM；
- Root 模块收缩为启动、监督、BootGuard 和恢复；Action 只保留可操作信息；
- PowerShell/Bash 构建器改为 Python 统一实现的薄包装；用户产物只保留一个 APK；
- 通知权限改为选择本地视频时请求，拒绝不阻断核心链路。

仍需真实设备资源才能完成的事项：Google/Xiaomi 每个 OTA 的合法 adapter、离线签名 Profile
和完整真机验收。公开仓库不伪造这些结果。
