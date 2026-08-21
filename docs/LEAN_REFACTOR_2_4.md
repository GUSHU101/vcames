# 3.2 logic reset

3.2 删除了按包名 Camera Injection、front/back 选择器、额外 external camera 和 Android 11 特判。
唯一产品链路是 Pixel 5 `legacy/0` Provider 接管：camera 0/1 同时映射到 `/dev/video100`。

网络部分也从 HTTP 专用客户端改为受限 FFmpeg argv 管道，统一处理直播协议和容器。旧协议字段被
Profile v2、bridge schema 4 和 daemon protocol 5 取代，避免旧运行时静默接受新控制命令。
