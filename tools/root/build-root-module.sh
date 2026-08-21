#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
args=(--api "${1:-30}")
[[ -z "${VCAMES_REPLACEMENT_ADAPTER:-}" ]] || args+=(--adapter "$VCAMES_REPLACEMENT_ADAPTER")
[[ -z "${VCAMES_PROFILE:-}" ]] || args+=(--profile "$VCAMES_PROFILE")
[[ -z "${VCAMES_PROFILE_SIGNATURE:-}" ]] || args+=(--profile-signature "$VCAMES_PROFILE_SIGNATURE")
[[ -z "${VCAMES_PROFILE_PUBLIC_KEY:-}" ]] || args+=(--profile-public-key "$VCAMES_PROFILE_PUBLIC_KEY")
exec python3 "$ROOT_DIR/tools/root/build_device_pack.py" "${args[@]}"
