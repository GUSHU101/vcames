#!/system/bin/sh

MODDIR="${0%/*}"
STATE_DIR="/data/adb/vcames"
LOG_FILE="$STATE_DIR/root-service.log"
STATUS_FILE="$STATE_DIR/status.txt"
SAFE_MODE_FILE="$STATE_DIR/disable-replacement"
FAILURE_FILE="$STATE_DIR/replacement-failures"
STABLE_FILE="$STATE_DIR/process-stable.properties"
PACKAGE="io.github.gushu101.vcames"
adapter_pid=""
daemon_pid=""
provider_pid=""

cleanup() {
  [ -z "$daemon_pid" ] || kill "$daemon_pid" 2>/dev/null
  [ -z "$adapter_pid" ] || kill "$adapter_pid" 2>/dev/null
  [ -z "$provider_pid" ] || kill "$provider_pid" 2>/dev/null
}
trap cleanup EXIT

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"
exec >>"$LOG_FILE" 2>&1

echo "=== VCamES Root Bridge 2.1.0 $(date) ==="
echo "manufacturer=$(getprop ro.product.manufacturer) brand=$(getprop ro.product.brand) device=$(getprop ro.product.device) api=$(getprop ro.build.version.sdk) kernel=$(uname -r)"

write_status() {
  echo "$1" >"$STATUS_FILE"
  chmod 0600 "$STATUS_FILE"
  echo "status=$1"
}

provider_ready() {
  lshal 2>/dev/null | grep -q 'camera.provider.*external/0' ||
    service list 2>/dev/null | grep -q 'camera.provider.*external/0'
}

record_adapter_failure() {
  failures="$(cat "$FAILURE_FILE" 2>/dev/null)"
  case "$failures" in ''|*[!0-9]*) failures=0 ;; esac
  failures=$((failures + 1))
  echo "$failures" >"$FAILURE_FILE"
  chmod 0600 "$FAILURE_FILE"
  if [ "$failures" -ge 3 ]; then
    : >"$SAFE_MODE_FILE"
    chmod 0600 "$SAFE_MODE_FILE"
    echo "replacement adapter entered safe mode after $failures failures"
  fi
}

adapter_available=false
if [ -x "$MODDIR/bin/vcames-camera-adapter" ] && [ ! -f "$SAFE_MODE_FILE" ]; then
  adapter_available=true
elif [ -f "$SAFE_MODE_FILE" ]; then
  echo "replacement adapter disabled by BootGuard safe mode"
fi

module_load_failed=false
if [ ! -e /dev/video100 ] && [ -f "$MODDIR/kernel/v4l2loopback.ko" ]; then
  echo "loading bundled v4l2loopback"
  if ! insmod "$MODDIR/kernel/v4l2loopback.ko" \
      video_nr=100 card_label=VCamES exclusive_caps=0 max_buffers=4; then
    module_load_failed=true
    echo "v4l2loopback load failed; kernel release/signature/KMI must match exactly"
  fi
fi

video_available=false
if [ -e /dev/video100 ]; then
  video_available=true
  restorecon /dev/video100 2>/dev/null
  chown system:camera /dev/video100 2>/dev/null
  chmod 0660 /dev/video100 2>/dev/null
fi

if [ "$video_available" = false ] && [ "$adapter_available" = false ]; then
  if [ -f "$SAFE_MODE_FILE" ]; then
    write_status "SAFE_MODE_REPLACEMENT_DISABLED"
  elif [ "$module_load_failed" = true ]; then
    write_status "NEEDS_COMPATIBLE_KERNEL_MODULE_OR_ADAPTER"
  else
    write_status "NEEDS_V4L2LOOPBACK_OR_ADAPTER"
  fi
  exit 0
fi

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

case "$app_uid" in
  ''|*[!0-9]*) write_status "NEEDS_CONTROLLER_APK"; exit 0 ;;
esac
if [ "$app_uid" -lt 10000 ] || [ "$app_uid" -gt 2000000 ]; then
  write_status "INVALID_CONTROLLER_UID"
  exit 0
fi
echo "controller_uid=$app_uid"

if [ "$adapter_available" = true ]; then
  echo "starting exact-build front/back adapter with memfd FrameBus protocol"
  "$MODDIR/bin/vcames-camera-adapter" --serve \
    --socket vcames-camera-adapter \
    --manifest "$MODDIR/compatibility.properties" &
  adapter_pid=$!
  sleep 2
  if ! kill -0 "$adapter_pid" 2>/dev/null; then
    record_adapter_failure
    adapter_pid=""
    adapter_available=false
    if [ "$video_available" = false ]; then
      write_status "REPLACEMENT_ADAPTER_START_FAILED"
      exit 0
    fi
  fi
fi

"$MODDIR/bin/vcamesd" --allowed-uid "$app_uid" --drop-to-system &
daemon_pid=$!
sleep 2
if ! kill -0 "$daemon_pid" 2>/dev/null; then
  write_status "DAEMON_START_FAILED"
  exit 0
fi

if [ "$video_available" = true ] && ! provider_ready; then
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
      provider_pid=""
    fi
  fi
fi

external_ready=false
if [ "$video_available" = true ] && provider_ready; then
  external_ready=true
fi
if [ "$external_ready" = false ] && [ -z "$adapter_pid" ]; then
  write_status "NEEDS_EXTERNAL_CAMERA_PROVIDER"
  exit 0
fi

if [ -f "$SAFE_MODE_FILE" ] && [ "$external_ready" = true ]; then
  write_status "READY_EXTERNAL_SAFE_MODE_REPLACEMENT_DISABLED"
elif [ -n "$adapter_pid" ] && [ "$external_ready" = true ]; then
  write_status "READY_EXTERNAL_ADAPTER_AVAILABLE_UNVERIFIED"
elif [ -n "$adapter_pid" ]; then
  write_status "ADAPTER_AVAILABLE_UNVERIFIED"
else
  write_status "READY_EXTERNAL"
fi

stable_seconds=0
stable_recorded=false
while kill -0 "$daemon_pid" 2>/dev/null; do
  sleep 5
  stable_seconds=$((stable_seconds + 5))
  if [ -n "$adapter_pid" ] && ! kill -0 "$adapter_pid" 2>/dev/null; then
    echo "replacement adapter terminated unexpectedly"
    record_adapter_failure
    adapter_pid=""
    if [ -f "$SAFE_MODE_FILE" ]; then
      if [ "$external_ready" = true ]; then
        write_status "READY_EXTERNAL_SAFE_MODE_REPLACEMENT_DISABLED"
      else
        write_status "SAFE_MODE_REPLACEMENT_DISABLED"
      fi
    elif [ "$external_ready" = true ]; then
      write_status "READY_EXTERNAL_REPLACEMENT_ADAPTER_CRASHED"
    else
      write_status "REPLACEMENT_ADAPTER_CRASHED"
    fi
  fi
  if [ "$stable_seconds" -ge 60 ] && [ "$stable_recorded" = false ]; then
    {
      echo "version=2.1.0"
      echo "device=$(getprop ro.product.device)"
      echo "api=$(getprop ro.build.version.sdk)"
      echo "fingerprint_sha256=$(printf '%s' "$(getprop ro.build.fingerprint)" | sha256sum | cut -d' ' -f1)"
      echo "verification=unverified-process-stability-only"
    } >"$STABLE_FILE"
    chmod 0600 "$STABLE_FILE"
    [ -z "$adapter_pid" ] || echo 0 >"$FAILURE_FILE"
    stable_recorded=true
  fi
done
write_status "DAEMON_STOPPED"
