# Root stock integration

用户先在 Pixel 5 安装并配置 KernelSU/Magisk，再给 APK 的一次性 `su -c id -u` 请求授权。APK 不改变
ROOT 环境。独立模块在 late_start 阶段校验设备包并接管相机 Provider。
安装器会依据 KernelSU 的 `KSU=true` 环境把策略源域从 `magisk/magisk_file` 专门化为
`ksu/ksu_file`；模块不修改 `/system`，因此不依赖 KernelSU metamodule。

精确包包含 `kernel/v4l2loopback.ko`、`bin/vcames-global-camera-provider`、`bin/ffmpeg`、
`ffmpeg.LICENSE.json`、`licenses/FFmpeg-LGPL-2.1.txt`、signed `profile.json` 和 runtime projection。缺一项即
`NEEDS_SIGNED_EXACT_DEVICE_PACK`。

运行时只停止 Profile 中记录且已校验的 OEM init service。替换 Provider 无法注册或运行中丢失时，
trap 先终止 VCamES 进程，再 `ctl.start` 恢复同一 OEM service。模块禁用后下次开机不会执行接管。
