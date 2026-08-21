#!/system/bin/sh

MODDIR="${0%/*}"
STATE_DIR=/data/adb/vcames
LOG_FILE="$STATE_DIR/root-service.log"
STATUS_FILE="$STATE_DIR/status.txt"
PACKAGE=io.github.gushu101.vcames
VIDEO_DEVICE=/dev/video100
PROVIDER_INTERFACE=android.hardware.camera.provider@2.4::ICameraProvider/legacy/0
provider_pid=""
daemon_pid=""
proxy_pid=""
loaded_module=false
oem_provider_stopped=false

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"
exec >>"$LOG_FILE" 2>&1
echo "=== VCamES Pixel 5 Global Provider $(date) ==="

write_status() {
  if [ "$(cat "$STATUS_FILE" 2>/dev/null)" = "$1" ]; then
    return
  fi
  printf '%s\n' "$1" >"$STATUS_FILE"
  chmod 0600 "$STATUS_FILE"
  echo "status=$1"
}

profile_value() {
  sed -n "s/^$1=//p" "$MODDIR/profile.runtime.properties" | head -n 1
}

bridge_value() {
  sed -n "s/^$1=//p" "$MODDIR/bridge.properties" | head -n 1
}

provider_server_pid() {
  lshal -ip 2>/dev/null | awk -v target="$PROVIDER_INTERFACE" \
    '$1 == target && $2 ~ /^[0-9]+$/ { print $2; exit }'
}

verify_oem_provider_identity() {
  expected_path="$(profile_value oem_provider_binary)"
  expected_hash="$(profile_value oem_provider_binary_sha256)"
  actual_pid="$(provider_server_pid)"
  case "$actual_pid" in ''|*[!0-9]*) return 1 ;; esac
  actual_path="$(readlink "/proc/$actual_pid/exe" 2>/dev/null)"
  [ "$actual_path" = "$expected_path" ] || return 1
  actual_hash="$(sha256sum "/proc/$actual_pid/exe" 2>/dev/null | cut -d' ' -f1)"
  [ "$actual_hash" = "$expected_hash" ]
}

stop_pid() {
  [ -z "$1" ] || kill "$1" 2>/dev/null
}

pid_alive() {
  [ -n "$1" ] || return 1
  kill -0 "$1" 2>/dev/null || return 1
  [ "$(awk '{print $3}' "/proc/$1/stat" 2>/dev/null)" != Z ]
}

wait_for_pid_exit() {
  candidate="$1"
  count=0
  while pid_alive "$candidate" && [ "$count" -lt 5 ]; do
    count=$((count + 1))
    sleep 1
  done
  ! pid_alive "$candidate"
}

stop_and_reap() {
  candidate="$1"
  [ -n "$candidate" ] || return
  stop_pid "$candidate"
  if ! wait_for_pid_exit "$candidate"; then
    kill -9 "$candidate" 2>/dev/null
  fi
  wait "$candidate" 2>/dev/null || true
}

restore_oem_provider() {
  [ "$oem_provider_stopped" = true ] || return
  service_name="$(profile_value oem_provider_service)"
  echo "restoring OEM camera provider service $service_name"
  count=0
  while lshal 2>/dev/null | grep -Fq "$PROVIDER_INTERFACE" && [ "$count" -lt 10 ]; do
    count=$((count + 1))
    sleep 1
  done
  setprop ctl.start "$service_name"
  count=0
  while [ "$(getprop "init.svc.$service_name")" != running ] && [ "$count" -lt 20 ]; do
    count=$((count + 1))
    sleep 1
  done
  if [ "$(getprop "init.svc.$service_name")" = running ] && \
     verify_oem_provider_identity; then
    oem_provider_stopped=false
    echo "OEM camera provider restored"
  else
    echo "ERROR: OEM camera provider restoration or identity verification failed"
  fi
}

