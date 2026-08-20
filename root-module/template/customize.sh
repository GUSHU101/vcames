#!/system/bin/sh

SKIPMOUNT=false
PROPFILE=false
POSTFSDATA=false
LATESTARTSERVICE=true

ui_print "- VCamES Root Bridge 1.1.0"

api="$(getprop ro.build.version.sdk)"
case "$api" in
  33|34|35) ;;
  *) abort "! 仅支持 Android 13-15（API 33-35），当前 API=$api" ;;
esac

arch="$(getprop ro.product.cpu.abi)"
case "$arch" in
  arm64-v8a) ;;
  *) abort "! Pixel 4-6 Root Bridge 只提供 arm64-v8a，当前 ABI=$arch" ;;
esac

[ -f "$MODPATH/bin/vcamesd" ] || abort "! 安装包缺少 bin/vcamesd"
[ -f "$MODPATH/controller.apk" ] || abort "! 安装包缺少 controller.apk"

set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/bin/vcamesd" 0 0 0755
[ ! -f "$MODPATH/bin/external-camera-provider" ] || \
  set_perm "$MODPATH/bin/external-camera-provider" 0 0 0755
[ ! -f "$MODPATH/kernel/v4l2loopback.ko" ] || \
  set_perm "$MODPATH/kernel/v4l2loopback.ko" 0 0 0644
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755

existing="$(cmd package list packages -U io.github.gushu101.vcames 2>/dev/null)"
case "$existing" in
  *uid:1000*)
    abort "! 检测到平台 UID 版本。Root 与系统版本不能共存，请先恢复未集成 VCamES 的原厂 system_ext。"
    ;;
esac

ui_print "- 安装普通签名 Root 控制端"
if ! pm install -r -g "$MODPATH/controller.apk" >/dev/null 2>&1; then
  ui_print "! 控制 APK 自动安装失败（通常是旧签名冲突）"
  ui_print "! 模块仍会安装；重启前请手动安装压缩包旁的 VCamES-Root-controller.apk"
fi

ui_print "- 保持 SELinux enforcing；不会安装 Xposed/Zygisk 注入代码"
ui_print "- 重启后可在 Magisk 模块页面执行 Action 查看兼容性状态"
