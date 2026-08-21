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
proxy_pid=""
module_version="$(sed -n 's/^version=//p' "$MODDIR/module.prop" | head -n 1)"
[ -n "$module_version" ] || module_version="unknown"

cleanup() {
  [ -z "$daemon_pid" ] || kill "$daemon_pid" 2>/dev/null
  [ -z "$proxy_pid" ] || kill "$proxy_pid" 2>/dev/null
  [ -z "$adapter_pid" ] || kill "$adapter_pid" 2>/dev/null
  [ -z "$provider_pid" ] || kill "$provider_pid" 2>/dev/null
}
trap cleanup EXIT

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"
exec >>"$LOG_FILE" 2>&1
rm -f "$STABLE_FILE"

echo "=== VCamES Root Bridge $module_version $(date) ==="
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

start_adapter() {
  "$MODDIR/bin/vcames-camera-adapter" --serve \
    --socket vcames-camera-adapter \
    --manifest "$MODDIR/compatibility.properties" &
  candidate_pid=$!
  sleep 2
  if ! kill -0 "$candidate_pid" 2>/dev/null; then
    return 1
  fi
  adapter_pid="$candidate_pid"
  return 0
}

start_daemon() {
  "$MODDIR/bin/vcamesd" --allowed-uid "$app_uid" --drop-to-system \
    --control-socket vcamesd_private \
    --frame-socket vcamesd_frames_private &
  candidate_pid=$!
  sleep 2
  if ! kill -0 "$candidate_pid" 2>/dev/null; then
    return 1
  fi
  daemon_pid="$candidate_pid"
  return 0
}

start_proxy() {
  "$MODDIR/bin/vcames-socket-proxy" \
    --allowed-uid "$app_uid" \
    --drop-to-system \
    --public-control vcamesd \
    --private-control vcamesd_private \
    --public-frames vcamesd_frames \
    --private-frames vcamesd_frames_private &
  candidate_pid=$!
  sleep 1
  if ! kill -0 "$candidate_pid" 2>/dev/null; then
    return 1
  fi
  proxy_pid="$candidate_pid"
  return 0
}

start_provider() {
  [ -x "$MODDIR/bin/external-camera-provider" ] || return 1
  echo "starting external camera provider: $MODDIR/bin/external-camera-provider"
  "$MODDIR/bin/external-camera-provider" &
  candidate_pid=$!
  count=0
  while [ "$count" -lt 10 ] && ! provider_ready; do
    if ! kill -0 "$candidate_pid" 2>/dev/null; then
      return 1
    fi
    sleep 1
    count=$((count + 1))
  done
  if ! provider_ready; then
    kill "$candidate_pid" 2>/dev/null
    return 1
  fi
  provider_pid="$candidate_pid"
  return 0
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
  adapter_attempts=0
  adapter_initial_backoff=1
  while [ -z "$adapter_pid" ] && [ "$adapter_attempts" -lt 3 ] && \
      [ ! -f "$SAFE_MODE_FILE" ]; do
    adapter_attempts=$((adapter_attempts + 1))
    if ! start_adapter; then
      record_adapter_failure
      adapter_pid=""
      if [ "$adapter_attempts" -lt 3 ] && [ ! -f "$SAFE_MODE_FILE" ]; then
        sleep "$adapter_initial_backoff"
        adapter_initial_backoff=$((adapter_initial_backoff * 2))
      fi
    fi
  done
  if [ -z "$adapter_pid" ]; then
    adapter_available=false
    if [ "$video_available" = false ]; then
      if [ -f "$SAFE_MODE_FILE" ]; then
        write_status "SAFE_MODE_REPLACEMENT_DISABLED"
      else
        write_status "REPLACEMENT_ADAPTER_START_FAILED"
      fi
      exit 0
    fi
  fi
fi

daemon_attempts=0
daemon_initial_backoff=1
while [ -z "$daemon_pid" ] && [ "$daemon_attempts" -lt 3 ]; do
  daemon_attempts=$((daemon_attempts + 1))
  if ! start_daemon; then
    write_status "DAEMON_START_RETRYING"
    [ "$daemon_attempts" -ge 3 ] || sleep "$daemon_initial_backoff"
    daemon_initial_backoff=$((daemon_initial_backoff * 2))
  fi
done
if [ -z "$daemon_pid" ]; then
  write_status "SAFE_MODE_CORE_DAEMON_START_FAILURE"
  exit 0
fi

if ! chcon u:object_r:vcames_proxy_exec:s0 "$MODDIR/bin/vcames-socket-proxy" 2>/dev/null; then
  write_status "SOCKET_PROXY_SELINUX_LABEL_FAILED"
  exit 0
fi
proxy_context="$(ls -Z "$MODDIR/bin/vcames-socket-proxy" 2>/dev/null)"
case "$proxy_context" in *:vcames_proxy_exec:*) ;; *)
  write_status "SOCKET_PROXY_SELINUX_CONTEXT_INVALID"
  exit 0 ;;
