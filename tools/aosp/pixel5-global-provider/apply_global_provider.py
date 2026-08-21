#!/usr/bin/env python3
"""Create the exact-build Pixel 5 legacy/0 global replacement Provider.

The script patches only known AOSP anchors and aborts on any drift. Run it in
the source checkout matching the phone's stock OTA, then build the named arm64
target. It never patches a device or imports proprietary Pixel camera files.
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


PROVIDER_REL = Path(
    "hardware/interfaces/camera/provider/2.4/default/"
    "ExternalCameraProviderImpl_2_4.cpp"
)
DEVICE_HEADER_REL = Path(
    "hardware/interfaces/camera/device/3.4/default/include/"
    "ext_device_v3_4_impl/ExternalCameraDevice_3_4.h"
)
DEVICE_SOURCE_REL = Path(
    "hardware/interfaces/camera/device/3.4/default/ExternalCameraDevice.cpp"
)
BLUEPRINT_REL = Path("hardware/interfaces/camera/provider/2.4/default/Android.bp")
DEVICE_BLUEPRINT_REL = Path("hardware/interfaces/camera/device/3.4/default/Android.bp")
PROVIDER_MARKER = "name: \"vcames-global-camera-provider\""
DEVICE_MARKER = "name: \"vcames-camera-device-3-4\""


def replace_section(text: str, start: str, end: str, replacement: str,
                    label: str) -> str:
    first = text.find(start)
    if first < 0:
        raise ValueError(f"missing {label} start anchor")
    second = text.find(end, first + len(start))
    if second < 0:
        raise ValueError(f"missing {label} end anchor")
    if text.find(start, first + 1) >= 0:
        raise ValueError(f"ambiguous {label} start anchor")
    return text[:first] + replacement + text[second:]


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise ValueError(f"expected one {label} anchor, found {count}")
    return text.replace(old, new, 1)


def replace_one_of(text: str, anchors: tuple[str, ...], new: str,
                   label: str) -> str:
    matches = [(anchor, text.count(anchor)) for anchor in anchors]
    total = sum(count for _, count in matches)
    if total != 1:
        raise ValueError(f"expected one {label} anchor, found {total}")
    anchor = next(anchor for anchor, count in matches if count == 1)
    return text.replace(anchor, new, 1)


def patch_provider(text: str) -> str:
    helper = r'''namespace {
const std::regex kDeviceNameRE("device@3\\.4/legacy/(0|1)");
constexpr const char* kReplacementDevicePath = "/dev/video100";
constexpr int kMaxDevicePathLen = 256;
constexpr char kDevicePath[] = "/dev/";
constexpr char kPrefix[] = "video";
constexpr int kPrefixLen = sizeof(kPrefix) - 1;

bool matchDeviceName(const hidl_string& deviceName, std::string* logicalId) {
    std::smatch match;
    const std::string name(deviceName.c_str());
    if (!std::regex_match(name, match, kDeviceNameRE)) {
        return false;
    }
    if (logicalId != nullptr) {
        *logicalId = match[1];
    }
    return true;
}

'''
    text = replace_section(text, "namespace {", "} // anonymous namespace",
                           helper, "provider helper")
    text = replace_one_of(
        text,
        (
            '    mHotPlugThread.run("ExtCamHotPlug", PRIORITY_BACKGROUND);',
            '    mHotPlugThread->run("ExtCamHotPlug", PRIORITY_BACKGROUND);',
        ),
        "    // Fixed IDs 0 and 1 are always present; no /dev hotplug publisher.",
        "external hotplug thread start",
    )

    camera_list = '''Return<void> ExternalCameraProviderImpl_2_4::getCameraIdList(
        ICameraProvider::getCameraIdList_cb _hidl_cb) {
    hidl_vec<hidl_string> names;
    names.resize(2);
    names[0] = "device@3.4/legacy/0";
    names[1] = "device@3.4/legacy/1";
    _hidl_cb(Status::OK, names);
    return Void();
}

'''
    text = replace_section(
        text,
        "Return<void> ExternalCameraProviderImpl_2_4::getCameraIdList(",
        "Return<void> ExternalCameraProviderImpl_2_4::isSetTorchModeSupported(",
        camera_list,
        "camera ID list",
    )

    interface = '''Return<void> ExternalCameraProviderImpl_2_4::getCameraDeviceInterface_V3_x(
        const hidl_string& cameraDeviceName,
        ICameraProvider::getCameraDeviceInterface_V3_x_cb _hidl_cb) {
    std::string logicalId;
    if (!matchDeviceName(cameraDeviceName, &logicalId)) {
        _hidl_cb(Status::ILLEGAL_ARGUMENT, nullptr);
        return Void();
    }
    const bool front = logicalId == "1";
    const uint8_t facing = front ? ANDROID_LENS_FACING_FRONT
                                 : ANDROID_LENS_FACING_BACK;
    const int32_t orientation = front ? 270 : 90;
    sp<device::V3_4::implementation::ExternalCameraDevice> deviceImpl =
            new device::V3_4::implementation::ExternalCameraDevice(
                    kReplacementDevicePath, mCfg, logicalId, facing, orientation);
    if (deviceImpl == nullptr || deviceImpl->isInitFailed()) {
        ALOGE("%s: logical camera %s initialization failed", __FUNCTION__,
              logicalId.c_str());
        _hidl_cb(Status::INTERNAL_ERROR, nullptr);
        return Void();
    }
    _hidl_cb(Status::OK, deviceImpl->getInterface());
    return Void();
}

'''
    text = replace_section(
        text,
        "Return<void> ExternalCameraProviderImpl_2_4::getCameraDeviceInterface_V3_x(",
        "void ExternalCameraProviderImpl_2_4::addExternalCamera(",
        interface,
        "camera interface",
    )

    no_hotplug_publish = '''void ExternalCameraProviderImpl_2_4::addExternalCamera(
        const char* devName) {
    // The replacement exports only fixed internal IDs 0 and 1. Never publish
    // /dev/video100 as a third LENS_FACING_EXTERNAL camera.
    (void)devName;
}

'''
    text = replace_section(
        text,
        "void ExternalCameraProviderImpl_2_4::addExternalCamera(",
        "void ExternalCameraProviderImpl_2_4::deviceAdded(",
        no_hotplug_publish,
        "external camera publication",
    )
    no_hotplug_add = '''void ExternalCameraProviderImpl_2_4::deviceAdded(
        const char* devName) {
    (void)devName;
}

'''
    text = replace_section(
        text,
        "void ExternalCameraProviderImpl_2_4::deviceAdded(",
        "void ExternalCameraProviderImpl_2_4::deviceRemoved(",
        no_hotplug_add,
        "external camera addition",
    )
    no_hotplug_remove = '''void ExternalCameraProviderImpl_2_4::deviceRemoved(
        const char* devName) {
    (void)devName;
}

'''
    return replace_section(
        text,
        "void ExternalCameraProviderImpl_2_4::deviceRemoved(",
        "ExternalCameraProviderImpl_2_4::HotplugThread::HotplugThread(",
        no_hotplug_remove,
        "external camera removal",
    )


def patch_device_header(text: str) -> str:
    constructor = (
        "    ExternalCameraDevice(const std::string& cameraId, "
        "const ExternalCameraConfig& cfg);"
    )
    with_replacement = constructor + '''
    ExternalCameraDevice(const std::string& devicePath,
                         const ExternalCameraConfig& cfg,
                         const std::string& logicalCameraId,
                         uint8_t replacementFacing,
                         int32_t replacementOrientation);'''
    text = replace_once(text, constructor, with_replacement,
                        "external device constructor")
    field = "    const ExternalCameraConfig& mCfg;"
    fields = field + '''
    uint8_t mReplacementFacing = 2;  // ANDROID_LENS_FACING_EXTERNAL
    int32_t mReplacementOrientation = 0;'''
    return replace_once(text, field, fields, "external device fields")


def patch_device_source(text: str) -> str:
    destructor = "ExternalCameraDevice::~ExternalCameraDevice() {}"
    replacement_constructor = '''ExternalCameraDevice::ExternalCameraDevice(
        const std::string& devicePath,
        const ExternalCameraConfig& cfg,
        const std::string& logicalCameraId,
        uint8_t replacementFacing,
        int32_t replacementOrientation) :
        mCameraId(logicalCameraId),
        mDevicePath(devicePath),
        mCfg(cfg),
        mReplacementFacing(replacementFacing),
        mReplacementOrientation(replacementOrientation) {}

'''
    text = replace_once(text, destructor, replacement_constructor + destructor,
                        "external device destructor")
    text = replace_once(
        text,
        "const uint8_t facing = ANDROID_LENS_FACING_EXTERNAL;",
        "const uint8_t facing = mReplacementFacing;",
        "lens facing metadata",
    )
    text = replace_once(
        text,
        "const int32_t orientation = mCfg.orientation;",
        "const int32_t orientation = mReplacementOrientation;",
        "sensor orientation metadata",
    )
    return replace_once(
        text,
        "resCost.resourceCost = 100;",
        "resCost.resourceCost = 50;  // IDs 0 and 1 may read /dev/video100 concurrently.",
        "replacement device resource cost",
    )


def patch_tree(aosp_root: Path) -> None:
    here = Path(__file__).resolve().parent
    transforms = {
        PROVIDER_REL: patch_provider,
        DEVICE_HEADER_REL: patch_device_header,
        DEVICE_SOURCE_REL: patch_device_source,
    }
    staged: dict[Path, str] = {}

    # Compute and validate every edit before touching the checkout. In
    # particular, an anchor drift in a later file must not leave a half-patched
    # Provider tree behind.
    for relative, transform in transforms.items():
        target = aosp_root / relative
        if not target.is_file():
            raise ValueError(f"missing AOSP source: {relative}")
        original = target.read_text(encoding="utf-8")
        patched = transform(original)
        if patched == original:
            raise ValueError(f"patch made no change: {relative}")
        staged[target] = patched

    provider_dir = (aosp_root / PROVIDER_REL).parent
    service_source = here / "vcames-global-service.cpp"
    if not service_source.is_file():
        raise ValueError("missing vcames-global-service.cpp overlay")
    service_target = provider_dir / service_source.name
    if service_target.exists():
        raise ValueError(f"overlay already exists: {service_target.relative_to(aosp_root)}")

    blueprints = (
        (BLUEPRINT_REL, PROVIDER_MARKER, "Android.bp.fragment"),
        (DEVICE_BLUEPRINT_REL, DEVICE_MARKER, "Android.bp.device.fragment"),
    )
    for relative, marker, fragment_name in blueprints:
        blueprint = aosp_root / relative
        if not blueprint.is_file():
            raise ValueError(f"missing AOSP blueprint: {relative}")
        blueprint_text = blueprint.read_text(encoding="utf-8")
        if marker in blueprint_text:
            raise ValueError(f"VCamES Android.bp module already exists in {relative}")
        fragment_path = here / fragment_name
        if not fragment_path.is_file():
            raise ValueError(f"missing overlay fragment: {fragment_name}")
        fragment = fragment_path.read_text(encoding="utf-8")
        staged[blueprint] = blueprint_text.rstrip() + "\n\n" + fragment

    for target, contents in staged.items():
        target.write_text(contents, encoding="utf-8")
    shutil.copy2(service_source, service_target)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("aosp_root", type=Path)
    args = parser.parse_args()
    try:
        patch_tree(args.aosp_root.resolve())
    except (OSError, ValueError) as failure:
        parser.error(str(failure))
    print("Patched exact AOSP tree. Build: m vcames-global-camera-provider")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