cleanup() {
  stop_and_reap "$proxy_pid"
  stop_and_reap "$daemon_pid"
  stop_and_reap "$provider_pid"
  restore_oem_provider
  if [ "$loaded_module" = true ]; then
    rmmod v4l2loopback 2>/dev/null
  fi
}
trap cleanup EXIT
handle_shutdown_signal() {
  write_status SAFE_MODE_RUNTIME_STOPPING_RESTORING_OEM
  exit 0
}
trap handle_shutdown_signal HUP INT TERM

for file in bin/vcamesd bin/vcames-socket-proxy bin/vcames-global-camera-provider \
  bin/ffmpeg kernel/v4l2loopback.ko profile.json profile.sig ffmpeg.LICENSE.json \
  licenses/FFmpeg-LGPL-2.1.txt profile.runtime.properties bridge.properties; do
  if [ ! -s "$MODDIR/$file" ]; then
    write_status NEEDS_SIGNED_EXACT_DEVICE_PACK
    exit 0
  fi
done

if [ "$(profile_value replacement_scope)" != global-front-back ] || \
   [ "$(profile_value provider_interface)" != "$PROVIDER_INTERFACE" ] || \
   [ "$(profile_value back_camera_id)" != 0 ] || \
   [ "$(profile_value front_camera_id)" != 1 ]; then
  write_status INVALID_GLOBAL_PROVIDER_PROFILE
  exit 0
fi
oem_service="$(profile_value oem_provider_service)"
case "$oem_service" in
  ''|*[!A-Za-z0-9_.-]*) write_status INVALID_OEM_PROVIDER_SERVICE; exit 0 ;;
esac
oem_binary="$(profile_value oem_provider_binary)"
case "$oem_binary" in
  /vendor/bin/hw/*|/odm/bin/hw/*) ;;
  *) write_status INVALID_OEM_PROVIDER_BINARY; exit 0 ;;
esac
oem_binary_hash="$(profile_value oem_provider_binary_sha256)"
case "$oem_binary_hash" in
  ''|*[!0-9a-f]*) write_status INVALID_OEM_PROVIDER_BINARY_HASH; exit 0 ;;
  *) [ "${#oem_binary_hash}" -eq 64 ] || {
    write_status INVALID_OEM_PROVIDER_BINARY_HASH
    exit 0
  } ;;
esac

app_uid=""
attempt=0
while [ -z "$app_uid" ] && [ "$attempt" -lt 60 ]; do
  app_uid="$(cmd package list packages -U "$PACKAGE" 2>/dev/null | \
    sed -n 's/.* uid:\([0-9][0-9]*\).*/\1/p' | head -n 1)"
  [ -n "$app_uid" ] || { attempt=$((attempt + 1)); sleep 5; }
done
case "$app_uid" in ''|*[!0-9]*) write_status NEEDS_CONTROLLER_APK; exit 0 ;; esac
if [ "$app_uid" -lt 10000 ] || [ "$app_uid" -gt 2000000 ]; then
  write_status INVALID_CONTROLLER_UID
  exit 0
fi
if [ "$(bridge_value controller_package)" != "$PACKAGE" ]; then
  write_status CONTROLLER_PACKAGE_CONTRACT_MISMATCH
  exit 0
fi
controller_hash="$(bridge_value controller_apk_sha256)"
case "$controller_hash" in
  ''|*[!0-9a-f]*) write_status INVALID_CONTROLLER_APK_HASH; exit 0 ;;
  *) [ "${#controller_hash}" -eq 64 ] || {
    write_status INVALID_CONTROLLER_APK_HASH
    exit 0
  } ;;
esac
controller_apk="$(cmd package path "$PACKAGE" 2>/dev/null | \
  sed -n 's/^package:\(.*\/base\.apk\)$/\1/p' | head -n 1)"
if [ -z "$controller_apk" ] || [ ! -f "$controller_apk" ]; then
  write_status CONTROLLER_BASE_APK_MISSING
  exit 0
fi
if [ "$(sha256sum "$controller_apk" 2>/dev/null | cut -d' ' -f1)" != \
     "$controller_hash" ]; then
  write_status CONTROLLER_APK_HASH_MISMATCH
  exit 0
fi