esac
proxy_attempts=0
proxy_initial_backoff=1
while [ -z "$proxy_pid" ] && [ "$proxy_attempts" -lt 3 ]; do
  proxy_attempts=$((proxy_attempts + 1))
  if ! start_proxy; then
    write_status "SOCKET_PROXY_START_RETRYING"
    [ "$proxy_attempts" -ge 3 ] || sleep "$proxy_initial_backoff"
    proxy_initial_backoff=$((proxy_initial_backoff * 2))
  fi
done
if [ -z "$proxy_pid" ]; then
  write_status "SAFE_MODE_CORE_PROXY_START_FAILURE"
  exit 0
fi

if [ "$video_available" = true ] && ! provider_ready && \
    [ -x "$MODDIR/bin/external-camera-provider" ]; then
  provider_attempts=0
  provider_initial_backoff=1
  while ! provider_ready && [ "$provider_attempts" -lt 3 ]; do
    provider_attempts=$((provider_attempts + 1))
    if ! start_provider; then
      provider_pid=""
      [ "$provider_attempts" -ge 3 ] || sleep "$provider_initial_backoff"
      provider_initial_backoff=$((provider_initial_backoff * 2))
    fi
  done
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
adapter_stable_seconds=0
adapter_backoff_seconds=1
core_stable_seconds=0
core_failures=0
core_backoff_seconds=1
provider_failures=0
provider_backoff_seconds=1
while true; do
  sleep 5
  if ! kill -0 "$daemon_pid" 2>/dev/null; then
    daemon_pid=""
    stable_seconds=0
    stable_recorded=false
    rm -f "$STABLE_FILE"
    core_stable_seconds=0
    core_failures=$((core_failures + 1))
    if [ "$core_failures" -ge 3 ]; then
      write_status "SAFE_MODE_CORE_DAEMON_FAILURE"
      break
    fi
    write_status "DAEMON_RESTARTING_ATTACHED_CONFIGURATION_LOST"
    sleep "$core_backoff_seconds"
    if start_daemon; then
      echo "vcamesd restarted; controller must reapply the saved media configuration"
      write_status "CORE_RECOVERED_REAPPLY_CONFIGURATION"
    fi
    core_backoff_seconds=$((core_backoff_seconds * 2))
    [ "$core_backoff_seconds" -le 30 ] || core_backoff_seconds=30
    continue
  fi
  if ! kill -0 "$proxy_pid" 2>/dev/null; then
    proxy_pid=""
    stable_seconds=0
    stable_recorded=false
    rm -f "$STABLE_FILE"
    core_stable_seconds=0
    core_failures=$((core_failures + 1))
    if [ "$core_failures" -ge 3 ]; then
      write_status "SAFE_MODE_CORE_PROXY_FAILURE"
      break
    fi
    write_status "SOCKET_PROXY_RESTARTING"
    sleep "$core_backoff_seconds"
    if start_proxy; then
      echo "socket proxy restarted"
      write_status "CORE_RECOVERED_REAPPLY_CONFIGURATION"
    fi
    core_backoff_seconds=$((core_backoff_seconds * 2))
    [ "$core_backoff_seconds" -le 30 ] || core_backoff_seconds=30
    continue
  fi
  core_stable_seconds=$((core_stable_seconds + 5))
  if [ "$core_stable_seconds" -ge 60 ]; then
    core_failures=0
    core_backoff_seconds=1
  fi

  if [ "$video_available" = true ] && ! provider_ready; then
    external_ready=false
    if [ -x "$MODDIR/bin/external-camera-provider" ]; then
      provider_failures=$((provider_failures + 1))
      if [ "$provider_failures" -le 3 ]; then
        write_status "EXTERNAL_PROVIDER_RESTARTING"
        if [ -n "$provider_pid" ]; then
          kill "$provider_pid" 2>/dev/null
          provider_pid=""
        fi
        sleep "$provider_backoff_seconds"
        if start_provider; then
          external_ready=true
          provider_failures=0
          provider_backoff_seconds=1
          write_status "READY_EXTERNAL_PROVIDER_RECOVERED_UNVERIFIED"
        else
          provider_pid=""
          provider_backoff_seconds=$((provider_backoff_seconds * 2))
          [ "$provider_backoff_seconds" -le 30 ] || provider_backoff_seconds=30
        fi
      elif [ -n "$adapter_pid" ]; then
        write_status "ADAPTER_AVAILABLE_EXTERNAL_PROVIDER_SAFE_MODE"
      else
        write_status "SAFE_MODE_EXTERNAL_PROVIDER_FAILURE"
      fi
    fi
  elif [ "$video_available" = true ]; then
    external_ready=true
    provider_failures=0
    provider_backoff_seconds=1
  fi

  stable_seconds=$((stable_seconds + 5))
  if [ -n "$adapter_pid" ]; then
    if kill -0 "$adapter_pid" 2>/dev/null; then
      adapter_stable_seconds=$((adapter_stable_seconds + 5))
      if [ "$adapter_stable_seconds" -ge 60 ]; then
        echo 0 >"$FAILURE_FILE"
        chmod 0600 "$FAILURE_FILE"
        adapter_backoff_seconds=1
      fi
    else
      echo "replacement adapter terminated unexpectedly"
      record_adapter_failure
      adapter_pid=""
      adapter_stable_seconds=0
      if [ -f "$SAFE_MODE_FILE" ]; then
        if [ "$external_ready" = true ]; then
          write_status "READY_EXTERNAL_SAFE_MODE_REPLACEMENT_DISABLED"
        else
          write_status "SAFE_MODE_REPLACEMENT_DISABLED"
        fi
      else
        if [ "$external_ready" = true ]; then
          write_status "READY_EXTERNAL_REPLACEMENT_ADAPTER_RESTARTING"
        else
          write_status "REPLACEMENT_ADAPTER_RESTARTING"
        fi
        while [ -z "$adapter_pid" ] && [ ! -f "$SAFE_MODE_FILE" ] && \
            kill -0 "$daemon_pid" 2>/dev/null && kill -0 "$proxy_pid" 2>/dev/null; do
          sleep "$adapter_backoff_seconds"
          if start_adapter; then
            echo "replacement adapter restarted; daemon health monitor will reattach FrameBus"
            if [ "$external_ready" = true ]; then
              write_status "READY_EXTERNAL_ADAPTER_REATTACHING_UNVERIFIED"
            else
              write_status "ADAPTER_REATTACHING_UNVERIFIED"
            fi
            adapter_backoff_seconds=1
          else
            record_adapter_failure
            adapter_pid=""
            adapter_backoff_seconds=$((adapter_backoff_seconds * 2))
            [ "$adapter_backoff_seconds" -le 30 ] || adapter_backoff_seconds=30
          fi
        done
        if [ -f "$SAFE_MODE_FILE" ]; then
          if [ "$external_ready" = true ]; then
            write_status "READY_EXTERNAL_SAFE_MODE_REPLACEMENT_DISABLED"
          else
            write_status "SAFE_MODE_REPLACEMENT_DISABLED"
          fi
        fi
      fi
    fi
  fi
  if [ "$stable_seconds" -ge 60 ] && [ "$stable_recorded" = false ]; then
    {
      echo "version=$module_version"
      echo "device=$(getprop ro.product.device)"
      echo "api=$(getprop ro.build.version.sdk)"
      echo "fingerprint_sha256=$(printf '%s' "$(getprop ro.build.fingerprint)" | sha256sum | cut -d' ' -f1)"
      echo "verification=unverified-process-stability-only"
    } >"$STABLE_FILE"
    chmod 0600 "$STABLE_FILE"
    stable_recorded=true
  fi
done
