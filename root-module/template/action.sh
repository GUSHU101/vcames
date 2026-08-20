#!/system/bin/sh

STATE_DIR="/data/adb/vcames"
echo "VCamES Root Bridge"
echo "设备: $(getprop ro.product.model) ($(getprop ro.product.device))"
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
[ ! -f "$STATE_DIR/last-known-good.properties" ] || {
  echo "Last known good:"
  sed 's/^/  /' "$STATE_DIR/last-known-good.properties"
}

echo "video100:"
ls -lZ /dev/video100 2>/dev/null || echo "  不存在"
cat /sys/class/video4linux/video100/name 2>/dev/null || true
echo
echo "External Camera Provider:"
provider="$(lshal 2>/dev/null | grep 'camera.provider.*external/0')"
[ -n "$provider" ] || provider="$(service list 2>/dev/null | grep 'camera.provider.*external/0')"
[ -z "$provider" ] && echo "  external/0 未注册" || echo "$provider"
echo
echo "Front/back replacement adapter:"
if [ -x "${0%/*}/bin/vcames-camera-adapter" ]; then
  echo "  已安装（完整构建哈希校验 + memfd FrameBus）"
else
  echo "  未安装；仅可使用 external/0"
fi
echo
echo "最近日志:"
tail -n 30 "$STATE_DIR/root-service.log" 2>/dev/null || echo "  无日志"
