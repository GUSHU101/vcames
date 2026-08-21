# Controller APK

控制器是 minSdk 30 的普通 arm64 Android 应用。它只做四件事：检测 Pixel 5/API 和 `su` uid 0、
确认独立运行时状态、选择媒体/变换参数、通过 UID proxy 控制 daemon 并导出不含用户媒体的诊断。

界面没有应用包名或前后摄选择。网络地址支持协议 allowlist；本地文件通过 Storage Access Framework，
不会把任意文件路径交给 FFmpeg。APK 不包含 `.ko`、Provider、FFmpeg、ROOT 管理或模块安装代码。
