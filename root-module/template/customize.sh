#!/system/bin/sh

SKIPMOUNT=false
PROPFILE=false
POSTFSDATA=false
LATESTARTSERVICE=true

module_version="$(sed -n 's/^version=//p' "$MODPATH/module.prop" | head -n 1)"
[ -n "$module_version" ] || abort "! module.prop is missing version"
ui_print "- VCamES Root Bridge $module_version"

api="$(getprop ro.build.version.sdk)"
case "$api" in 30|31|32|33) ;; *) abort "! Android 11-13 only; current API=$api" ;; esac
[ "$(getprop ro.product.cpu.abi)" = arm64-v8a ] || abort "! arm64-v8a only"

identity="$(printf '%s|%s' "$(getprop ro.product.manufacturer)" \
  "$(getprop ro.product.brand)" | tr '[:upper:]' '[:lower:]')"
case "$identity" in
  *google*|*xiaomi*|*redmi*|*poco*) ;;
  *) abort "! This release supports Google and Xiaomi/Redmi/POCO only" ;;
esac

[ -f "$MODPATH/bin/vcamesd" ] || abort "! missing bin/vcamesd"
[ -f "$MODPATH/bin/vcames-socket-proxy" ] || abort "! missing bin/vcames-socket-proxy"
[ -f "$MODPATH/device-probe.sh" ] || abort "! missing device-probe.sh"
if ! cmd package path io.github.gushu101.vcames >/dev/null 2>&1; then
  abort "! Install the VCamES APK before installing its Root Bridge"
fi

profile_value() {
  sed -n "s/^$1=//p" "$MODPATH/profile.runtime.properties" | head -n 1
}

if [ -f "$MODPATH/bin/vcames-camera-adapter" ]; then
  [ -s "$MODPATH/profile.json" ] || abort "! adapter requires profile.json"
  [ -s "$MODPATH/profile.sig" ] || abort "! adapter requires profile.sig"
  [ -s "$MODPATH/profile.runtime.properties" ] || \
    abort "! adapter requires generated profile.runtime.properties"
  expected_profile_hash="$(profile_value profile_sha256)"
  [ "$(profile_value validation_status)" = VERIFIED ] || \
    abort "! Profile is not VERIFIED"
  actual_profile_hash="$(sha256sum "$MODPATH/profile.json" | cut -d' ' -f1)"
  [ "$expected_profile_hash" = "$actual_profile_hash" ] || abort "! Profile hash mismatch"
  actual_adapter_hash="$(sha256sum "$MODPATH/bin/vcames-camera-adapter" | cut -d' ' -f1)"
  [ "$(profile_value adapter_sha256)" = "$actual_adapter_hash" ] || \
    abort "! adapter hash mismatch"

  actual_probe="$MODPATH/.device-probe.actual"
  sh "$MODPATH/device-probe.sh" >"$actual_probe" || abort "! DeviceProbe failed"
  for key in vendor_family soc_family camera_hal_transport manufacturer product device api \
    system_fingerprint_sha256 vendor_fingerprint_sha256 cameraserver_sha256 \
    camera_provider_sha256 vendor_camera_libraries_sha256 graphics_stack_sha256 \
    compatibility_id; do
    expected="$(profile_value "$key")"
    actual="$(sed -n "s/^$key=//p" "$actual_probe" | head -n 1)"
    [ -n "$expected" ] || abort "! Profile projection missing $key"
    [ "$expected" = "$actual" ] || abort "! Exact Profile mismatch: $key"
  done
  rm -f "$actual_probe"
  ui_print "- exact compatibility_id and adapter hash matched"
else
  ui_print "! No signed device pack included; module will fail closed"
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/bin/vcamesd" 0 0 0755
set_perm "$MODPATH/bin/vcames-socket-proxy" 0 0 0755
set_perm "$MODPATH/device-probe.sh" 0 0 0755
[ ! -f "$MODPATH/bin/vcames-camera-adapter" ] || \
  set_perm "$MODPATH/bin/vcames-camera-adapter" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755

ui_print "- ordinary APK + explicit uid-0 grant; no system UID, Xposed or Zygisk"
ui_print "- SELinux remains enforcing; replacement fails closed to OEM camera"
