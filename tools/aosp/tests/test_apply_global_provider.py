import importlib.util
import tempfile
import unittest
from pathlib import Path


MODULE = Path(__file__).parents[1] / "pixel5-global-provider" / "apply_global_provider.py"
SPEC = importlib.util.spec_from_file_location("apply_global_provider", MODULE)
assert SPEC and SPEC.loader
PATCHER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCHER)


class GlobalProviderPatcherTest(unittest.TestCase):
    def test_provider_contract_is_global_ids_zero_and_one(self):
        source = """namespace {
old helper
} // anonymous namespace
    mHotPlugThread.run("ExtCamHotPlug", PRIORITY_BACKGROUND);
Return<void> ExternalCameraProviderImpl_2_4::getCameraIdList(
old list
Return<void> ExternalCameraProviderImpl_2_4::isSetTorchModeSupported(
torch
Return<void> ExternalCameraProviderImpl_2_4::getCameraDeviceInterface_V3_x(
old interface
void ExternalCameraProviderImpl_2_4::addExternalCamera(
old add
void ExternalCameraProviderImpl_2_4::deviceAdded(
device add
void ExternalCameraProviderImpl_2_4::deviceRemoved(
old remove
ExternalCameraProviderImpl_2_4::HotplugThread::HotplugThread(
tail
"""
        patched = PATCHER.patch_provider(source)
        self.assertIn('"device@3.4/legacy/0"', patched)
        self.assertIn('"device@3.4/legacy/1"', patched)
        self.assertIn('kReplacementDevicePath = "/dev/video100"', patched)
        self.assertIn("ANDROID_LENS_FACING_FRONT", patched)
        self.assertIn("ANDROID_LENS_FACING_BACK", patched)
        self.assertNotIn("old interface", patched)
        self.assertNotIn("device add", patched)
        self.assertNotIn("old remove", patched)
        self.assertNotIn('mHotPlugThread.run("ExtCamHotPlug"', patched)

    def test_android_14_pointer_hotplug_anchor_is_supported(self):
        source = """namespace {
old helper
} // anonymous namespace
    mHotPlugThread->run("ExtCamHotPlug", PRIORITY_BACKGROUND);
Return<void> ExternalCameraProviderImpl_2_4::getCameraIdList(
old list
Return<void> ExternalCameraProviderImpl_2_4::isSetTorchModeSupported(
torch
Return<void> ExternalCameraProviderImpl_2_4::getCameraDeviceInterface_V3_x(
old interface
void ExternalCameraProviderImpl_2_4::addExternalCamera(
old add
void ExternalCameraProviderImpl_2_4::deviceAdded(
device add
void ExternalCameraProviderImpl_2_4::deviceRemoved(
old remove
ExternalCameraProviderImpl_2_4::HotplugThread::HotplugThread(
tail
"""
        patched = PATCHER.patch_provider(source)
        self.assertNotIn('mHotPlugThread->run("ExtCamHotPlug"', patched)
        self.assertIn('"device@3.4/legacy/1"', patched)

    def test_device_metadata_uses_replacement_facing(self):
        header = """    ExternalCameraDevice(const std::string& cameraId, const ExternalCameraConfig& cfg);
    const ExternalCameraConfig& mCfg;
"""
        source = """ExternalCameraDevice::~ExternalCameraDevice() {}
const uint8_t facing = ANDROID_LENS_FACING_EXTERNAL;
const int32_t orientation = mCfg.orientation;
resCost.resourceCost = 100;
"""
        patched_header = PATCHER.patch_device_header(header)
        patched_source = PATCHER.patch_device_source(source)
        self.assertIn("logicalCameraId", patched_header)
        self.assertIn("mReplacementFacing", patched_header)
        self.assertIn("mCameraId(logicalCameraId)", patched_source)
        self.assertIn("const uint8_t facing = mReplacementFacing;", patched_source)
        self.assertIn("const int32_t orientation = mReplacementOrientation;", patched_source)
        self.assertIn("resCost.resourceCost = 50;", patched_source)

    def test_anchor_drift_fails_closed(self):
        with self.assertRaises(ValueError):
            PATCHER.patch_provider("upstream changed")

    def test_tree_preflight_never_leaves_partial_patch(self):
        provider = """namespace {
old helper
} // anonymous namespace
    mHotPlugThread.run(\"ExtCamHotPlug\", PRIORITY_BACKGROUND);
Return<void> ExternalCameraProviderImpl_2_4::getCameraIdList(
old list
Return<void> ExternalCameraProviderImpl_2_4::isSetTorchModeSupported(
torch
Return<void> ExternalCameraProviderImpl_2_4::getCameraDeviceInterface_V3_x(
old interface
void ExternalCameraProviderImpl_2_4::addExternalCamera(
old add
void ExternalCameraProviderImpl_2_4::deviceAdded(
device add
void ExternalCameraProviderImpl_2_4::deviceRemoved(
old remove
ExternalCameraProviderImpl_2_4::HotplugThread::HotplugThread(
tail
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            provider_path = root / PATCHER.PROVIDER_REL
            header_path = root / PATCHER.DEVICE_HEADER_REL
            source_path = root / PATCHER.DEVICE_SOURCE_REL
            provider_bp = root / PATCHER.BLUEPRINT_REL
            device_bp = root / PATCHER.DEVICE_BLUEPRINT_REL
            for path in (provider_path, header_path, source_path,
                         provider_bp, device_bp):
                path.parent.mkdir(parents=True, exist_ok=True)
            provider_path.write_text(provider, encoding="utf-8")
            # Deliberate later-file anchor drift: the provider transform itself
            # succeeds, but the device header transform must fail.
            header_path.write_text("changed upstream", encoding="utf-8")
            source_path.write_text("changed upstream", encoding="utf-8")
            provider_bp.write_text("provider bp", encoding="utf-8")
            device_bp.write_text("device bp", encoding="utf-8")

            with self.assertRaises(ValueError):
                PATCHER.patch_tree(root)

            self.assertEqual(provider_path.read_text(encoding="utf-8"), provider)
            self.assertFalse((provider_path.parent /
                              "vcames-global-service.cpp").exists())

    def test_provider_binary_embeds_all_patched_sources(self):
        blueprint = (MODULE.parent / "Android.bp.fragment").read_text(
            encoding="utf-8")
        device_blueprint = (MODULE.parent / "Android.bp.device.fragment").read_text(
            encoding="utf-8")
        self.assertIn('"ExternalCameraProviderImpl_2_4.cpp"', blueprint)
        self.assertIn('"vcames-camera-device-3-4"', blueprint)
        self.assertIn('"ExternalCameraDevice.cpp"', device_blueprint)
        self.assertIn('"ExternalCameraDeviceSession.cpp"', device_blueprint)
        self.assertIn('"ExternalCameraUtils.cpp"', device_blueprint)
        self.assertNotIn(
            '"android.hardware.camera.provider@2.4-external",', blueprint)
        self.assertNotIn('"camera.device@3.4-external-impl",', blueprint)


if __name__ == "__main__":
    unittest.main()
