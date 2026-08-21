#!/system/bin/sh

SKIPMOUNT=false
PROPFILE=false
POSTFSDATA=false
LATESTARTSERVICE=true

ui_print "- VCamES Root Bridge 2.1.0"

api="$(getprop ro.build.version.sdk)"
case "$api" in
  30|31|32|33) ;;
  *) abort "! 仅支持 Android 11-13（API 30-33），当前 API=$api" ;;
esac

arch="$(getprop ro.product.cpu.abi)"
case "$arch" in
  arm64-v8a) ;;
  *) abort "! Root Bridge 只提供 arm64-v8a，当前 ABI=$arch" ;;
esac

manufacturer="$(getprop ro.product.manufacturer)"
brand="$(getprop ro.product.brand)"
device="$(getprop ro.product.device)"
vendor_identity="$(printf '%s|%s' "$manufacturer" "$brand" | tr '[:upper:]' '[:lower:]')"
case "$vendor_identity" in
  *google*) vendor_family="google" ;;
  *xiaomi*|*redmi*|*poco*) vendor_family="xiaomi" ;;
  *samsung*) vendor_family="samsung" ;;
  *) abort "! 仅支持 Google、小米/Redmi/POCO、Samsung，当前为 $manufacturer / $brand" ;;
esac

soc_identity="$(printf '%s|%s|%s|%s' \
  "$(getprop ro.soc.manufacturer)" "$(getprop ro.soc.model)" \
  "$(getprop ro.board.platform)" "$(getprop ro.hardware)" | \
  tr '[:upper:]' '[:lower:]')"
case "$soc_identity" in
  *tensor*|*gs101*|*gs201*) soc_family="tensor" ;;
  *qualcomm*|*snapdragon*|*qcom*|*msm*|*sm[0-9][0-9][0-9]*) soc_family="qualcomm" ;;
  *exynos*) soc_family="exynos" ;;
  *mediatek*|*mtk*|*mt[0-9][0-9][0-9][0-9]*) soc_family="mediatek" ;;
  *) soc_family="unknown" ;;
esac

[ -f "$MODPATH/bin/vcamesd" ] || abort "! 安装包缺少 bin/vcamesd"
if [ ! -f "$MODPATH/controller.apk" ] && \
    ! cmd package path io.github.gushu101.vcames >/dev/null 2>&1; then
  abort "! standalone 模块需要先安装 VCamES Root APK"
fi

compat_value() {
  sed -n "s/^$1=//p" "$MODPATH/compatibility.properties" | head -n 1
}

aggregate_hash() {
  hashes=""
  for candidate in "$@"; do
    if [ -f "$candidate" ]; then
      hashes="$hashes$(sha256sum "$candidate")
"
    fi
  done
  [ -n "$hashes" ] || { printf 'MISSING'; return; }
  printf '%s' "$hashes" | sort | sha256sum | cut -d' ' -f1
}

camera_hal_transport() {
  aidl=false
  hidl=false
  if service list 2>/dev/null | grep -q 'android.hardware.camera.provider.ICameraProvider'; then
    aidl=true
  fi
  if lshal 2>/dev/null | grep -q 'camera.provider@'; then
    hidl=true
  fi
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
  if [ "$aidl" = true ] && [ "$hidl" = true ]; then
    printf 'mixed'
  elif [ "$aidl" = true ]; then
    printf 'aidl'
  elif [ "$hidl" = true ]; then
    printf 'hidl'
  else
    printf 'unknown'
  fi
}

