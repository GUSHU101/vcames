# Validation plan

每个 Pixel 5 OTA/内核组合单独执行：

1. 保存安装前的 `lshal`、CameraManager ID/facing/orientation、原厂 init service 和 SELinux 状态；
2. 核验 signed Profile、`.ko`、Provider、FFmpeg、许可清单及系统栈哈希；
3. 验证全部网络协议样例、断流重连、首帧/无帧超时、错误凭据不进入日志；
4. 验证本地 H.264/H.265/VP9 视频循环、旋转、镜像、固定 1280×720@30 输出和时间戳；
5. 对 camera 0、1 和 0+1 并发执行 Camera1/Camera2/CameraX 预览、拍照、录像；
6. 验证常用视频通话、直播、扫码和系统相机应用均自动看到替换画面；
7. 分别 kill Provider、daemon、proxy，移除 video100，确认状态与 OEM 恢复路径；
8. 尝试错误 Profile/签名/哈希/OTA、API 29/35、非 redfin、非控制器 socket UID，均应失败关闭；
9. 重启、模块禁用与卸载后原厂相机恢复；执行 100 次循环和 8 小时压力；
10. 报告记录构建身份、全部 SHA-256、测试应用版本、日志和逐项 PASS/FAIL。
