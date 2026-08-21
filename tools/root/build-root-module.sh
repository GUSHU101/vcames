#!/usr/bin/env bash
set -euo pipefail

API="${1:-33}"
case "$API" in 30|31|32|33) ;; *) echo "API must be between 30 and 33" >&2; exit 64 ;; esac

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/out/root"
NDK_DIR="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
[[ -n "$NDK_DIR" ]] || { echo "Set ANDROID_NDK_HOME" >&2; exit 64; }

BUILD_DIR="$OUT_DIR/android-$API-arm64"
cmake -S "$ROOT_DIR/daemon" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK_DIR/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM="android-$API" \
  -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"

mkdir -p "$OUT_DIR"
STAGE="$(mktemp -d "$OUT_DIR/stage-XXXXXXXX")"
cleanup() {
  case "$STAGE" in "$OUT_DIR"/stage-*) rm -rf -- "$STAGE" ;; esac
}
trap cleanup EXIT

cp -R "$ROOT_DIR/root-module/template/." "$STAGE/"
mkdir -p "$STAGE/bin" "$STAGE/system/vendor/etc/vintf/manifest"
cp "$BUILD_DIR/vcamesd" "$STAGE/bin/vcamesd"
cp "$BUILD_DIR/vcames-socket-proxy" "$STAGE/bin/vcames-socket-proxy"
cp "$ROOT_DIR/aosp/config/external_camera_config.xml" \
  "$STAGE/system/vendor/etc/external_camera_config.xml"
if [[ -n "${VCAMES_KERNEL_MODULE:-}" ]]; then
  mkdir -p "$STAGE/kernel"
  cp "$VCAMES_KERNEL_MODULE" "$STAGE/kernel/v4l2loopback.ko"
fi
if [[ -n "${VCAMES_PROVIDER_BINARY:-}" ]]; then
  case "${VCAMES_EXTERNAL_PROVIDER_TRANSPORT:-}" in
    hidl-2.4) fragment="manifest_vcames_camera_provider_hidl_2_4.xml" ;;
    hidl-2.7) fragment="manifest_vcames_camera_provider_hidl_2_7.xml" ;;
    aidl-1) fragment="manifest_vcames_camera_provider_aidl_1.xml" ;;
    *) echo "Set VCAMES_EXTERNAL_PROVIDER_TRANSPORT to hidl-2.4, hidl-2.7, or aidl-1" >&2; exit 64 ;;
  esac
  cp "$VCAMES_PROVIDER_BINARY" "$STAGE/bin/external-camera-provider"
  cp "$ROOT_DIR/aosp/vintf/$fragment" \
    "$STAGE/system/vendor/etc/vintf/manifest/manifest_vcames_camera_provider.xml"
  printf 'transport=%s\n' "$VCAMES_EXTERNAL_PROVIDER_TRANSPORT" \
    >"$STAGE/external-provider.properties"
elif [[ -n "${VCAMES_EXTERNAL_PROVIDER_TRANSPORT:-}" ]]; then
  echo "VCAMES_EXTERNAL_PROVIDER_TRANSPORT requires VCAMES_PROVIDER_BINARY" >&2
  exit 64
fi
if [[ -n "${VCAMES_REPLACEMENT_ADAPTER:-}" ]]; then
  [[ -n "${VCAMES_COMPATIBILITY_MANIFEST:-}" ]] || {
    echo "Set VCAMES_COMPATIBILITY_MANIFEST with VCAMES_REPLACEMENT_ADAPTER" >&2
    exit 64
  }
  for field in schema vendor_family soc_family camera_hal_transport manufacturer product device api \
      system_fingerprint_sha256 vendor_fingerprint_sha256 cameraserver_sha256 \
      camera_provider_sha256 vendor_camera_libraries_sha256 graphics_stack_sha256 \
      adapter_sha256 compatibility_id; do
    grep -q "^${field}=" "$VCAMES_COMPATIBILITY_MANIFEST" || {
      echo "Compatibility manifest missing: $field" >&2
      exit 64
    }
  done
  manifest_vendor="$(sed -n 's/^vendor_family=//p' "$VCAMES_COMPATIBILITY_MANIFEST" | head -n 1)"
  case "$manifest_vendor" in google|xiaomi|samsung) ;; *)
    echo "Unsupported vendor_family: $manifest_vendor" >&2; exit 64 ;;
  esac
  manifest_api="$(sed -n 's/^api=//p' "$VCAMES_COMPATIBILITY_MANIFEST" | head -n 1)"
  [[ "$manifest_api" == "$API" ]] || {
    echo "Build API $API does not match manifest API $manifest_api" >&2
    exit 64
  }
  manifest_schema="$(sed -n 's/^schema=//p' "$VCAMES_COMPATIBILITY_MANIFEST" | head -n 1)"
  [[ "$manifest_schema" == "2" ]] || {
    echo "Compatibility manifest schema must be 2" >&2
    exit 64
  }
  manifest_adapter_hash="$(sed -n 's/^adapter_sha256=//p' \
    "$VCAMES_COMPATIBILITY_MANIFEST" | head -n 1 | tr '[:upper:]' '[:lower:]')"
  actual_adapter_hash="$(sha256sum "$VCAMES_REPLACEMENT_ADAPTER" | cut -d' ' -f1)"
  [[ "$manifest_adapter_hash" == "$actual_adapter_hash" ]] || {
    echo "Compatibility manifest adapter_sha256 does not match adapter" >&2
    exit 64
  }
  cp "$VCAMES_REPLACEMENT_ADAPTER" "$STAGE/bin/vcames-camera-adapter"
  cp "$VCAMES_COMPATIBILITY_MANIFEST" "$STAGE/compatibility.properties"
elif [[ -n "${VCAMES_COMPATIBILITY_MANIFEST:-}" ]]; then
  echo "VCAMES_COMPATIBILITY_MANIFEST requires VCAMES_REPLACEMENT_ADAPTER" >&2
  exit 64
fi

# Embed a controller-free module into the Root APK. Since the app is already
# installed when it invokes the root manager, customize.sh accepts this form.
GENERATED_ASSETS="$ROOT_DIR/app/build/generated/rootBridgeAssets"
mkdir -p "$GENERATED_ASSETS"
rm -f -- "$GENERATED_ASSETS/vcames-root-bridge.zip"
(cd "$STAGE" && zip -qr "$GENERATED_ASSETS/vcames-root-bridge.zip" .)
"$ROOT_DIR/gradlew" :app:assembleRootDebug
cp "$ROOT_DIR/app/build/outputs/apk/root/debug/app-root-debug.apk" "$STAGE/controller.apk"

rm -f -- "$OUT_DIR/VCamES-Root-API$API.zip"
(cd "$STAGE" && zip -qr "$OUT_DIR/VCamES-Root-API$API.zip" .)
cp "$STAGE/controller.apk" "$OUT_DIR/VCamES-Root-controller.apk"
cp "$STAGE/controller.apk" "$OUT_DIR/VCamES-Root-standalone.apk"
echo "Created $OUT_DIR/VCamES-Root-API$API.zip"
echo "Created $OUT_DIR/VCamES-Root-standalone.apk"
