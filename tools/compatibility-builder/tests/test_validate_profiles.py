import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).parents[1] / "validate_profiles.py"
SPEC = importlib.util.spec_from_file_location("validate_profiles", MODULE_PATH)
assert SPEC and SPEC.loader
VALIDATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATOR)

SIGN_PATH = Path(__file__).parents[1] / "profile_sign.py"
SIGN_SPEC = importlib.util.spec_from_file_location("profile_sign", SIGN_PATH)
assert SIGN_SPEC and SIGN_SPEC.loader
SIGNER = importlib.util.module_from_spec(SIGN_SPEC)
SIGN_SPEC.loader.exec_module(SIGNER)


class CatalogValidationTest(unittest.TestCase):
    def test_empty_development_catalog(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "catalog.json"
            path.write_text(json.dumps({
                "schema_version": 1,
                "product_scope": "pixel5-redfin-android11-14-global-provider",
                "catalog_status": "EMPTY_NO_VERIFIED_PROFILES",
                "entries": [],
                "signature": {"status": "UNSIGNED_DEVELOPMENT", "algorithm": "Ed25519",
                              "key_id": "", "file": ""},
            }), encoding="utf-8")
            VALIDATOR.validate_catalog(path)

    def test_rejects_unverified_nonempty_catalog(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "catalog.json"
            path.write_text(json.dumps({
                "schema_version": 1,
                "product_scope": "pixel5-redfin-android11-14-global-provider",
                "catalog_status": "SIGNED_VERIFIED_PROFILES",
                "entries": [{"compatibility_id": "0" * 64, "profile": "missing.json",
                             "signature": "missing.sig", "status": "CANDIDATE"}],
                "signature": {"status": "SIGNED", "algorithm": "Ed25519",
                              "key_id": "test", "file": "catalog.sig"},
            }), encoding="utf-8")
            with self.assertRaises(VALIDATOR.ValidationError):
                VALIDATOR.validate_catalog(path)

    def test_rejects_duplicate_json_keys(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "catalog.json"
            path.write_text('{"schema_version":1,"schema_version":1}', encoding="utf-8")
            with self.assertRaises(VALIDATOR.ValidationError):
                VALIDATOR.validate_catalog(path)

    def test_canonical_json_is_stable(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "profile.json"
            path.write_text('{"z": 1, "a": "测试"}', encoding="utf-8")
            self.assertEqual(SIGNER.canonical_bytes(path), '{"a":"测试","z":1}\n'.encode())


if __name__ == "__main__":
    unittest.main()