ffmpeg_has() {
  mode="$1"
  name="$2"
  "$MODDIR/bin/ffmpeg" -hide_banner "$mode" 2>/dev/null | \
    sed 's/^[ .A-Z][ .A-Z]*//' | awk '{print $1}' | grep -qx "$name"
}
for protocol in http https httpproxy tls crypto ffrtmpcrypt ffrtmphttp \
  rtmp rtmps rtmpe rtmpt rtmpte \
  rtmpts srt rist rtp srtp udp tcp mmsh mmst; do
  ffmpeg_has -protocols "$protocol" || {
    write_status "FFMPEG_MISSING_PROTOCOL_$protocol"
    exit 0
  }
done
for demuxer in hls dash flv rtsp rtp mpegts mjpeg asf; do
  ffmpeg_has -demuxers "$demuxer" || {
    write_status "FFMPEG_MISSING_DEMUXER_$demuxer"
    exit 0
  }
done
for decoder in h264 hevc vp8 vp9 av1 mpeg4 mjpeg; do
  ffmpeg_has -decoders "$decoder" || {
    write_status "FFMPEG_MISSING_DECODER_$decoder"
    exit 0
  }
done
for filter in fps scale pad; do
  ffmpeg_has -filters "$filter" || {
    write_status "FFMPEG_MISSING_FILTER_$filter"
    exit 0
  }
done
ffmpeg_has -encoders mjpeg || { write_status FFMPEG_MISSING_MJPEG_ENCODER; exit 0; }

if [ "$(getprop "init.svc.$oem_service")" != running ]; then
  write_status OEM_PROVIDER_SERVICE_NOT_RUNNING
  exit 0
fi
if ! verify_oem_provider_identity; then
  write_status OEM_PROVIDER_IDENTITY_MISMATCH
  exit 0
fi
if ! lshal 2>/dev/null | grep -Fq "$PROVIDER_INTERFACE"; then
  write_status OEM_LEGACY_PROVIDER_NOT_REGISTERED
  exit 0
fi
camera_dump="$(dumpsys media.camera 2>/dev/null)"
printf '%s' "$camera_dump" | grep -Eq '(Camera ID:?|Camera device|Device)[[:space:]]*0([^0-9]|$)' || {
  write_status OEM_BACK_CAMERA_ID_0_MISSING
  exit 0
}
printf '%s' "$camera_dump" | grep -Eq '(Camera ID:?|Camera device|Device)[[:space:]]*1([^0-9]|$)' || {
  write_status OEM_FRONT_CAMERA_ID_1_MISSING
  exit 0
}

if [ ! -d /sys/module/v4l2loopback ]; then
  if ! insmod "$MODDIR/kernel/v4l2loopback.ko" \
      video_nr=100 card_label=VCamES exclusive_caps=0 max_buffers=4; then
    write_status V4L2LOOPBACK_LOAD_FAILED
    exit 0
  fi
  loaded_module=true
fi
attempt=0
while [ ! -c "$VIDEO_DEVICE" ] && [ "$attempt" -lt 20 ]; do
  attempt=$((attempt + 1))
  sleep 1
done
[ -c "$VIDEO_DEVICE" ] || { write_status V4L2_DEVICE_MISSING; exit 0; }
chown 1000:1006 "$VIDEO_DEVICE" 2>/dev/null || true
chmod 0660 "$VIDEO_DEVICE" || { write_status V4L2_DEVICE_PERMISSION_FAILED; exit 0; }

start_provider() {
  "$MODDIR/bin/vcames-global-camera-provider" &
  candidate=$!
  sleep 2
  pid_alive "$candidate" || return 1
  count=0
  while ! lshal 2>/dev/null | grep -Fq "$PROVIDER_INTERFACE" && [ "$count" -lt 10 ]; do
    count=$((count + 1))
    sleep 1
  done
  registration_pid="$(provider_server_pid)"
  [ "$registration_pid" = "$candidate" ] || {
    stop_and_reap "$candidate"
    return 1
  }
  provider_pid=$candidate
}

