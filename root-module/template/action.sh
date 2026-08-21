#!/system/bin/sh

STATE_DIR=/data/adb/vcames
MODDIR="${0%/*}"
echo "VCamES Root Bridge $(sed -n 's/^version=//p' "$MODDIR/module.prop" | head -n 1)"
echo "状态: $(cat "$STATE_DIR/status.txt" 2>/dev/null || printf UNKNOWN)"
echo "SELinux: $(getenforce)"
echo "daemon PID: $(pidof vcamesd 2>/dev/null || printf stopped)"
echo "proxy PID: $(pidof vcames-socket-proxy 2>/dev/null || printf stopped)"
echo "adapter PID: $(pidof vcames-camera-adapter 2>/dev/null || printf stopped)"
if [ -f "$STATE_DIR/disable-replacement" ]; then
  echo "BootGuard: SAFE_MODE"
  echo "修复适配包后恢复: su -c 'rm -f /data/adb/vcames/disable-replacement /data/adb/vcames/replacement-failures'"
else
  echo "BootGuard: normal"
fi
echo "最近错误:"
tail -n 12 "$STATE_DIR/root-service.log" 2>/dev/null || echo "  no log"
echo "停用模块: touch /data/adb/modules/vcames_root_bridge/disable && reboot"
