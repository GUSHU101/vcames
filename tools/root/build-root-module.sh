#!/usr/bin/env bash
set -euo pipefail

API="${1:-35}"
case "$API" in 30|31|32|33|34|35) ;; *) echo "API must be between 30 and 35" >&2; exit 64 ;; esac

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
cp "$ROOT_DIR/aosp/config/external_camera_config.xml" \
  "$STAGE/system/vendor/etc/external_camera_config.xml"
cp "$ROOT_DIR/aosp/vintf/manifest_vcames_camera_provider.xml" \
  "$STAGE/system/vendor/etc/vintf/manifest/manifest_vcames_camera_provider.xml"

if [[ -n "${VCAMES_KERNEL_MODULE:-}" ]]; then
  mkdir -p "$STAGE/kernel"
  cp "$VCAMES_KERNEL_MODULE" "$STAGE/kernel/v4l2loopback.ko"
fi
if [[ -n "${VCAMES_PROVIDER_BINARY:-}" ]]; then
  cp "$VCAMES_PROVIDER_BINARY" "$STAGE/bin/external-camera-provider"
fi
if [[ -n "${VCAMES_REPLACEMENT_ADAPTER:-}" ]]; then
  [[ -n "${VCAMES_COMPATIBILITY_MANIFEST:-}" ]] || {
    echo "Set VCAMES_COMPATIBILITY_MANIFEST with VCAMES_REPLACEMENT_ADAPTER" >&2
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