start_daemon() {
  "$MODDIR/bin/vcamesd" \
    --control-socket vcamesd_private --frame-socket vcamesd_frames_private \
    --ffmpeg-path "$MODDIR/bin/ffmpeg" --prime-global-camera &
  candidate=$!
  # vcamesd exits if its 720p placeholder cannot be encoded and written within
  # five seconds. Wait past that gate before allowing Provider registration.
  sleep 6
  pid_alive "$candidate" || return 1
  daemon_pid=$candidate
}

start_proxy() {
  "$MODDIR/bin/vcames-socket-proxy" --allowed-uid "$app_uid" --drop-to-system \
    --public-control vcamesd --private-control vcamesd_private \
    --public-frames vcamesd_frames --private-frames vcamesd_frames_private &
  candidate=$!
  sleep 1
  pid_alive "$candidate" || return 1
  proxy_pid=$candidate
}

start_with_retry() {
  command_name=$1
  count=0
  backoff=1
  while [ "$count" -lt 3 ]; do
    count=$((count + 1))
    "$command_name" && return 0
    sleep "$backoff"
    backoff=$((backoff * 2))
  done
  return 1
}

for pair in \
  "vcames_camera_provider_exec:$MODDIR/bin/vcames-global-camera-provider" \
  "vcames_proxy_exec:$MODDIR/bin/vcames-socket-proxy"; do
  context="${pair%%:*}"
  path="${pair#*:}"
  chcon "u:object_r:$context:s0" "$path" 2>/dev/null || {
    write_status SELINUX_EXEC_LABEL_FAILED
    exit 0
  }
  ls -Z "$path" 2>/dev/null | grep -Fq ":$context:" || {
    write_status SELINUX_EXEC_CONTEXT_INVALID
    exit 0
  }
done

setprop ctl.stop "$oem_service"
attempt=0
while [ "$(getprop "init.svc.$oem_service")" != stopped ] && [ "$attempt" -lt 20 ]; do
  attempt=$((attempt + 1))
  sleep 1
done
if [ "$(getprop "init.svc.$oem_service")" != stopped ]; then
  write_status OEM_PROVIDER_STOP_FAILED
  exit 0
fi
oem_provider_stopped=true
attempt=0
while lshal 2>/dev/null | grep -Fq "$PROVIDER_INTERFACE" && [ "$attempt" -lt 20 ]; do
  attempt=$((attempt + 1))
  sleep 1
done
if lshal 2>/dev/null | grep -Fq "$PROVIDER_INTERFACE"; then
  write_status OEM_PROVIDER_UNREGISTER_FAILED_RESTORING_OEM
  exit 0
fi

start_with_retry start_daemon || { write_status CORE_DAEMON_START_FAILED_RESTORING_OEM; exit 0; }
start_provider || { write_status GLOBAL_PROVIDER_START_FAILED_RESTORING_OEM; exit 0; }
start_with_retry start_proxy || { write_status CORE_PROXY_START_FAILED_RESTORING_OEM; exit 0; }
write_status READY_GLOBAL_FRONT_BACK

failures=0
while true; do
  sleep 5
  if ! pid_alive "$provider_pid" || \
     [ "$(provider_server_pid)" != "$provider_pid" ]; then
    write_status SAFE_MODE_GLOBAL_PROVIDER_LOST_RESTORING_OEM
    break
  fi
  failed=""
  if [ ! -c "$VIDEO_DEVICE" ]; then
    failed=v4l2
  elif ! pid_alive "$daemon_pid"; then
    daemon_pid=""
    failed=daemon
    start_with_retry start_daemon || true
  elif ! pid_alive "$proxy_pid"; then
    proxy_pid=""
    failed=proxy
    start_with_retry start_proxy || true
  fi
  if [ -n "$failed" ]; then
    failures=$((failures + 1))
    write_status "GLOBAL_RUNTIME_${failed}_RESTARTING"
    if [ "$failures" -ge 3 ]; then
      write_status "SAFE_MODE_${failed}_FAILED_RESTORING_OEM"
      break
    fi
  else
    failures=0
    write_status READY_GLOBAL_FRONT_BACK
  fi
done
