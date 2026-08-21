#!/usr/bin/env python3
"""Canonical VCamES APK/Root Bridge builder (stdlib only)."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEX64 = set("0123456789abcdef")


def fail(message: str) -> None:
    raise ValueError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def release_values() -> dict[str, str]:
    result: dict[str, str] = {}
    for line in (ROOT / "version.properties").read_text(encoding="utf-8").splitlines():
        if "=" in line and not line.startswith("#"):
            key, value = line.split("=", 1)
            result[key.strip()] = value.strip()
    required = {"versionName", "versionCode", "bridgeSchema", "daemonProtocol",
                "frameBusVersion", "profileSchema"}
    missing = required - result.keys()
    if missing:
        fail(f"version.properties missing: {', '.join(sorted(missing))}")
    return result


def build_native(api: int, ndk: Path) -> tuple[Path, Path]:
    toolchain = ndk / "build" / "cmake" / "android.toolchain.cmake"
    if not toolchain.is_file():
        fail(f"Android NDK toolchain not found: {toolchain}")
    cmake = shutil.which("cmake")
    ninja = shutil.which("ninja")
    if not cmake or not ninja:
        fail("cmake and ninja must be available on PATH")
    build = ROOT / "out" / "developer" / f"android-{api}-arm64"
    subprocess.run([
        cmake, "-S", str(ROOT / "daemon"), "-B", str(build), "-G", "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={ninja}", f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-30",
        "-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release",
    ], check=True)
    subprocess.run([cmake, "--build", str(build)], check=True)
    return build / "vcamesd", build / "vcames-socket-proxy"


def require_hex64(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(c not in HEX64 for c in value):
        fail(f"{label} must be a lowercase SHA-256")
    return value


def load_profile(profile_path: Path, signature_path: Path, adapter: Path,
                 api: int) -> tuple[dict[str, object], dict[str, str]]:
    if not signature_path.is_file() or signature_path.stat().st_size == 0:
        fail("Profile signature is missing or empty")
    profile = json.loads(profile_path.read_text(encoding="utf-8"))
    if not isinstance(profile, dict) or profile.get("schema_version") != 1:
        fail("Profile must use schema_version 1")
    validation = profile.get("validation")
    if not isinstance(validation, dict) or validation.get("status") != "VERIFIED":
        fail("Only a VERIFIED Profile can be packaged")
    canonical = (json.dumps(profile, ensure_ascii=False, sort_keys=True,
                            separators=(",", ":")) + "\n").encode("utf-8")
    if profile_path.read_bytes() != canonical:
        fail("Release Profile JSON must be canonical")
    if profile.get("vendor_family") not in {"google", "xiaomi"}:
        fail("Profile vendor_family is outside the product scope")
    if profile.get("api") != api:
        fail(f"Profile API {profile.get('api')} does not match build API {api}")
    compatibility_id = require_hex64(profile.get("compatibility_id"),
                                     "Profile compatibility_id")
    device = profile.get("device")
    build = profile.get("build")
    camera_hal = profile.get("camera_hal")
    hashes = profile.get("hashes")
    resources = profile.get("resources")
    if not all(isinstance(item, dict)
               for item in (device, build, camera_hal, hashes, resources)):
        fail("Profile device/build/camera_hal/hashes/resources objects are required")
    report_name = validation.get("report")
    if not isinstance(report_name, str) or not report_name:
        fail("Profile validation.report is required")
    report_path = (profile_path.parent / report_name).resolve()
    if profile_path.parent.resolve() not in report_path.parents or not report_path.is_file():
        fail("Profile validation report is missing or escapes the device-pack directory")
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if report.get("status") != "VERIFIED" or report.get("compatibility_id") != compatibility_id:
        fail("Profile validation report is not VERIFIED for this compatibility_id")
    if report.get("artifact_sha256") != resources.get("artifact_sha256"):
        fail("Profile validation report artifact hash mismatch")
    tests = report.get("tests")
    if not isinstance(tests, list) or not tests or any(
            not isinstance(item, dict) or item.get("status") != "PASS" for item in tests):
        fail("Profile validation report requires a non-empty PASS test list")
    adapter_hash = sha256(adapter)
    expected_adapter = require_hex64(hashes.get("adapter_sha256"),
                                     "Profile hashes.adapter_sha256")
    if adapter_hash != expected_adapter:
        fail("Replacement adapter hash does not match Profile")
    mapping = {
        "profile_sha256": sha256(profile_path),
        "validation_status": "VERIFIED",
        "vendor_family": str(profile["vendor_family"]),
        "soc_family": str(profile["soc_family"]),
        "camera_hal_transport": str(camera_hal["transport"]),
        "manufacturer": str(device["manufacturer"]),
        "product": str(device["product"]),
        "device": str(device["device"]),
        "api": str(profile["api"]),
        "system_fingerprint_sha256": require_hex64(
            build.get("system_fingerprint_sha256"), "system fingerprint"),
        "vendor_fingerprint_sha256": require_hex64(
            build.get("vendor_fingerprint_sha256"), "vendor fingerprint"),
        "cameraserver_sha256": require_hex64(
            hashes.get("cameraserver_sha256"), "cameraserver hash"),
        "camera_provider_sha256": require_hex64(
            hashes.get("camera_provider_sha256"), "camera provider hash"),
        "vendor_camera_libraries_sha256": require_hex64(
            hashes.get("vendor_camera_libraries_sha256"), "vendor camera hash"),
        "graphics_stack_sha256": require_hex64(
            hashes.get("graphics_stack_sha256"), "graphics stack hash"),
        "adapter_sha256": adapter_hash,
        "compatibility_id": compatibility_id,
    }
    return profile, mapping


def verify_profile_signature(public_key: Path, profile: Path, signature: Path) -> None:
    if not public_key.is_file():
        fail("Profile public key is missing")
    openssl = shutil.which("openssl")
    if not openssl:
        fail("openssl is required to verify a device-pack Profile signature")
    result = subprocess.run([
        openssl, "pkeyutl", "-verify", "-pubin", "-inkey", str(public_key),
        "-rawin", "-in", str(profile), "-sigfile", str(signature),
    ], capture_output=True, text=True, check=False)
    if result.returncode != 0:
        fail(f"Profile Ed25519 signature verification failed: {result.stderr.strip()}")


def zip_tree(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(source).as_posix())


def build(args: argparse.Namespace) -> tuple[Path, Path]:
    if args.api not in range(30, 34):
        fail("API must be between 30 and 33")
    values = release_values()
    daemon = args.daemon
    proxy = args.proxy
    if daemon is None:
        ndk_text = args.ndk or os.environ.get("ANDROID_NDK_HOME") \
            or os.environ.get("ANDROID_NDK_ROOT")
        if not ndk_text:
            fail("Set ANDROID_NDK_HOME or pass --ndk")
        daemon, proxy = build_native(args.api, Path(ndk_text).resolve())
    elif proxy is None:
        proxy = daemon.with_name("vcames-socket-proxy")
    if not daemon.is_file() or not proxy.is_file():
        fail("vcamesd and vcames-socket-proxy binaries are required")

    developer = ROOT / "out" / "developer"
    release = ROOT / "out" / "release"
    developer.mkdir(parents=True, exist_ok=True)
    release.mkdir(parents=True, exist_ok=True)
    stage_path = Path(tempfile.mkdtemp(prefix="vcames-stage-", dir=developer))
    try:
        shutil.copytree(ROOT / "root-module" / "template", stage_path,
                        dirs_exist_ok=True)
        module_template = (stage_path / "module.prop.in").read_text(encoding="utf-8")
        module_template = module_template.replace("@VERSION_NAME@", values["versionName"])
        module_template = module_template.replace("@VERSION_CODE@", values["versionCode"])
        (stage_path / "module.prop").write_text(module_template, encoding="ascii")
        (stage_path / "module.prop.in").unlink()
        bridge = "".join(f"{key}={value}\n" for key, value in [
            ("bridge_schema", values["bridgeSchema"]),
            ("version_name", values["versionName"]),
            ("version_code", values["versionCode"]),
            ("daemon_protocol", values["daemonProtocol"]),
            ("frame_bus_version", values["frameBusVersion"]),
            ("profile_schema", values["profileSchema"]),
        ])
        (stage_path / "bridge.properties").write_text(bridge, encoding="ascii")
        binary_dir = stage_path / "bin"
        binary_dir.mkdir(exist_ok=True)
        shutil.copy2(daemon, binary_dir / "vcamesd")
        shutil.copy2(proxy, binary_dir / "vcames-socket-proxy")

        if args.adapter:
            if not args.profile or not args.profile_signature or not args.profile_public_key:
                fail("--adapter requires --profile, --profile-signature and --profile-public-key")
            verify_profile_signature(args.profile_public_key, args.profile,
                                     args.profile_signature)
            _, runtime = load_profile(args.profile, args.profile_signature,
                                      args.adapter, args.api)
            shutil.copy2(args.adapter, binary_dir / "vcames-camera-adapter")
            shutil.copy2(args.profile, stage_path / "profile.json")
            shutil.copy2(args.profile_signature, stage_path / "profile.sig")
            projection = "".join(f"{key}={value}\n" for key, value in runtime.items())
            (stage_path / "profile.runtime.properties").write_text(
                projection, encoding="utf-8")

        module = args.output or developer / f"VCamES-Root-API{args.api}.zip"
        module = module.resolve()
        zip_tree(stage_path, module)
        assets = ROOT / "app" / "build" / "generated" / "rootBridgeAssets"
        assets.mkdir(parents=True, exist_ok=True)
        shutil.copy2(module, assets / "vcames-root-bridge.zip")

        gradle = ROOT / ("gradlew.bat" if os.name == "nt" else "gradlew")
        subprocess.run([str(gradle), ":app:assembleDebug"], cwd=ROOT, check=True)
        built_apk = ROOT / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk"
        apk = release / f"VCamES-{values['versionName']}.apk"
        shutil.copy2(built_apk, apk)
        return apk, module
    finally:
        shutil.rmtree(stage_path, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--api", type=int, default=30)
    parser.add_argument("--ndk")
    parser.add_argument("--daemon", type=Path)
    parser.add_argument("--proxy", type=Path)
    parser.add_argument("--adapter", type=Path)
    parser.add_argument("--profile", type=Path)
    parser.add_argument("--profile-signature", type=Path)
    parser.add_argument("--profile-public-key", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        apk, module = build(args)
    except (OSError, ValueError, KeyError, json.JSONDecodeError,
            subprocess.CalledProcessError) as failure:
        print(f"Build failed: {failure}", file=sys.stderr)
        return 1
    print(f"User APK: {apk}")
    print(f"Developer Root Bridge: {module}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
