#!/usr/bin/env python3
"""Strict release gate for the VCamES Profile v1 catalog (stdlib only)."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

HEX64 = re.compile(r"^[0-9a-f]{64}$")
PATCH = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}$")


class ValidationError(ValueError):
    pass


def load_json(path: Path) -> dict[str, Any]:
    def no_duplicates(items: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in items:
            if key in result:
                raise ValidationError(f"{path}: duplicate JSON key: {key}")
            result[key] = value
        return result

    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=no_duplicates)
    except (OSError, json.JSONDecodeError) as exc:
        raise ValidationError(f"{path}: cannot read JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise ValidationError(f"{path}: top level must be an object")
    return value


def canonical_bytes(value: dict[str, Any]) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True,
                       separators=(",", ":")) + "\n").encode("utf-8")


def required_object(parent: dict[str, Any], key: str, where: str) -> dict[str, Any]:
    value = parent.get(key)
    if not isinstance(value, dict):
        raise ValidationError(f"{where}.{key} must be an object")
    return value


def safe_child(root: Path, relative: Any, where: str) -> Path:
    if not isinstance(relative, str) or not relative or "\\" in relative:
        raise ValidationError(f"{where} must be a non-empty POSIX relative path")
    candidate = Path(relative)
    if candidate.is_absolute() or ".." in candidate.parts:
        raise ValidationError(f"{where} escapes the profiles directory")
    resolved = (root / candidate).resolve()
    if root.resolve() not in resolved.parents and resolved != root.resolve():
        raise ValidationError(f"{where} escapes the profiles directory")
    if not resolved.is_file():
        raise ValidationError(f"{where} does not exist: {relative}")
    return resolved


def require_hex(value: Any, where: str) -> None:
    if not isinstance(value, str) or not HEX64.fullmatch(value):
        raise ValidationError(f"{where} must be a lowercase SHA-256")


def validate_profile(path: Path, expected_id: str, repository_root: Path) -> None:
    profile = load_json(path)
    if path.read_bytes() != canonical_bytes(profile):
        raise ValidationError(f"{path}: release Profile JSON must be canonical")
    if profile.get("schema_version") != 1:
        raise ValidationError(f"{path}: schema_version must be 1")
    if profile.get("compatibility_id") != expected_id:
        raise ValidationError(f"{path}: compatibility_id does not match catalog")
    require_hex(expected_id, f"{path}.compatibility_id")
    if profile.get("vendor_family") not in {"google", "xiaomi"}:
        raise ValidationError(f"{path}: unsupported vendor_family")
    if profile.get("soc_family") not in {"tensor", "qualcomm", "mediatek"}:
        raise ValidationError(f"{path}: unsupported soc_family")
    api = profile.get("api")
    if not isinstance(api, int) or isinstance(api, bool) or not 30 <= api <= 33:
        raise ValidationError(f"{path}: api must be 30..33")

    device = required_object(profile, "device", str(path))
    for key in ("manufacturer", "product", "device", "model"):
        if not isinstance(device.get(key), str) or not device[key].strip():
            raise ValidationError(f"{path}.device.{key} is required")
    build = required_object(profile, "build", str(path))
    if not isinstance(build.get("build_id"), str) or not build["build_id"].strip():
        raise ValidationError(f"{path}.build.build_id is required")
    if not isinstance(build.get("security_patch"), str) or not PATCH.fullmatch(build["security_patch"]):
        raise ValidationError(f"{path}.build.security_patch must be YYYY-MM-DD")
    for key in ("system_fingerprint_sha256", "vendor_fingerprint_sha256"):
        require_hex(build.get(key), f"{path}.build.{key}")

    camera_hal = required_object(profile, "camera_hal", str(path))
    if camera_hal.get("transport") not in {"hidl", "aidl", "mixed"}:
        raise ValidationError(f"{path}.camera_hal.transport is invalid")
    if not isinstance(camera_hal.get("provider_instance"), str) or not camera_hal["provider_instance"]:
        raise ValidationError(f"{path}.camera_hal.provider_instance is required")
    hashes = required_object(profile, "hashes", str(path))
    if len(hashes) < 4:
        raise ValidationError(f"{path}.hashes requires at least four artifacts")
    for key, value in hashes.items():
        require_hex(value, f"{path}.hashes.{key}")
    capabilities = required_object(profile, "capabilities", str(path))
    for key in ("external", "front", "back"):
        if not isinstance(capabilities.get(key), bool):
            raise ValidationError(f"{path}.capabilities.{key} must be boolean")
    resources = required_object(profile, "resources", str(path))
    if resources.get("contains_proprietary_oem_files") is not False:
        raise ValidationError(f"{path}: proprietary OEM resources are forbidden")
    require_hex(resources.get("artifact_sha256"), f"{path}.resources.artifact_sha256")

    validation = required_object(profile, "validation", str(path))
    if validation.get("status") != "VERIFIED":
        raise ValidationError(f"{path}: catalog profiles must be VERIFIED")
    report_path = safe_child(
        repository_root, validation.get("report"), f"{path}.validation.report")
    report = load_json(report_path)
    if report.get("status") != "VERIFIED" or report.get("compatibility_id") != expected_id:
        raise ValidationError(f"{report_path}: report is not VERIFIED for this compatibility_id")
    if report.get("artifact_sha256") != resources.get("artifact_sha256"):
        raise ValidationError(f"{report_path}: release artifact hash does not match Profile")
    tests = report.get("tests")
    if (not isinstance(tests, list) or not tests
            or any(not isinstance(item, dict) or item.get("status") != "PASS" for item in tests)):
        raise ValidationError(f"{report_path}: tests must be a non-empty PASS list")


def verify_signature(public_key: Path, data: Path, signature: Path) -> None:
    result = subprocess.run(
        ["openssl", "pkeyutl", "-verify", "-pubin", "-inkey", str(public_key),
         "-rawin", "-in", str(data), "-sigfile", str(signature)],
        check=False, capture_output=True, text=True,
    )
    if result.returncode != 0:
        raise ValidationError(f"signature verification failed for {data}: {result.stderr.strip()}")


def validate_catalog(catalog_path: Path, public_key: Path | None = None) -> None:
    catalog_path = catalog_path.resolve()
    root = catalog_path.parent
    repository_root = root.parent
    catalog = load_json(catalog_path)
    if catalog.get("schema_version") != 1:
        raise ValidationError("catalog.schema_version must be 1")
    if catalog.get("product_scope") != "google-xiaomi-android11-13":
        raise ValidationError("catalog.product_scope is invalid")
    entries = catalog.get("entries")
    if not isinstance(entries, list):
        raise ValidationError("catalog.entries must be an array")
    signature = required_object(catalog, "signature", "catalog")
    if signature.get("algorithm") != "Ed25519":
        raise ValidationError("catalog signature algorithm must be Ed25519")

    if not entries:
        if catalog.get("catalog_status") != "EMPTY_NO_VERIFIED_PROFILES":
            raise ValidationError("an empty catalog must declare EMPTY_NO_VERIFIED_PROFILES")
        if signature.get("status") != "UNSIGNED_DEVELOPMENT":
            raise ValidationError("empty development catalog signature state is invalid")
        return

    if catalog.get("catalog_status") != "SIGNED_VERIFIED_PROFILES":
        raise ValidationError("non-empty catalog must declare SIGNED_VERIFIED_PROFILES")
    if catalog_path.read_bytes() != canonical_bytes(catalog):
        raise ValidationError("non-empty release catalog JSON must be canonical")
    if signature.get("status") != "SIGNED" or not signature.get("key_id"):
        raise ValidationError("non-empty catalog requires a signing key id")
    if public_key is None:
        raise ValidationError("non-empty catalog requires --public-key for Ed25519 verification")
    catalog_sig = safe_child(root, signature.get("file"), "catalog.signature.file")
    seen: set[str] = set()
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise ValidationError(f"catalog.entries[{index}] must be an object")
        compatibility_id = entry.get("compatibility_id")
        require_hex(compatibility_id, f"catalog.entries[{index}].compatibility_id")
        if compatibility_id in seen:
            raise ValidationError(f"duplicate compatibility_id: {compatibility_id}")
        seen.add(compatibility_id)
        if entry.get("status") != "VERIFIED":
            raise ValidationError(f"catalog.entries[{index}] must be VERIFIED")
        profile_path = safe_child(root, entry.get("profile"), f"catalog.entries[{index}].profile")
        profile_sig = safe_child(root, entry.get("signature"), f"catalog.entries[{index}].signature")
        validate_profile(profile_path, compatibility_id, repository_root)
        verify_signature(public_key, profile_path, profile_sig)
    verify_signature(public_key, catalog_path, catalog_sig)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("catalog", type=Path)
    parser.add_argument("--public-key", type=Path)
    args = parser.parse_args()
    try:
        validate_catalog(args.catalog, args.public_key)
    except (ValidationError, OSError) as exc:
        print(f"Profile validation failed: {exc}", file=sys.stderr)
        return 1
    print(f"Profile catalog valid: {args.catalog}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
