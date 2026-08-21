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
    def make_profile(self, directory: Path, adapter: Path, status: str = "VERIFIED") -> Path:
        digest = hashlib.sha256(adapter.read_bytes()).hexdigest()
        profile = {
            "schema_version": 1,
            "compatibility_id": "a" * 64,
            "vendor_family": "google",
            "soc_family": "tensor",
            "api": 33,
            "device": {"manufacturer": "Google", "product": "oriole",
                       "device": "oriole", "model": "Pixel 6"},
            "build": {"build_id": "TQ3A", "security_patch": "2023-08-05",
                      "system_fingerprint_sha256": "b" * 64,
                      "vendor_fingerprint_sha256": "c" * 64},
            "camera_hal": {"transport": "aidl", "provider_instance": "internal/0"},
            "hashes": {"adapter_sha256": digest, "cameraserver_sha256": "d" * 64,
                       "camera_provider_sha256": "e" * 64,
                       "vendor_camera_libraries_sha256": "f" * 64,
                       "graphics_stack_sha256": "1" * 64},
            "capabilities": {"front": True, "back": True},
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
            adapter = root / "adapter"
            adapter.write_bytes(b"adapter")
            signature = root / "profile.sig"
            signature.write_bytes(b"signature")
            profile = self.make_profile(root, adapter)
            _, projection = BUILDER.load_profile(profile, signature, adapter, 33)
            self.assertEqual(projection["compatibility_id"], "a" * 64)
            self.assertEqual(projection["adapter_sha256"],
                             hashlib.sha256(b"adapter").hexdigest())
            self.assertEqual(projection["camera_hal_transport"], "aidl")

    def test_unverified_profile_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            adapter = root / "adapter"
            adapter.write_bytes(b"adapter")
            signature = root / "profile.sig"
            signature.write_bytes(b"signature")
            profile = self.make_profile(root, adapter, "CANDIDATE")
            with self.assertRaises(ValueError):
                BUILDER.load_profile(profile, signature, adapter, 33)


if __name__ == "__main__":
    unittest.main()
