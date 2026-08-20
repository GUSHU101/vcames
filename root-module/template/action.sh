#!/system/bin/sh

STATE_DIR="/data/adb/vcames"
echo "VCamES Root Bridge"
echo "设备: $(getprop ro.product.model) ($(getprop ro.product.device))"
echo "Android: $(getprop ro.build.version.release) / API $(getprop ro.build.version.sdk)"
echo "Kernel: $(uname -r)"
echo "SELinux: $(getenforce)"
echo "Root: $(id)"
echo

if [ -f "$STATE_DIR/status.txt" ]; then
  echo "状态: $(cat "$STATE_DIR/status.txt")"
else
  echo "状态: 尚未生成，请确认已重启"
fi

echo "video100:"
ls -lZ /dev/video100 2>/dev/null || echo "  不存在"
cat /sys/class/video4linux/video100/name 2>/dev/null || true
echo
echo "External Camera Provider:"
lshal 2>/dev/null | grep 'camera.provider.*external/0' || echo "  external/0 未注册"
echo
echo "最近日志:"
tail -n 30 "$STATE_DIR/root-service.log" 2>/dev/null || echo "  无日志"
