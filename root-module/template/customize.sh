#!/system/bin/sh

SKIPMOUNT=false
PROPFILE=false
POSTFSDATA=false
LATESTARTSERVICE=true

module_version="$(sed -n 's/^version=//p' "$MODPATH/module.prop" | head -n 1)"
[ -n "$module_version" ] || abort "! module.prop is missing version"
ui_print "- VCamES Pixel 5 Runtime $module_version"

# KernelSU uses ksu/ksu_file where Magisk uses magisk/magisk_file. Both load
# the same module format and sepolicy.rule contract, so specialize the source
# domain before the manager applies the installed rule file at boot.
if [ "${KSU:-false}" = true ]; then
  sed -i 's/magisk_file/ksu_file/g; s/magisk/ksu/g' "$MODPATH/sepolicy.rule" || \
    abort "! unable to specialize SELinux rules for KernelSU"
  ui_print "- KernelSU module runtime detected"
else
  ui_print "- Magisk-compatible module runtime detected"
fi

api="$(getprop ro.build.version.sdk)"
case "$api" in 30|31|32|33|34) ;; *) abort "! Pixel 5 global Provider runtime requires Android 11-14 (API 30-34); current API=$api" ;; esac
[ "$(getprop ro.product.cpu.abi)" = arm64-v8a ] || abort "! arm64-v8a only"

[ "$(getprop ro.product.manufacturer | tr '[:upper:]' '[:lower:]')" = google ] || \
  abort "! Google Pixel 5 only"
[ "$(getprop ro.product.device)" = redfin ] || abort "! Pixel 5 device codename redfin required"
[ "$(getprop ro.product.name)" = redfin ] || abort "! Pixel 5 stock product redfin required"

for file in bin/vcamesd bin/vcames-socket-proxy \
  device-probe.sh bridge.properties; do
  [ -s "$MODPATH/$file" ] || abort "! missing $file"
done
profile_value() {
  sed -n "s/^$1=//p" "$MODPATH/profile.runtime.properties" | head -n 1
}

has_pack=false
if [ -e "$MODPATH/kernel/v4l2loopback.ko" ] || \
   [ -e "$MODPATH/bin/vcames-global-camera-provider" ] || \
   [ -e "$MODPATH/bin/ffmpeg" ] || \
   [ -e "$MODPATH/profile.json" ]; then
  has_pack=true
  for file in kernel/v4l2loopback.ko bin/vcames-global-camera-provider bin/ffmpeg \
    ffmpeg.LICENSE.json licenses/FFmpeg-LGPL-2.1.txt profile.json profile.sig \
    profile.runtime.properties; do
    [ -s "$MODPATH/$file" ] || abort "! incomplete exact device pack: missing $file"
  done
  [ "$(profile_value validation_status)" = VERIFIED ] || abort "! Profile is not VERIFIED"
  [ "$(profile_value api)" = "$api" ] || abort "! Profile API does not match this device"
  [ "$(profile_value replacement_scope)" = global-front-back ] || \
    abort "! Runtime must replace front and back globally"
  [ "$(profile_value provider_interface)" = \
    android.hardware.camera.provider@2.4::ICameraProvider/legacy/0 ] || \
    abort "! Runtime must take over the Pixel 5 legacy/0 Provider instance"
  [ "$(profile_value back_camera_id)" = 0 ] || abort "! Pixel 5 back camera ID must be 0"
  [ "$(profile_value front_camera_id)" = 1 ] || abort "! Pixel 5 front camera ID must be 1"
  [ "$(profile_value profile_sha256)" = \
    "$(sha256sum "$MODPATH/profile.json" | cut -d' ' -f1)" ] || abort "! Profile hash mismatch"
  [ "$(profile_value kernel_module_sha256)" = \
    "$(sha256sum "$MODPATH/kernel/v4l2loopback.ko" | cut -d' ' -f1)" ] || \
    abort "! v4l2loopback kernel module hash mismatch"
  [ "$(profile_value global_provider_sha256)" = \
    "$(sha256sum "$MODPATH/bin/vcames-global-camera-provider" | cut -d' ' -f1)" ] || \
    abort "! Global Camera Provider hash mismatch"
  [ "$(profile_value ffmpeg_sha256)" = \
    "$(sha256sum "$MODPATH/bin/ffmpeg" | cut -d' ' -f1)" ] || \
    abort "! FFmpeg binary hash mismatch"
  [ "$(profile_value ffmpeg_manifest_sha256)" = \
    "$(sha256sum "$MODPATH/ffmpeg.LICENSE.json" | cut -d' ' -f1)" ] || \
    abort "! FFmpeg license manifest hash mismatch"
  [ "$(profile_value ffmpeg_license_sha256)" = \
    "$(sha256sum "$MODPATH/licenses/FFmpeg-LGPL-2.1.txt" | cut -d' ' -f1)" ] || \
    abort "! FFmpeg LGPL license text hash mismatch"

  actual_probe="$MODPATH/.device-probe.actual"
  sh "$MODPATH/device-probe.sh" >"$actual_probe" || abort "! DeviceProbe failed"
  for key in vendor_family soc_family camera_hal_transport manufacturer product device api \
    system_fingerprint_sha256 vendor_fingerprint_sha256 kernel_release_sha256 \
    cameraserver_sha256 camera_provider_sha256 vendor_camera_libraries_sha256 \
    graphics_stack_sha256 oem_provider_binary oem_provider_binary_sha256 compatibility_id; do
    expected="$(profile_value "$key")"
    actual="$(sed -n "s/^$key=//p" "$actual_probe" | head -n 1)"
    [ -n "$expected" ] || abort "! Profile projection missing $key"
    [ "$expected" = "$actual" ] || abort "! Exact Profile mismatch: $key"
  done
  rm -f "$actual_probe"
  ui_print "- exact build, kernel, global Provider and FFmpeg hashes matched"
else
  ui_print "! No signed exact device pack included; runtime will fail closed"
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644
for file in bin/vcamesd bin/vcames-socket-proxy \
  device-probe.sh service.sh action.sh; do
  set_perm "$MODPATH/$file" 0 0 0755
done
if [ "$has_pack" = true ]; then
  set_perm "$MODPATH/bin/vcames-global-camera-provider" 0 0 0755
  set_perm "$MODPATH/bin/ffmpeg" 0 0 0755
  set_perm "$MODPATH/kernel/v4l2loopback.ko" 0 0 0644
fi

ui_print "- runtime is installed separately; the APK never installs or changes ROOT"
ui_print "- SELinux remains enforcing; Provider takeover fails closed and restores the OEM camera"
