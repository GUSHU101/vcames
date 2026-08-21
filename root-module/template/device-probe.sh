#!/system/bin/sh

# Canonical exact-build identity. This script reports facts and never predicts compatibility.
aggregate_hash() {
  hashes=""
  for candidate in "$@"; do
    [ ! -f "$candidate" ] || hashes="$hashes$(sha256sum "$candidate")
"
  done
  [ -n "$hashes" ] || { printf 'MISSING'; return; }
  printf '%s' "$hashes" | sort | sha256sum | cut -d' ' -f1
}

camera_hal_transport() {
  aidl=false
  hidl=false
  service list 2>/dev/null | grep -q 'android.hardware.camera.provider.ICameraProvider' && aidl=true
  lshal 2>/dev/null | grep -q 'camera.provider@' && hidl=true
  if grep -R -E 'format="aidl"|<fqname>ICameraProvider/' \
      /vendor/etc/vintf/manifest* /vendor/etc/vintf/manifest/*.xml \
      /system/etc/vintf/manifest* 2>/dev/null | grep -q 'camera.provider'; then
    aidl=true
  fi
  if grep -R -E 'format="hidl"|camera.provider@[0-9]' \
      /vendor/etc/vintf/manifest* /vendor/etc/vintf/manifest/*.xml \
      /system/etc/vintf/manifest* 2>/dev/null | grep -q 'camera.provider'; then
    hidl=true
  fi
  if [ "$aidl" = true ] && [ "$hidl" = true ]; then printf mixed
  elif [ "$aidl" = true ]; then printf aidl
  elif [ "$hidl" = true ]; then printf hidl
  else printf unknown; fi
}

manufacturer="$(getprop ro.product.manufacturer)"
brand="$(getprop ro.product.brand)"
product="$(getprop ro.product.name)"
device="$(getprop ro.product.device)"
api="$(getprop ro.build.version.sdk)"
vendor_identity="$(printf '%s|%s' "$manufacturer" "$brand" | tr '[:upper:]' '[:lower:]')"
case "$vendor_identity|$device|$product" in
  *google*\|redfin\|redfin) vendor_family=google ;;
  *) vendor_family=unsupported ;;
esac
soc_identity="$(printf '%s|%s|%s|%s' "$(getprop ro.soc.manufacturer)" \
  "$(getprop ro.soc.model)" "$(getprop ro.board.platform)" "$(getprop ro.hardware)" | \
  tr '[:upper:]' '[:lower:]')"
case "$soc_identity" in
  *qualcomm*|*snapdragon*|*qcom*|*msm*|*sm7250*) soc_family=qualcomm ;;
  *) soc_family=unknown ;;
esac
transport="$(camera_hal_transport)"
provider_interface=android.hardware.camera.provider@2.4::ICameraProvider/legacy/0
oem_provider_pid="$(lshal -ip 2>/dev/null | awk -v target="$provider_interface" \
  '$1 == target && $2 ~ /^[0-9]+$/ { print $2; exit }')"
oem_provider_binary="$(readlink "/proc/$oem_provider_pid/exe" 2>/dev/null)"
oem_provider_binary_hash="$(sha256sum "/proc/$oem_provider_pid/exe" 2>/dev/null | cut -d' ' -f1)"
[ -n "$oem_provider_binary" ] || oem_provider_binary=MISSING
[ -n "$oem_provider_binary_hash" ] || oem_provider_binary_hash=MISSING
system_hash="$(printf '%s' "$(getprop ro.build.fingerprint)" | sha256sum | cut -d' ' -f1)"
vendor_hash="$(printf '%s' "$(getprop ro.vendor.build.fingerprint)" | sha256sum | cut -d' ' -f1)"
kernel_release_hash="$(printf '%s' "$(uname -r)" | sha256sum | cut -d' ' -f1)"
cameraserver_hash="$(sha256sum /system/bin/cameraserver 2>/dev/null | cut -d' ' -f1)"
[ -n "$cameraserver_hash" ] || cameraserver_hash=MISSING
provider_hash="$(aggregate_hash /vendor/bin/hw/*camera*provider* /vendor/lib64/hw/*camera*provider* /vendor/lib64/*camera*provider*)"
camera_hash="$(aggregate_hash /vendor/bin/hw/*camera* /vendor/lib64/hw/*camera* /vendor/lib64/*camera*.so /vendor/lib64/*camera*/*)"
graphics_hash="$(aggregate_hash /vendor/lib64/hw/*mapper* /vendor/lib64/hw/*allocator* /vendor/lib64/*mapper* /vendor/lib64/*allocator*)"
compatibility_id="$(printf '%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s' \
  "$vendor_family" "$soc_family" "$transport" "$manufacturer" "$product" "$device" "$api" \
  "$system_hash" "$vendor_hash" "$kernel_release_hash" "$cameraserver_hash" "$provider_hash" "$camera_hash" \
  "$graphics_hash" "$oem_provider_binary" "$oem_provider_binary_hash" | sha256sum | cut -d' ' -f1)"

printf 'vendor_family=%s\nsoc_family=%s\ncamera_hal_transport=%s\n' \
  "$vendor_family" "$soc_family" "$transport"
printf 'selinux=%s\n' "$(getenforce)"
printf 'manufacturer=%s\nproduct=%s\ndevice=%s\napi=%s\n' \
  "$manufacturer" "$product" "$device" "$api"
printf 'system_fingerprint_sha256=%s\nvendor_fingerprint_sha256=%s\n' "$system_hash" "$vendor_hash"
printf 'kernel_release_sha256=%s\n' "$kernel_release_hash"
printf 'cameraserver_sha256=%s\ncamera_provider_sha256=%s\n' "$cameraserver_hash" "$provider_hash"
printf 'oem_provider_binary=%s\noem_provider_binary_sha256=%s\n' \
  "$oem_provider_binary" "$oem_provider_binary_hash"
printf 'vendor_camera_libraries_sha256=%s\ngraphics_stack_sha256=%s\n' "$camera_hash" "$graphics_hash"
printf 'compatibility_id=%s\n' "$compatibility_id"
