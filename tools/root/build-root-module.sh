#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
args=(--api "${1:-30}")
[[ -z "${VCAMES_KERNEL_MODULE:-}" ]] || args+=(--kernel-module "$VCAMES_KERNEL_MODULE")
[[ -z "${VCAMES_EXTERNAL_CAMERA_PROVIDER:-}" ]] || args+=(--provider "$VCAMES_EXTERNAL_CAMERA_PROVIDER")
[[ -z "${VCAMES_FFMPEG:-}" ]] || args+=(--ffmpeg "$VCAMES_FFMPEG")
[[ -z "${VCAMES_FFMPEG_MANIFEST:-}" ]] || args+=(--ffmpeg-manifest "$VCAMES_FFMPEG_MANIFEST")
[[ -z "${VCAMES_FFMPEG_LICENSE:-}" ]] || args+=(--ffmpeg-license "$VCAMES_FFMPEG_LICENSE")
[[ -z "${VCAMES_PROFILE:-}" ]] || args+=(--profile "$VCAMES_PROFILE")
[[ -z "${VCAMES_PROFILE_SIGNATURE:-}" ]] || args+=(--profile-signature "$VCAMES_PROFILE_SIGNATURE")
[[ -z "${VCAMES_PROFILE_PUBLIC_KEY:-}" ]] || args+=(--profile-public-key "$VCAMES_PROFILE_PUBLIC_KEY")
exec python3 "$ROOT_DIR/tools/root/build_device_pack.py" "${args[@]}"
