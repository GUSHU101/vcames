#!/system/bin/sh

MODDIR="${0%/*}"
STATE_DIR=/data/adb/vcames
LOG_FILE="$STATE_DIR/root-service.log"
STATUS_FILE="$STATE_DIR/status.txt"
SAFE_MODE_FILE="$STATE_DIR/disable-replacement"
FAILURE_FILE="$STATE_DIR/replacement-failures"
PACKAGE=io.github.gushu101.vcames
adapter_pid=""
daemon_pid=""
proxy_pid=""

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"
exec >>"$LOG_FILE" 2>&1
echo "=== VCamES Root Bridge $(date) ==="

write_status() {
  printf '%s\n' "$1" >"$STATUS_FILE"
  chmod 0600 "$STATUS_FILE"
  echo "status=$1"
}

cleanup() {
  [ -z "$adapter_pid" ] || kill "$adapter_pid" 2>/dev/null
  [ -z "$daemon_pid" ] || kill "$daemon_pid" 2>/dev/null
  [ -z "$proxy_pid" ] || kill "$proxy_pid" 2>/dev/null
}
trap cleanup EXIT

if [ -f "$SAFE_MODE_FILE" ]; then
  write_status SAFE_MODE_REPLACEMENT_DISABLED_OEM_PASSTHROUGH
  exit 0
fi
for file in bin/vcames-camera-adapter profile.json profile.sig profile.runtime.properties; do
  if [ ! -s "$MODDIR/$file" ]; then
    write_status NEEDS_SIGNED_EXACT_DEVICE_PACK
    exit 0
  fi
done

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

start_adapter() {
  "$MODDIR/bin/vcames-camera-adapter" --serve --socket vcames-camera-adapter \
    --profile "$MODDIR/profile.json" --signature "$MODDIR/profile.sig" &
  candidate=$!
  sleep 2
  kill -0 "$candidate" 2>/dev/null || return 1
  adapter_pid=$candidate
}

start_daemon() {
  "$MODDIR/bin/vcamesd" --allowed-uid "$app_uid" --drop-to-system \
    --control-socket vcamesd_private --frame-socket vcamesd_frames_private &
  candidate=$!
  sleep 2
  kill -0 "$candidate" 2>/dev/null || return 1
  daemon_pid=$candidate
}

start_proxy() {
  "$MODDIR/bin/vcames-socket-proxy" --allowed-uid "$app_uid" --drop-to-system \
    --public-control vcamesd --private-control vcamesd_private \
    --public-frames vcamesd_frames --private-frames vcamesd_frames_private &
  candidate=$!
  sleep 1
  kill -0 "$candidate" 2>/dev/null || return 1
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

record_adapter_failure() {
  failures="$(cat "$FAILURE_FILE" 2>/dev/null)"
  case "$failures" in ''|*[!0-9]*) failures=0 ;; esac
  failures=$((failures + 1))
  echo "$failures" >"$FAILURE_FILE"
  chmod 0600 "$FAILURE_FILE"
  if [ "$failures" -ge 3 ]; then
    : >"$SAFE_MODE_FILE"
    chmod 0600 "$SAFE_MODE_FILE"
  fi
}

start_with_retry start_adapter || {
  record_adapter_failure
  write_status REPLACEMENT_ADAPTER_START_FAILED_OEM_PASSTHROUGH
  exit 0
}
start_with_retry start_daemon || { write_status SAFE_MODE_CORE_DAEMON_FAILURE; exit 0; }
if ! chcon u:object_r:vcames_proxy_exec:s0 "$MODDIR/bin/vcames-socket-proxy" 2>/dev/null; then
  write_status SOCKET_PROXY_SELINUX_LABEL_FAILED
  exit 0
fi
case "$(ls -Z "$MODDIR/bin/vcames-socket-proxy" 2>/dev/null)" in
  *:vcames_proxy_exec:*) ;;
  *) write_status SOCKET_PROXY_SELINUX_CONTEXT_INVALID; exit 0 ;;
esac
start_with_retry start_proxy || { write_status SAFE_MODE_CORE_PROXY_FAILURE; exit 0; }
write_status READY_UNVERIFIED_EXACT_PROFILE

adapter_failures=0
core_failures=0
stable=0
while true; do
  sleep 5
  if ! kill -0 "$daemon_pid" 2>/dev/null; then
    daemon_pid=""
    core_failures=$((core_failures + 1))
    [ "$core_failures" -lt 3 ] || { write_status SAFE_MODE_CORE_DAEMON_FAILURE; break; }
    write_status DAEMON_RESTARTING_REAPPLY_CONFIGURATION
    start_with_retry start_daemon || continue
  fi
  if ! kill -0 "$proxy_pid" 2>/dev/null; then
    proxy_pid=""
    core_failures=$((core_failures + 1))
    [ "$core_failures" -lt 3 ] || { write_status SAFE_MODE_CORE_PROXY_FAILURE; break; }
    write_status SOCKET_PROXY_RESTARTING
    start_with_retry start_proxy || continue
  fi
  if ! kill -0 "$adapter_pid" 2>/dev/null; then
    adapter_pid=""
    adapter_failures=$((adapter_failures + 1))
    record_adapter_failure
    if [ -f "$SAFE_MODE_FILE" ]; then
      write_status SAFE_MODE_REPLACEMENT_DISABLED_OEM_PASSTHROUGH
      break
    fi
    write_status REPLACEMENT_ADAPTER_RESTARTING_OEM_PASSTHROUGH
    start_with_retry start_adapter || continue
    write_status ADAPTER_REATTACHING_UNVERIFIED
  fi
  stable=$((stable + 5))
  if [ "$stable" -ge 60 ]; then
    core_failures=0
    adapter_failures=0
    echo 0 >"$FAILURE_FILE"
  fi
done
