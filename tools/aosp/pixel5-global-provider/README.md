# Pixel 5 global camera Provider

This source overlay is the system-level replacement backend for stock Pixel 5
Android 11–14. It changes the AOSP HIDL 2.4 external device implementation so
the already-declared `legacy/0` Provider exposes two internal camera names:

- `device@3.4/legacy/0`: back-facing, orientation 90°;
- `device@3.4/legacy/1`: front-facing, orientation 270°.

Both open `/dev/video100`. CameraService therefore keeps the public IDs apps
already request; no app package, Camera Injection API, Xposed hook, or extra
external-camera ID is involved. Each replacement device reports resource cost
50 so IDs 0 and 1 can read the same loopback stream concurrently. The external
hotplug scanner is not started; it cannot publish an accidental third camera.

Run against the exact AOSP tag/branch and vendor environment for the phone's
installed stock OTA:

```bash
python3 apply_global_provider.py /path/to/aosp
source build/envsetup.sh
lunch aosp_redfin-userdebug   # use the matching Pixel build configuration
m vcames-global-camera-provider
```

The patcher aborts if any required upstream anchor drifted. Build API 30, 31,
32, 33, and 34 artifacts separately; never reuse a Provider across an OTA.
Package only the generated arm64 binary whose hash is recorded in a signed v2
Profile. The runtime stops the exact OEM init service recorded by that Profile,
registers this binary as `legacy/0`, and restores the OEM service on every
failure path.

The fail-closed source transform is regression-checked against the official
`android-11.0.0_r48`, `android-12.1.0_r27`, `android-13.0.0_r75`, and
`android-14.0.0_r75` Camera HAL trees. That checks source compatibility only;
it does not replace exact-OTA Soong compilation or the physical Pixel 5 gates.

The build target compiles the patched 2.4 Provider and 3.4 external device,
session, and utility sources directly into one binary. It deliberately does not
link the stock `android.hardware.camera.provider@2.4-external` or
`camera.device@3.4-external-impl` shared libraries, because doing so would load
unpatched OTA libraries at runtime.

The overlay modifies Apache-2.0 AOSP sources. It does not copy or redistribute
Google proprietary camera HAL binaries.