if [ -f "$MODPATH/bin/vcames-camera-adapter" ]; then
  [ -f "$MODPATH/compatibility.properties" ] || \
    abort "! 摄像头替换适配器缺少 compatibility.properties"

  actual_product="$(getprop ro.product.name)"
  actual_hal_transport="$(camera_hal_transport)"
  actual_system_fingerprint="$(printf '%s' "$(getprop ro.build.fingerprint)" | sha256sum | cut -d' ' -f1)"
  actual_vendor_fingerprint="$(printf '%s' "$(getprop ro.vendor.build.fingerprint)" | sha256sum | cut -d' ' -f1)"
  actual_cameraserver="$(sha256sum /system/bin/cameraserver 2>/dev/null | cut -d' ' -f1)"
  actual_provider="$(aggregate_hash \
    /vendor/bin/hw/*camera*provider* \
    /vendor/lib64/hw/*camera*provider* \
    /vendor/lib64/*camera*provider*)"
  actual_vendor_camera="$(aggregate_hash \
    /vendor/bin/hw/*camera* /vendor/lib64/hw/*camera* \
    /vendor/lib64/*camera*.so /vendor/lib64/*camera*/*)"
  actual_graphics="$(aggregate_hash \
    /vendor/lib64/hw/*mapper* /vendor/lib64/hw/*allocator* \
    /vendor/lib64/*mapper* /vendor/lib64/*allocator*)"
  actual_adapter="$(sha256sum "$MODPATH/bin/vcames-camera-adapter" | cut -d' ' -f1)"
  actual_compatibility_id="$(printf '%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s' \
    "$vendor_family" "$soc_family" "$actual_hal_transport" \
    "$manufacturer" "$actual_product" "$device" "$api" \
    "$actual_system_fingerprint" "$actual_vendor_fingerprint" \
    "$actual_cameraserver" "$actual_provider" "$actual_vendor_camera" \
    "$actual_graphics" | \
    sha256sum | cut -d' ' -f1)"

  [ "$soc_family" != "unknown" ] || abort "! 无法识别 SoC family，拒绝加载 replacement adapter"
  [ "$actual_hal_transport" != "unknown" ] || abort "! 无法识别 Camera HIDL/AIDL transport"
  [ "$(compat_value vendor_family)" = "$vendor_family" ] || abort "! 适配器厂商族不匹配"
  [ "$(compat_value soc_family)" = "$soc_family" ] || abort "! 适配器 SoC family 不匹配"
  [ "$(compat_value camera_hal_transport)" = "$actual_hal_transport" ] || \
    abort "! 适配器 Camera HAL transport 不匹配"
  [ "$(compat_value manufacturer)" = "$manufacturer" ] || abort "! 适配器制造商不匹配"
  [ "$(compat_value product)" = "$actual_product" ] || abort "! 适配器 product 不匹配"
  [ "$(compat_value device)" = "$device" ] || abort "! 适配器设备不匹配"
  [ "$(compat_value api)" = "$api" ] || abort "! 适配器 API 不匹配"
  [ "$(compat_value system_fingerprint_sha256)" = "$actual_system_fingerprint" ] || \
    abort "! 适配器 system fingerprint 不匹配"
  [ "$(compat_value vendor_fingerprint_sha256)" = "$actual_vendor_fingerprint" ] || \
    abort "! 适配器 vendor fingerprint 不匹配"
  [ "$(compat_value cameraserver_sha256)" = "$actual_cameraserver" ] || \
    abort "! 适配器 cameraserver 不匹配"
  [ "$(compat_value camera_provider_sha256)" = "$actual_provider" ] || \
    abort "! 适配器 camera provider 集合不匹配"
  [ "$(compat_value vendor_camera_libraries_sha256)" = "$actual_vendor_camera" ] || \
    abort "! 适配器 vendor camera 库集合不匹配"
  [ "$(compat_value graphics_stack_sha256)" = "$actual_graphics" ] || \
    abort "! 适配器 mapper/allocator 集合不匹配"
  [ "$(compat_value adapter_sha256)" = "$actual_adapter" ] || \
    abort "! 适配器二进制自身哈希不匹配"
  [ "$(compat_value compatibility_id)" = "$actual_compatibility_id" ] || \
    abort "! compatibility_id 不匹配"
  ui_print "- $vendor_family/$soc_family/$actual_hal_transport 前后摄适配器完整构建校验通过"
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/bin/vcamesd" 0 0 0755
[ ! -f "$MODPATH/bin/external-camera-provider" ] || \
  set_perm "$MODPATH/bin/external-camera-provider" 0 0 0755
[ ! -f "$MODPATH/bin/vcames-camera-adapter" ] || \
  set_perm "$MODPATH/bin/vcames-camera-adapter" 0 0 0755
[ ! -f "$MODPATH/kernel/v4l2loopback.ko" ] || \
  set_perm "$MODPATH/kernel/v4l2loopback.ko" 0 0 0644
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755

existing="$(cmd package list packages -U io.github.gushu101.vcames 2>/dev/null)"
case "$existing" in
  *uid:1000*)
    abort "! 检测到平台 UID 版本。Root 与系统版本不能共存。"
    ;;
esac

if [ -f "$MODPATH/controller.apk" ]; then
  ui_print "- 安装普通签名 Root 控制端"
  if ! pm install -r -g "$MODPATH/controller.apk" >/dev/null 2>&1; then
    ui_print "! 控制 APK 自动安装失败；请手动安装配套 controller APK"
  fi
fi

ui_print "- 兼容 KernelSU/Magisk 模块脚本；KernelSU system 覆盖需要 metamodule"
ui_print "- 保持 SELinux enforcing；不安装 Xposed/Zygisk 注入代码"
ui_print "- 重启后执行模块 Action 查看能力与安全模式状态"
