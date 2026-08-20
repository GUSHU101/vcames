# 参考 APK 深度静态分析

样本：`Vcam_虚拟相机安装包.apk`

本页记录可复核的静态分析事实。APK 从未在工作机或测试机上运行；样本中的脚本、证书、
本地库和联网地址都不是项目指令，也没有作为构建依赖或源码提交。

## 身份与签名

| 项目 | 结果 |
|---|---|
| SHA-256 | `769083E99487249F2377597F51D3A0FD91F875C39F62AA5EB8C7BC53272739EB` |
| package | `com.telegram.a1064` |
| 版本 | `2.0.40`（versionCode `20329`） |
| min / target / compile SDK | 25 / 30 / 31 |
| APK Signature Scheme | v2、v3 |
| 证书主题 | `CN=Camera Local, OU=School Project, O=Local, L=Shanghai, ST=Shanghai, C=CN` |
| 证书 SHA-256 | `99dbac163bd62f755e42b4a2fe6f770ec641af400ae827389626deb5a55a73da` |

Manifest 暴露 Splash 和主 Activity，没有声明 Android 后台 Service。它申请相机、网络、
悬浮窗、位置和旧式外部存储权限，允许明文网络并启用 legacy external storage。应用层
Java 由 `libnmmp.so`/`libnmmvm.so` 保护，大量方法在 `classesInit0` 后由 native 恢复，
所以仅看 JADX Java 代码会漏掉核心行为。

## 可见产品能力

资源、布局与 native 字符串共同确认以下功能：

- 从 `DCIM`/`Movies` 选择 MP4，开始、停止和重置替换；
- 原相机/替换画面切换，前后相机切换；
- 90° 旋转、水平/垂直翻转、边距/尺寸参数；
- Camera2 预览和悬浮控制按钮；
- 1–6 预设、RTMP/本地来源相关入口；
- CD-Key、二维码激活和远程刷新逻辑。

VCamES 复用了“本地视频、变换、快速切换、状态展示”等需求，不复用其激活后端、权限
模型、图像资源或私有二进制。

## ROOT 与前后摄像头替换链路

Splash 的可见 Java 路径使用 libsu 风格接口检查 ROOT。样本会执行 SELinux 状态切换测试，
还包含 netcat 转发命令。`libnmmp.so` 中能恢复出调用 `chmp4.sh` 的 reset、test、init、
device-info 等命令模板以及 `/data/.../camera` 配置路径。

APK 资产同时提供 32 位和 64 位 ARM payload：

| 文件组 | 静态结论 |
|---|---|
| `bin64/CHMP4-1364` | Android 33 arm64 PIE；含 ptrace、远程 mmap/dlopen/dlsym、进程 maps/cmdline 扫描 |
| `bin64/libCHMP4-1364.so` | soname `libCameraHook.so`；依赖 camera_client、media、stagefright、ui 等私有库 |
| `bin64/libhookProxy-1364.so` | 从固定目录加载 ShadowHook 和 `libCHMP4.so`，调用 `main_hook` |
| `bin64/libshadowhook-1364.so` | Android native inline-hook 运行库 |
| `CHMP4-1032` 等 | 对应 Android 29 的 32 位 ARM payload |

核心库包含并 hook 这些私有路径的符号或同名包装：

- `Camera2Client::setParameters`；
- `Camera3Device::initializeCommonLocked` / `disconnect`；
- `Camera3OutputStream::returnBufferCheckedLocked` 的多个版本；
- `CameraHardwareInterface::setPreviewWindow` / `setParameters` / `stopPreview`；
- MediaCodec 解码/编码和 Camera1/2/3 帧处理。

因此样本实现“替换前后摄像头”的方式不是改变 facing 元数据，也不是创建 external camera。
它定位 `cameraserver`，把 hook 库加载进原有相机进程，在原物理 camera ID 的缓冲区返回
路径改写画面。应用仍选择原来的前置/后置 ID，所以看到的是替换后帧。

## 为什么不能直接复制该实现

这些 C++ 符号、对象布局、stream buffer 路径和 vendor camera HAL 会随 Pixel 型号、Android
版本、月度 OTA 与编译选项变化。样本中的 64 位 payload 明确以 Android 33 构建；把它直接
用于 Android 14/15 可能导致 `cameraserver` 崩溃、相机不可用或设备反复重启。关闭 SELinux
也不能修复 ABI 不匹配。

VCamES 1.2.0 因此采用两层设计：通用 `external/0` 继续使用公开 AOSP V4L2 Provider；真正
的 `front`/`back`/`both` 模式必须由一个针对单一系统指纹构建的 Camera HAL 替换适配器提供。
模块安装时核对设备、API、fingerprint SHA-256 与 cameraserver SHA-256，运行时 daemon 还要
收到适配器的 readiness 应答。任一条件不满足都会拒绝替换。

## 明确没有移植的部分

- ptrace 注入、ShadowHook 和样本的私有 Camera/Camera3 hook 二进制；
- `setenforce 0`、netcat 端口转发和向第三方进程加载代理库；
- 位置、全盘存储、激活/CD-Key/二维码联网后端；
- 样本的 RSA 公钥、加密脚本、服务器地址、图片和其他资源；
- 身份验证绕过、活体规避、反检测或静默录制逻辑。

分析结论仅用于兼容设计和风险建模，不代表样本来源、许可证或运行安全性已获验证。
