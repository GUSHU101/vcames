#!/system/bin/sh

MODDIR="${0%/*}"
STATE_DIR="/data/adb/vcames"
LOG_FILE="$STATE_DIR/root-service.log"
STATUS_FILE="$STATE_DIR/status.txt"
PACKAGE="io.github.gushu101.vcames"

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"
exec >>"$LOG_FILE" 2>&1

echo "=== VCamES Root Bridge $(date) ==="
echo "device=$(getprop ro.product.device) api=$(getprop ro.build.version.sdk) kernel=$(uname -r)"

write_status() {
  echo "$1" >"$STATUS_FILE"
  chmod 0600 "$STATUS_FILE"
  echo "status=$1"
}

provider_ready() {
  lshal 2>/dev/null | grep -q 'camera.provider.*external/0'
}

module_load_failed=false
if [ ! -e /dev/video100 ] && [ -f "$MODDIR/kernel/v4l2loopback.ko" ]; then
  echo "loading bundled v4l2loopback"
  if ! insmod "$MODDIR/kernel/v4l2loopback.ko" \
      video_nr=100 card_label=VCamES exclusive_caps=0 max_buffers=4; then
    module_load_failed=true
    echo "v4l2loopback load failed; kernel release/signature/KMI must match exactly"
  fi
fi

if [ ! -e /dev/video100 ]; then
  if [ "$module_load_failed" = true ]; then
    write_status "NEEDS_COMPATIBLE_KERNEL_MODULE"
  else
    write_status "NEEDS_V4L2LOOPBACK"
  fi
  exit 0
fi

restorecon /dev/video100 2>/dev/null
chown system:camera /dev/video100 2>/dev/null
chmod 0660 /dev/video100 2>/dev/null

app_uid=""
uid_attempts=0
while [ -z "$app_uid" ] && [ "$uid_attempts" -lt 60 ]; do
  app_uid="$(cmd package list packages -U "$PACKAGE" 2>/dev/null |
    sed -n 's/.* uid:\([0-9][0-9]*\).*/\1/p' | head -n 1)"
  if [ -z "$app_uid" ]; then
    uid_attempts=$((uid_attempts + 1))
    sleep 5
  fi
done

if [ -z "$app_uid" ]; then
  write_status "NEEDS_CONTROLLER_APK"
  echo "controller package was not found after 300 seconds"
  exit 0
fi

case "$app_uid" in
  ''|*[!0-9]*) write_status "INVALID_CONTROLLER_UID"; exit 0 ;;
esac
if [ "$app_uid" -lt 10000 ] || [ "$app_uid" -gt 2000000 ]; then
  write_status "INVALID_CONTROLLER_UID"
  exit 0
fi

echo "controller_uid=$app_uid"
"$MODDIR/bin/vcamesd" --allowed-uid "$app_uid" &
daemon_pid=$!
sleep 2
if ! kill -0 "$daemon_pid" 2>/dev/null; then
  write_status "DAEMON_START_FAILED"
  exit 0
fi

if ! provider_ready; then
  provider=""
  if [ -x "$MODDIR/bin/external-camera-provider" ]; then
    provider="$MODDIR/bin/external-camera-provider"
  elif [ -x /vendor/bin/hw/android.hardware.camera.provider@2.4-external-service ]; then
    provider="/vendor/bin/hw/android.hardware.camera.provider@2.4-external-service"
  fi
  if [ -n "$provider" ]; then
    echo "starting external camera provider: $provider"
    "$provider" &
    provider_pid=$!
    count=0
    while [ "$count" -lt 10 ] && ! provider_ready; do
      sleep 1
      count=$((count + 1))
    done
    if ! provider_ready; then
      kill "$provider_pid" 2>/dev/null
    fi
  fi
fi

if ! provider_ready; then
  write_status "NEEDS_EXTERNAL_CAMERA_PROVIDER"
  echo "external/0 registration failed; the stock VINTF cache or matching provider is unavailable"
  exit 0
fi

write_status "READY"
wait "$daemon_pid"
