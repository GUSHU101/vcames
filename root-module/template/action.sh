#!/system/bin/sh

STATE_DIR=/data/adb/vcames
MODDIR="${0%/*}"
echo "VCamES Pixel 5 Runtime $(sed -n 's/^version=//p' "$MODDIR/module.prop" | head -n 1)"
echo "状态: $(cat "$STATE_DIR/status.txt" 2>/dev/null || printf UNKNOWN)"
echo "SELinux: $(getenforce)"
echo "video100: $([ -c /dev/video100 ] && printf ready || printf missing)"
echo "global provider PID: $(pidof vcames-global-camera-provider 2>/dev/null || printf stopped)"
echo "legacy/0 HAL: $(lshal 2>/dev/null | grep -F 'android.hardware.camera.provider@2.4::ICameraProvider/legacy/0' | head -n 1)"
echo "FFmpeg: $([ -x "$MODDIR/bin/ffmpeg" ] && printf ready || printf missing)"
echo "daemon PID: $(pidof vcamesd 2>/dev/null || printf stopped)"
echo "proxy PID: $(pidof vcames-socket-proxy 2>/dev/null || printf stopped)"
echo "最近日志:"
tail -n 16 "$STATE_DIR/root-service.log" 2>/dev/null || echo "  no log"
echo "停用模块: touch /data/adb/modules/vcames_root_bridge/disable && reboot"
