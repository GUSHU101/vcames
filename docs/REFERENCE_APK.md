# 参考 APK 静态分析说明

用户提供文件：`Vcam_虚拟相机安装包.apk`

- SHA-256：`769083E99487249F2377597F51D3A0FD91F875C39F62AA5EB8C7BC53272739EB`
- package：`com.telegram.a1064`
- version：`2.0.40`
- min/target/compile SDK：25 / 30 / 31

仅做了 Manifest、资源和本地库字符串的静态检查，没有运行 APK。可复用的产品需求包括：
本地 MP4、开始/停止、镜像、旋转、画面裁切/尺寸、浮层状态思路和相机预览思路。

以下行为明确没有移植：

- ShadowHook 与私有 Camera/Camera3 符号 hook；
- root 注入、`setenforce 0` 和向第三方进程加载代理库；
- 位置权限、旧式全盘存储权限、激活/CD-Key/二维码联网后端；
- APK 内任何私有 `.so`、图像、文本和服务地址。

VCamES 以公开 AOSP External Camera Provider 和 V4L2 接口重新实现同类能力。因此参考 APK
不是项目构建依赖，也不会被提交到 Git 仓库。
