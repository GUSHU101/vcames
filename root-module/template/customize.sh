#!/system/bin/sh

SKIPMOUNT=false
PROPFILE=false
POSTFSDATA=false
LATESTARTSERVICE=true

ui_print "- VCamES Root Bridge 1.2.0"

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

device="$(getprop ro.product.device)"
case "$device:$api" in
  flame:33|coral:33|sunfish:33) ;;
  bramble:33|bramble:34|redfin:33|redfin:34|barbet:33|barbet:34) ;;
  oriole:33|oriole:34|oriole:35|raven:33|raven:34|raven:35|bluejay:33|bluejay:34|bluejay:35) ;;
  *) abort "! 不支持的原厂 Pixel/API 组合：$device / API $api" ;;
esac

[ -f "$MODPATH/bin/vcamesd" ] || abort "! 安装包缺少 bin/vcamesd"
if [ ! -f "$MODPATH/controller.apk" ] && \
    ! cmd package path io.github.gushu101.vcames >/dev/null 2>&1; then
  abort "! standalone 模块需要先安装 VCamES Root APK"
fi

if [ -f "$MODPATH/bin/vcames-camera-adapter" ]; then
  [ -f "$MODPATH/compatibility.properties" ] || \
    abort "! 摄像头替换适配器缺少 compatibility.properties"
  expected_device="$(sed -n 's/^device=//p' "$MODPATH/compatibility.properties" | head -n 1)"
  expected_api="$(sed -n 's/^api=//p' "$MODPATH/compatibility.properties" | head -n 1)"
  expected_fingerprint="$(sed -n 's/^fingerprint_sha256=//p' "$MODPATH/compatibility.properties" | head -n 1)"
  expected_cameraserver="$(sed -n 's/^cameraserver_sha256=//p' "$MODPATH/compatibility.properties" | head -n 1)"
  actual_fingerprint="$(printf '%s' "$(getprop ro.build.fingerprint)" | sha256sum | cut -d' ' -f1)"
  actual_cameraserver="$(sha256sum /system/bin/cameraserver 2>/dev/null | cut -d' ' -f1)"
  [ "$expected_device" = "$device" ] || abort "! 替换适配器设备不匹配"
  [ "$expected_api" = "$api" ] || abort "! 替换适配器 API 不匹配"
  [ -n "$expected_fingerprint" ] && [ "$expected_fingerprint" = "$actual_fingerprint" ] || \
    abort "! 替换适配器系统指纹不匹配"
  [ -n "$expected_cameraserver" ] && [ "$expected_cameraserver" = "$actual_cameraserver" ] || \
    abort "! 替换适配器 cameraserver 不匹配"
  ui_print "- 前后摄像头替换适配器兼容性校验通过"
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/bin/vcamesd" 0 0 0755
[ ! -f "$MODPATH/bin/external-camera-provider" ] || \
  set_perm "$MODPATH/bin/external-camera-provider" 0 0 0755
[ ! -f "$MODPATH/bin/vcames-camera-adapter" ] || \
  set_perm "$MODPATH/bin/vcames-camera-adapter" 0 0 0755
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

if [ -f "$MODPATH/controller.apk" ]; then
  ui_print "- 安装普通签名 Root 控制端"
  if ! pm install -r -g "$MODPATH/controller.apk" >/dev/null 2>&1; then
    ui_print "! 控制 APK 自动安装失败（通常是旧签名冲突）"
    ui_print "! 模块仍会安装；重启前请手动安装压缩包旁的 VCamES-Root-controller.apk"
  fi
fi

ui_print "- 保持 SELinux enforcing；不会安装 Xposed/Zygisk 注入代码"
ui_print "- 重启后可在 Magisk 模块页面执行 Action 查看兼容性状态"
