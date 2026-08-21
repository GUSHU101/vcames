#!/system/bin/sh

STATE_DIR="/data/adb/vcames"
module_version="$(sed -n 's/^version=//p' "${0%/*}/module.prop" | head -n 1)"
echo "VCamES Root Bridge ${module_version:-unknown}"
echo "设备: $(getprop ro.product.model) ($(getprop ro.product.device))"
echo "厂商: $(getprop ro.product.manufacturer) / $(getprop ro.product.brand)"
echo "SoC: $(getprop ro.soc.manufacturer) $(getprop ro.soc.model) / $(getprop ro.board.platform)"
echo "Android: $(getprop ro.build.version.release) / API $(getprop ro.build.version.sdk)"
echo "Kernel: $(uname -r)"
echo "SELinux: $(getenforce)"
echo "Root: $(id)"
if command -v ksud >/dev/null 2>&1 || [ -x /data/adb/ksud ]; then
  echo "Root manager: KernelSU（system/vendor 覆盖需要 metamodule）"
elif command -v magisk >/dev/null 2>&1; then
  echo "Root manager: Magisk"
else
  echo "Root manager: 未识别"
fi
echo

if [ -f "$STATE_DIR/status.txt" ]; then
  echo "状态: $(cat "$STATE_DIR/status.txt")"
else
  echo "状态: 尚未生成，请确认已重启"
fi

if [ -f "$STATE_DIR/disable-replacement" ]; then
  echo "BootGuard: SAFE_MODE（前后替换已禁用）"
  echo "确认适配器已修复后执行：su -c 'rm /data/adb/vcames/disable-replacement /data/adb/vcames/replacement-failures'"
else
  echo "BootGuard: 正常"
fi
case "$(cat "$STATE_DIR/status.txt" 2>/dev/null)" in
  SAFE_MODE_CORE_*)
    echo "Core supervisor: 本次启动已安全停止；重启后会重试，继续失败时请导出 diagnostics.zip"
    ;;
esac
[ ! -f "$STATE_DIR/process-stable.properties" ] || {
  echo "进程稳定记录（不代表相机内容已验证）："
  sed 's/^/  /' "$STATE_DIR/process-stable.properties"
}
echo
echo "VCamES socket proxy:"
if [ -x "${0%/*}/bin/vcames-socket-proxy" ]; then
  proxy_context="$(ls -Z "${0%/*}/bin/vcames-socket-proxy" 2>/dev/null)"
  case "$proxy_context" in
    *:vcames_proxy_exec:*) echo "  SELinux 标签正常：$proxy_context" ;;
    *) echo "  SELinux 标签异常：$proxy_context" ;;
  esac
else
  echo "  未安装"
fi

echo "video100:"
ls -lZ /dev/video100 2>/dev/null || echo "  不存在"
cat /sys/class/video4linux/video100/name 2>/dev/null || true
echo
echo "External Camera Provider:"
[ ! -f "${0%/*}/external-provider.properties" ] || {
  echo "  模块声明：$(cat "${0%/*}/external-provider.properties")"
}
provider="$(lshal 2>/dev/null | grep 'camera.provider.*external/0')"
[ -n "$provider" ] || provider="$(service list 2>/dev/null | grep 'camera.provider.*external/0')"
[ -z "$provider" ] && echo "  external/0 未注册" || echo "$provider"
echo
echo "Front/back replacement adapter:"
if [ -x "${0%/*}/bin/vcames-camera-adapter" ]; then
  echo "  已安装并通过构建哈希门禁；实际附着状态以 daemon replacement_attached 为准"
else
  echo "  未安装；仅可使用 external/0"
fi
echo
echo "最近日志:"
tail -n 30 "$STATE_DIR/root-service.log" 2>/dev/null || echo "  无日志"
