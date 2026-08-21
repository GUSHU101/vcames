import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "build_device_pack.py"
SPEC = importlib.util.spec_from_file_location("build_device_pack", MODULE_PATH)
assert SPEC and SPEC.loader
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


class DevicePackBuilderTest(unittest.TestCase):
    def make_ffmpeg_manifest(
            self, directory: Path, ffmpeg: Path, license_text: Path) -> Path:
        manifest = {
            "schema_version": 1,
            "name": "FFmpeg",
            "license": "LGPL-2.1-or-later",
            "source_url": "https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz",
            "source_revision": "n7.1",
            "build_configuration": ["--disable-gpl", "--disable-programs",
                                    "--disable-nonfree", "--enable-ffmpeg"],
            "binary_sha256": hashlib.sha256(ffmpeg.read_bytes()).hexdigest(),
            "license_text_sha256":
                hashlib.sha256(license_text.read_bytes()).hexdigest(),
            "input_protocols": ["http", "https", "httpproxy", "tls", "crypto",
                                "ffrtmpcrypt", "ffrtmphttp",
                                "rtmp", "rtmps", "rtmpe", "rtmpt", "rtmpte",
                                "rtmpts", "srt", "rist", "rtp", "srtp", "udp",
                                "tcp", "mmsh", "mmst"],
            "demuxers": ["hls", "dash", "flv", "rtsp", "rtp", "mpegts",
                         "mjpeg", "asf"],
            "decoders": ["h264", "hevc", "vp8", "vp9", "av1", "mpeg4", "mjpeg"],
            "filters": ["fps", "scale", "pad"],
            "encoders": ["mjpeg"],
        }
        path = directory / "ffmpeg.LICENSE.json"
        path.write_bytes((json.dumps(manifest, ensure_ascii=False, sort_keys=True,
                                     separators=(",", ":")) + "\n").encode("utf-8"))
        return path

    def make_profile(self, directory: Path, kernel: Path, provider: Path,
                     ffmpeg: Path, ffmpeg_manifest: Path, ffmpeg_license: Path,
                     status: str = "VERIFIED") -> Path:
        kernel_digest = hashlib.sha256(kernel.read_bytes()).hexdigest()
        provider_digest = hashlib.sha256(provider.read_bytes()).hexdigest()
        profile = {
            "schema_version": 2,
            "compatibility_id": "a" * 64,
            "vendor_family": "google",
            "soc_family": "qualcomm",
            "api": 33,
            "device": {"manufacturer": "Google", "product": "redfin",
                       "device": "redfin", "model": "Pixel 5"},
            "build": {"build_id": "TQ3A", "security_patch": "2023-08-05",
                      "system_fingerprint_sha256": "b" * 64,
                      "vendor_fingerprint_sha256": "c" * 64,
                      "kernel_release_sha256": "9" * 64},
            "camera_hal": {
                "transport": "hidl",
                "provider_instance": "legacy/0",
                "provider_interface":
                    "android.hardware.camera.provider@2.4::ICameraProvider/legacy/0",
                "registration": "oem-service-takeover",
                "replacement_scope": "global-front-back",
                "back_camera_id": "0",
                "front_camera_id": "1",
                "v4l2_device": "/dev/video100",
                "oem_provider_service": "vendor.camera-provider-2-4",
                "oem_provider_binary":
                    "/vendor/bin/hw/android.hardware.camera.provider@2.4-service_64",
            },
            "hashes": {"kernel_module_sha256": kernel_digest,
                       "global_provider_sha256": provider_digest,
                       "ffmpeg_sha256":
                           hashlib.sha256(ffmpeg.read_bytes()).hexdigest(),
                       "ffmpeg_manifest_sha256":
                           hashlib.sha256(ffmpeg_manifest.read_bytes()).hexdigest(),
                       "ffmpeg_license_sha256":
                           hashlib.sha256(ffmpeg_license.read_bytes()).hexdigest(),
                       "cameraserver_sha256": "d" * 64,
                       "camera_provider_sha256": "e" * 64,
                       "oem_provider_binary_sha256": "8" * 64,
                       "vendor_camera_libraries_sha256": "f" * 64,
                       "graphics_stack_sha256": "1" * 64},
            "capabilities": {"global_front": True, "global_back": True},
            "resources": {"contains_proprietary_oem_files": False,
                          "artifact_sha256": "2" * 64},
            "validation": {"status": status, "report": "validation-results/test.json"},
        }
        report = directory / "validation-results" / "test.json"
        report.parent.mkdir()
        report.write_text(json.dumps({
            "status": "VERIFIED",
            "compatibility_id": "a" * 64,
            "artifact_sha256": "2" * 64,
            "tests": [{"name": "content", "status": "PASS"}],
        }), encoding="utf-8")
        path = directory / "profile.json"
        path.write_bytes((json.dumps(profile, ensure_ascii=False, sort_keys=True,
                                     separators=(",", ":")) + "\n").encode("utf-8"))
        return path

    def test_verified_profile_generates_exact_projection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            kernel = root / "v4l2loopback.ko"
            kernel.write_bytes(b"kernel")
            provider = root / "external-camera-provider"
            provider.write_bytes(b"provider")
            ffmpeg = root / "ffmpeg"
            ffmpeg.write_bytes(b"ffmpeg")
            ffmpeg_license = root / "COPYING.LGPLv2.1"
            ffmpeg_license.write_bytes(b"LGPL 2.1 test fixture")
            ffmpeg_manifest = self.make_ffmpeg_manifest(
                root, ffmpeg, ffmpeg_license)
            signature = root / "profile.sig"
            signature.write_bytes(b"signature")
            profile = self.make_profile(
                root, kernel, provider, ffmpeg, ffmpeg_manifest, ffmpeg_license)
            _, projection = BUILDER.load_profile(
                profile, signature, kernel, provider, ffmpeg, ffmpeg_manifest,
                ffmpeg_license, 33)
            self.assertEqual(projection["compatibility_id"], "a" * 64)
            self.assertEqual(projection["kernel_module_sha256"],
                             hashlib.sha256(b"kernel").hexdigest())
            self.assertEqual(projection["global_provider_sha256"],
                             hashlib.sha256(b"provider").hexdigest())
            self.assertEqual(projection["ffmpeg_sha256"],
                             hashlib.sha256(b"ffmpeg").hexdigest())
            self.assertEqual(projection["profile_signature_sha256"],
                             hashlib.sha256(b"signature").hexdigest())
            self.assertEqual(projection["camera_hal_transport"], "hidl")
            self.assertEqual(
                projection["oem_provider_binary"],
                "/vendor/bin/hw/android.hardware.camera.provider@2.4-service_64")
            bridge = dict(BUILDER.bridge_values({
                "versionName": "3.2.0", "versionCode": "30200",
                "bridgeSchema": "4", "daemonProtocol": "5",
                "providerProtocol": "1", "profileSchema": "2",
            }, projection, "7" * 64))
            self.assertEqual(bridge["device_pack_id"], "a" * 64)
            self.assertEqual(bridge["device_pack_profile_sha256"],
                             projection["profile_sha256"])
            self.assertEqual(bridge["device_pack_kernel_sha256"],
                             projection["kernel_module_sha256"])
            self.assertEqual(bridge["device_pack_provider_sha256"],
                             projection["global_provider_sha256"])
            self.assertEqual(bridge["controller_apk_sha256"], "7" * 64)

    def test_unverified_profile_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            kernel = root / "v4l2loopback.ko"
            kernel.write_bytes(b"kernel")
            provider = root / "external-camera-provider"
            provider.write_bytes(b"provider")
            ffmpeg = root / "ffmpeg"
            ffmpeg.write_bytes(b"ffmpeg")
            ffmpeg_license = root / "COPYING.LGPLv2.1"
            ffmpeg_license.write_bytes(b"LGPL 2.1 test fixture")
            ffmpeg_manifest = self.make_ffmpeg_manifest(
                root, ffmpeg, ffmpeg_license)
            signature = root / "profile.sig"
            signature.write_bytes(b"signature")
            profile = self.make_profile(
                root, kernel, provider, ffmpeg, ffmpeg_manifest, ffmpeg_license,
                "CANDIDATE")
            with self.assertRaises(ValueError):
                BUILDER.load_profile(
                    profile, signature, kernel, provider, ffmpeg, ffmpeg_manifest,
                    ffmpeg_license, 33)

    def test_projection_rejects_newlines(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            kernel = root / "v4l2loopback.ko"
            kernel.write_bytes(b"kernel")
            provider = root / "external-camera-provider"
            provider.write_bytes(b"provider")
            ffmpeg = root / "ffmpeg"
            ffmpeg.write_bytes(b"ffmpeg")
            ffmpeg_license = root / "COPYING.LGPLv2.1"
            ffmpeg_license.write_bytes(b"LGPL 2.1 test fixture")
            ffmpeg_manifest = self.make_ffmpeg_manifest(
                root, ffmpeg, ffmpeg_license)
            signature = root / "profile.sig"
            signature.write_bytes(b"signature")
            profile = self.make_profile(
                root, kernel, provider, ffmpeg, ffmpeg_manifest, ffmpeg_license)
            value = json.loads(profile.read_text(encoding="utf-8"))
            value["device"]["manufacturer"] = "Google\nvalidation_status=VERIFIED"
            profile.write_bytes((json.dumps(value, ensure_ascii=False, sort_keys=True,
                                             separators=(",", ":")) + "\n").encode("utf-8"))
            with self.assertRaises(ValueError):
                BUILDER.load_profile(
                    profile, signature, kernel, provider, ffmpeg, ffmpeg_manifest,
                    ffmpeg_license, 33)

    def test_missing_device_asset_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            kernel = root / "v4l2loopback.ko"
            kernel.write_bytes(b"kernel")
            provider = root / "external-camera-provider"
            provider.write_bytes(b"provider")
            ffmpeg = root / "ffmpeg"
            ffmpeg.write_bytes(b"ffmpeg")
            ffmpeg_license = root / "COPYING.LGPLv2.1"
            ffmpeg_license.write_bytes(b"LGPL 2.1 test fixture")
            ffmpeg_manifest = self.make_ffmpeg_manifest(
                root, ffmpeg, ffmpeg_license)
            signature = root / "profile.sig"
            signature.write_bytes(b"signature")
            profile = self.make_profile(
                root, kernel, provider, ffmpeg, ffmpeg_manifest, ffmpeg_license)
            provider.unlink()
            with self.assertRaises(ValueError):
                BUILDER.load_profile(
                    profile, signature, kernel, provider, ffmpeg, ffmpeg_manifest,
                    ffmpeg_license, 33)

    def test_ffmpeg_gpl_configuration_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            ffmpeg = root / "ffmpeg"
            ffmpeg.write_bytes(b"ffmpeg")
            license_text = root / "COPYING.LGPLv2.1"
            license_text.write_bytes(b"LGPL 2.1 test fixture")
            manifest_path = self.make_ffmpeg_manifest(root, ffmpeg, license_text)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["build_configuration"].append("--enable-gpl")
            manifest_path.write_bytes((json.dumps(
                manifest, ensure_ascii=False, sort_keys=True,
                separators=(",", ":")) + "\n").encode("utf-8"))
            with self.assertRaises(ValueError):
                BUILDER.load_ffmpeg_manifest(manifest_path, ffmpeg, license_text)

    def test_ffmpeg_missing_common_decoder_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            ffmpeg = root / "ffmpeg"
            ffmpeg.write_bytes(b"ffmpeg")
            license_text = root / "COPYING.LGPLv2.1"
            license_text.write_bytes(b"LGPL 2.1 test fixture")
            manifest_path = self.make_ffmpeg_manifest(root, ffmpeg, license_text)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["decoders"].remove("h264")
            manifest_path.write_bytes((json.dumps(
                manifest, ensure_ascii=False, sort_keys=True,
                separators=(",", ":")) + "\n").encode("utf-8"))
            with self.assertRaises(ValueError):
                BUILDER.load_ffmpeg_manifest(manifest_path, ffmpeg, license_text)


if __name__ == "__main__":
    unittest.main()
