#!/usr/bin/env bash
set -euo pipefail

API="${1:-35}"
case "$API" in 33|34|35) ;; *) echo "API must be 33, 34, or 35" >&2; exit 64 ;; esac

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/out/root"
NDK_DIR="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
[[ -n "$NDK_DIR" ]] || { echo "Set ANDROID_NDK_HOME" >&2; exit 64; }

"$ROOT_DIR/gradlew" :app:assembleRootDebug
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
cp "$ROOT_DIR/app/build/outputs/apk/root/debug/app-root-debug.apk" "$STAGE/controller.apk"
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

rm -f -- "$OUT_DIR/VCamES-Root-API$API.zip"
(cd "$STAGE" && zip -qr "$OUT_DIR/VCamES-Root-API$API.zip" .)
cp "$STAGE/controller.apk" "$OUT_DIR/VCamES-Root-controller.apk"
echo "Created $OUT_DIR/VCamES-Root-API$API.zip"
