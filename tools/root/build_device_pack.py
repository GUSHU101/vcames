#!/usr/bin/env python3
"""Canonical VCamES APK and separately installed Pixel 5 runtime builder."""

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
                "providerProtocol", "profileSchema"}
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
        "-DANDROID_ABI=arm64-v8a", f"-DANDROID_PLATFORM=android-{api}",
        "-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release",
    ], check=True)
    subprocess.run([cmake, "--build", str(build)], check=True)
    return build / "vcamesd", build / "vcames-socket-proxy"


def require_hex64(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(c not in HEX64 for c in value):
        fail(f"{label} must be a lowercase SHA-256")
    return value


def projection_value(value: object, label: str) -> str:
    text = str(value)
    if not text or len(text) > 256 or any(character in text for character in "\r\n\0"):
        fail(f"{label} cannot be represented in runtime properties")
    return text


def load_ffmpeg_manifest(manifest_path: Path, ffmpeg: Path,
                         license_text: Path) -> dict[str, object]:
    if not manifest_path.is_file() or not ffmpeg.is_file() or not license_text.is_file():
        fail("FFmpeg binary, license manifest, and LGPL license text are required")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        fail("FFmpeg manifest must use schema_version 1")
    canonical = (json.dumps(manifest, ensure_ascii=False, sort_keys=True,
                            separators=(",", ":")) + "\n").encode("utf-8")
    if manifest_path.read_bytes() != canonical:
        fail("FFmpeg license manifest must be canonical JSON")
    if manifest.get("name") != "FFmpeg" or \
            manifest.get("license") != "LGPL-2.1-or-later":
        fail("FFmpeg pack must use the LGPL-2.1-or-later configuration")
    source_url = manifest.get("source_url")
    revision = manifest.get("source_revision")
    configuration = manifest.get("build_configuration")
    if (not isinstance(source_url, str) or not source_url.startswith("https://")
            or any(character in source_url for character in "\r\n\0")):
        fail("FFmpeg manifest source_url must use HTTPS")
    if not isinstance(revision, str) or not revision.strip():
        fail("FFmpeg manifest source_revision is required")
    if (not isinstance(configuration, list) or not configuration
            or any(not isinstance(item, str) or not item
                   or any(character in item for character in "\r\n\0")
                   for item in configuration)):
        fail("FFmpeg manifest build_configuration must be a non-empty string list")
    if any(item.startswith("--enable-gpl") or item.startswith("--enable-nonfree")
           for item in configuration):
        fail("FFmpeg GPL and nonfree build options are outside release scope")
    if "--disable-gpl" not in configuration or "--disable-nonfree" not in configuration:
        fail("FFmpeg manifest must explicitly disable GPL and nonfree components")
    binary_hash = sha256(ffmpeg)
    if manifest.get("binary_sha256") != binary_hash:
        fail("FFmpeg manifest binary hash mismatch")
    if manifest.get("license_text_sha256") != sha256(license_text):
        fail("FFmpeg LGPL license text hash mismatch")
    required_protocols = {
        "http", "https", "httpproxy", "tls", "crypto", "ffrtmpcrypt",
        "ffrtmphttp", "rtmp", "rtmps",
        "rtmpe", "rtmpt", "rtmpte", "rtmpts", "srt", "rist", "rtp",
        "srtp", "udp", "tcp", "mmsh", "mmst",
    }
    required_demuxers = {
        "hls", "dash", "flv", "rtsp", "rtp", "mpegts", "mjpeg", "asf",
    }
    required_decoders = {"h264", "hevc", "vp8", "vp9", "av1", "mpeg4", "mjpeg"}
    required_filters = {"fps", "scale", "pad"}
    protocols = manifest.get("input_protocols")
    demuxers = manifest.get("demuxers")
    decoders = manifest.get("decoders")
    filters = manifest.get("filters")
    encoders = manifest.get("encoders")
    if (not isinstance(protocols, list)
            or any(not isinstance(item, str) for item in protocols)
            or not required_protocols.issubset(set(protocols))):
        fail("FFmpeg manifest is missing required network input protocols")
    if (not isinstance(demuxers, list)
            or any(not isinstance(item, str) for item in demuxers)
            or not required_demuxers.issubset(set(demuxers))):
        fail("FFmpeg manifest is missing required streaming demuxers")
    if (not isinstance(decoders, list)
            or any(not isinstance(item, str) for item in decoders)
            or not required_decoders.issubset(set(decoders))):
        fail("FFmpeg manifest is missing required video decoders")
    if (not isinstance(filters, list)
            or any(not isinstance(item, str) for item in filters)
            or not required_filters.issubset(set(filters))):
        fail("FFmpeg manifest is missing required video filters")
    if (not isinstance(encoders, list)
            or any(not isinstance(item, str) for item in encoders)
            or "mjpeg" not in encoders):
        fail("FFmpeg manifest must declare the MJPEG encoder")
    return manifest


def load_profile(profile_path: Path, signature_path: Path, kernel_module: Path,
                 provider: Path, ffmpeg: Path, ffmpeg_manifest: Path,
                 ffmpeg_license: Path,
                 api: int) -> tuple[dict[str, object], dict[str, str]]:
    if not signature_path.is_file() or signature_path.stat().st_size == 0:
        fail("Profile signature is missing or empty")
    if not kernel_module.is_file() or not provider.is_file():
        fail("v4l2loopback kernel module and global Camera Provider are required")
    load_ffmpeg_manifest(ffmpeg_manifest, ffmpeg, ffmpeg_license)
    profile = json.loads(profile_path.read_text(encoding="utf-8"))
    if not isinstance(profile, dict) or profile.get("schema_version") != 2:
        fail("Profile must use schema_version 2")
    validation = profile.get("validation")
    if not isinstance(validation, dict) or validation.get("status") != "VERIFIED":
        fail("Only a VERIFIED Profile can be packaged")
    canonical = (json.dumps(profile, ensure_ascii=False, sort_keys=True,
                            separators=(",", ":")) + "\n").encode("utf-8")
    if profile_path.read_bytes() != canonical:
        fail("Release Profile JSON must be canonical")
    if profile.get("vendor_family") != "google":
        fail("Only Google Pixel 5 profiles are in product scope")
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
    if (str(device.get("manufacturer", "")).lower() != "google"
            or device.get("product") != "redfin"
            or device.get("device") != "redfin"
            or device.get("model") != "Pixel 5"):
        fail("Profile must identify Google Pixel 5 (redfin)")
    if (camera_hal.get("provider_instance") != "legacy/0"
            or camera_hal.get("provider_interface")
            != "android.hardware.camera.provider@2.4::ICameraProvider/legacy/0"
            or camera_hal.get("registration") != "oem-service-takeover"
            or camera_hal.get("replacement_scope") != "global-front-back"
            or camera_hal.get("back_camera_id") != "0"
            or camera_hal.get("front_camera_id") != "1"
            or camera_hal.get("v4l2_device") != "/dev/video100"):
        fail("Profile requires global Pixel 5 legacy/0 camera IDs 0/1 takeover")
    oem_provider_service = camera_hal.get("oem_provider_service")
    if (not isinstance(oem_provider_service, str) or not oem_provider_service
            or any(character not in
                   "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-"
                   for character in oem_provider_service)):
        fail("Profile camera_hal.oem_provider_service is invalid")
    oem_provider_binary = camera_hal.get("oem_provider_binary")
    if (not isinstance(oem_provider_binary, str)
            or not oem_provider_binary.startswith(("/vendor/bin/hw/", "/odm/bin/hw/"))
            or any(character not in
                   "/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@._+-"
                   for character in oem_provider_binary)):
        fail("Profile camera_hal.oem_provider_binary is invalid")
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
    kernel_hash = sha256(kernel_module)
    provider_hash = sha256(provider)
    ffmpeg_hash = sha256(ffmpeg)
    ffmpeg_manifest_hash = sha256(ffmpeg_manifest)
    ffmpeg_license_hash = sha256(ffmpeg_license)
    if kernel_hash != require_hex64(hashes.get("kernel_module_sha256"),
                                    "Profile hashes.kernel_module_sha256"):
        fail("v4l2loopback kernel module hash does not match Profile")
    if provider_hash != require_hex64(hashes.get("global_provider_sha256"),
                                      "Profile hashes.global_provider_sha256"):
        fail("Global Camera Provider hash does not match Profile")
    if ffmpeg_hash != require_hex64(hashes.get("ffmpeg_sha256"),
                                    "Profile hashes.ffmpeg_sha256"):
        fail("FFmpeg binary hash does not match Profile")
    if ffmpeg_manifest_hash != require_hex64(
            hashes.get("ffmpeg_manifest_sha256"),
            "Profile hashes.ffmpeg_manifest_sha256"):
        fail("FFmpeg manifest hash does not match Profile")
    if ffmpeg_license_hash != require_hex64(
            hashes.get("ffmpeg_license_sha256"),
            "Profile hashes.ffmpeg_license_sha256"):
        fail("FFmpeg LGPL license text hash does not match Profile")
    mapping = {
        "profile_sha256": sha256(profile_path),
        "profile_signature_sha256": sha256(signature_path),
        "validation_status": "VERIFIED",
        "vendor_family": projection_value(profile["vendor_family"], "vendor_family"),
        "soc_family": projection_value(profile["soc_family"], "soc_family"),
        "camera_hal_transport": projection_value(
            camera_hal["transport"], "camera_hal.transport"),
        "replacement_scope": "global-front-back",
        "provider_interface":
            "android.hardware.camera.provider@2.4::ICameraProvider/legacy/0",
        "back_camera_id": "0",
        "front_camera_id": "1",
        "oem_provider_service": projection_value(
            oem_provider_service, "camera_hal.oem_provider_service"),
        "oem_provider_binary": projection_value(
            oem_provider_binary, "camera_hal.oem_provider_binary"),
        "manufacturer": projection_value(device["manufacturer"], "device.manufacturer"),
        "product": projection_value(device["product"], "device.product"),
        "device": projection_value(device["device"], "device.device"),
        "api": str(profile["api"]),
        "system_fingerprint_sha256": require_hex64(
            build.get("system_fingerprint_sha256"), "system fingerprint"),
        "vendor_fingerprint_sha256": require_hex64(
            build.get("vendor_fingerprint_sha256"), "vendor fingerprint"),
        "kernel_release_sha256": require_hex64(
            build.get("kernel_release_sha256"), "kernel release"),
        "cameraserver_sha256": require_hex64(
            hashes.get("cameraserver_sha256"), "cameraserver hash"),
        "camera_provider_sha256": require_hex64(
            hashes.get("camera_provider_sha256"), "camera provider hash"),
        "oem_provider_binary_sha256": require_hex64(
            hashes.get("oem_provider_binary_sha256"), "OEM provider binary hash"),
        "vendor_camera_libraries_sha256": require_hex64(
            hashes.get("vendor_camera_libraries_sha256"), "vendor camera hash"),
        "graphics_stack_sha256": require_hex64(
            hashes.get("graphics_stack_sha256"), "graphics stack hash"),
        "kernel_module_sha256": kernel_hash,
        "global_provider_sha256": provider_hash,
        "ffmpeg_sha256": ffmpeg_hash,
        "ffmpeg_manifest_sha256": ffmpeg_manifest_hash,
        "ffmpeg_license_sha256": ffmpeg_license_hash,
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


def bridge_values(values: dict[str, str], runtime: dict[str, str] | None,
                  controller_apk_sha256: str) -> list[tuple[str, str]]:
    return [
        ("bridge_schema", values["bridgeSchema"]),
        ("version_name", values["versionName"]),
        ("version_code", values["versionCode"]),
        ("daemon_protocol", values["daemonProtocol"]),
        ("provider_protocol", values["providerProtocol"]),
        ("profile_schema", values["profileSchema"]),
        ("controller_package", "io.github.gushu101.vcames"),
        ("controller_apk_sha256", controller_apk_sha256),
        ("device_pack_id", runtime["compatibility_id"] if runtime else "none"),
        ("device_pack_profile_sha256", runtime["profile_sha256"] if runtime else "none"),
        ("device_pack_kernel_sha256",
         runtime["kernel_module_sha256"] if runtime else "none"),
        ("device_pack_provider_sha256",
         runtime["global_provider_sha256"] if runtime else "none"),
        ("device_pack_ffmpeg_sha256",
         runtime["ffmpeg_sha256"] if runtime else "none"),
    ]


def build(args: argparse.Namespace) -> tuple[Path, Path]:
    if args.api not in range(30, 35):
        fail("Pixel 5 global Provider runtime requires API 30 through 34")
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
        fail("vcamesd and vcames-socket-proxy are required")

    gradle = ROOT / ("gradlew.bat" if os.name == "nt" else "gradlew")
    subprocess.run([str(gradle), ":app:assembleDebug"], cwd=ROOT, check=True)
    built_apk = ROOT / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk"
    if not built_apk.is_file():
        fail("Gradle did not produce the controller APK")
    controller_apk_hash = sha256(built_apk)

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
        binary_dir = stage_path / "bin"
        binary_dir.mkdir(exist_ok=True)
        shutil.copy2(daemon, binary_dir / "vcamesd")
        shutil.copy2(proxy, binary_dir / "vcames-socket-proxy")

        runtime: dict[str, str] | None = None
        if (args.kernel_module or args.provider or args.ffmpeg or args.ffmpeg_manifest
                or args.ffmpeg_license):
            if not all((args.kernel_module, args.provider, args.ffmpeg,
                        args.ffmpeg_manifest, args.ffmpeg_license)):
                fail("--kernel-module, --provider, --ffmpeg, --ffmpeg-manifest and --ffmpeg-license must be supplied together")
            if not args.profile or not args.profile_signature or not args.profile_public_key:
                fail("device assets require --profile, --profile-signature and --profile-public-key")
            verify_profile_signature(args.profile_public_key, args.profile,
                                     args.profile_signature)
            _, runtime = load_profile(args.profile, args.profile_signature,
                                      args.kernel_module, args.provider, args.ffmpeg,
                                      args.ffmpeg_manifest, args.ffmpeg_license,
                                      args.api)
            kernel_dir = stage_path / "kernel"
            kernel_dir.mkdir(exist_ok=True)
            shutil.copy2(args.kernel_module, kernel_dir / "v4l2loopback.ko")
            shutil.copy2(args.provider, binary_dir / "vcames-global-camera-provider")
            shutil.copy2(args.ffmpeg, binary_dir / "ffmpeg")
            shutil.copy2(args.ffmpeg_manifest, stage_path / "ffmpeg.LICENSE.json")
            license_dir = stage_path / "licenses"
            license_dir.mkdir(exist_ok=True)
            shutil.copy2(args.ffmpeg_license, license_dir / "FFmpeg-LGPL-2.1.txt")
            shutil.copy2(args.profile, stage_path / "profile.json")
            shutil.copy2(args.profile_signature, stage_path / "profile.sig")
            projection = "".join(f"{key}={value}\n" for key, value in runtime.items())
            (stage_path / "profile.runtime.properties").write_text(
                projection, encoding="utf-8")

        bridge = "".join(
            f"{key}={value}\n" for key, value in bridge_values(
                values, runtime, controller_apk_hash))
        (stage_path / "bridge.properties").write_text(bridge, encoding="ascii")

        module = args.output or developer / f"VCamES-Pixel5-Runtime-API{args.api}.zip"
        module = module.resolve()
        zip_tree(stage_path, module)
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
    parser.add_argument("--kernel-module", type=Path)
    parser.add_argument("--provider", type=Path)
    parser.add_argument("--ffmpeg", type=Path)
    parser.add_argument("--ffmpeg-manifest", type=Path)
    parser.add_argument("--ffmpeg-license", type=Path)
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
    print(f"Pixel 5 Runtime (install separately): {module}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
