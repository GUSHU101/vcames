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
case "$vendor_identity" in
  *google*) vendor_family=google ;;
  *xiaomi*|*redmi*|*poco*) vendor_family=xiaomi ;;
  *) vendor_family=unsupported ;;
esac
soc_identity="$(printf '%s|%s|%s|%s' "$(getprop ro.soc.manufacturer)" \
  "$(getprop ro.soc.model)" "$(getprop ro.board.platform)" "$(getprop ro.hardware)" | \
  tr '[:upper:]' '[:lower:]')"
case "$soc_identity" in
  *tensor*|*gs101*|*gs201*) soc_family=tensor ;;
  *qualcomm*|*snapdragon*|*qcom*|*msm*|*sm[0-9][0-9][0-9]*) soc_family=qualcomm ;;
  *exynos*) soc_family=exynos ;;
  *mediatek*|*mtk*|*mt[0-9][0-9][0-9][0-9]*) soc_family=mediatek ;;
  *) soc_family=unknown ;;
esac
transport="$(camera_hal_transport)"
system_hash="$(printf '%s' "$(getprop ro.build.fingerprint)" | sha256sum | cut -d' ' -f1)"
vendor_hash="$(printf '%s' "$(getprop ro.vendor.build.fingerprint)" | sha256sum | cut -d' ' -f1)"
cameraserver_hash="$(sha256sum /system/bin/cameraserver 2>/dev/null | cut -d' ' -f1)"
[ -n "$cameraserver_hash" ] || cameraserver_hash=MISSING
provider_hash="$(aggregate_hash /vendor/bin/hw/*camera*provider* /vendor/lib64/hw/*camera*provider* /vendor/lib64/*camera*provider*)"
camera_hash="$(aggregate_hash /vendor/bin/hw/*camera* /vendor/lib64/hw/*camera* /vendor/lib64/*camera*.so /vendor/lib64/*camera*/*)"
graphics_hash="$(aggregate_hash /vendor/lib64/hw/*mapper* /vendor/lib64/hw/*allocator* /vendor/lib64/*mapper* /vendor/lib64/*allocator*)"
compatibility_id="$(printf '%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s' \
  "$vendor_family" "$soc_family" "$transport" "$manufacturer" "$product" "$device" "$api" \
  "$system_hash" "$vendor_hash" "$cameraserver_hash" "$provider_hash" "$camera_hash" \
  "$graphics_hash" | sha256sum | cut -d' ' -f1)"

printf 'vendor_family=%s\nsoc_family=%s\ncamera_hal_transport=%s\n' \
  "$vendor_family" "$soc_family" "$transport"
printf 'selinux=%s\n' "$(getenforce)"
printf 'manufacturer=%s\nproduct=%s\ndevice=%s\napi=%s\n' \
  "$manufacturer" "$product" "$device" "$api"
printf 'system_fingerprint_sha256=%s\nvendor_fingerprint_sha256=%s\n' "$system_hash" "$vendor_hash"
printf 'cameraserver_sha256=%s\ncamera_provider_sha256=%s\n' "$cameraserver_hash" "$provider_hash"
printf 'vendor_camera_libraries_sha256=%s\ngraphics_stack_sha256=%s\n' "$camera_hash" "$graphics_hash"
printf 'compatibility_id=%s\n' "$compatibility_id"
