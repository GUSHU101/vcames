import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class RuntimeContractTest(unittest.TestCase):
    def setUp(self):
        self.service = (ROOT / "root-module/template/service.sh").read_text(
            encoding="utf-8")
        self.customize = (ROOT / "root-module/template/customize.sh").read_text(
            encoding="utf-8")

    def test_global_provider_replaces_legacy_zero(self):
        interface = (
            "android.hardware.camera.provider@2.4::ICameraProvider/legacy/0")
        self.assertIn(interface, self.service)
        self.assertIn("back_camera_id", self.service)
        self.assertIn("front_camera_id", self.service)
        self.assertIn("READY_GLOBAL_FRONT_BACK", self.service)
        self.assertNotIn("camera-injector", self.service)

    def test_oem_is_stopped_only_after_preflight_and_restored_on_exit(self):
        self.assertLess(self.service.index("OEM_FRONT_CAMERA_ID_1_MISSING"),
                        self.service.index('setprop ctl.stop "$oem_service"'))
        self.assertLess(self.service.index('setprop ctl.stop "$oem_service"'),
                        self.service.index("start_provider ||"))
        self.assertLess(self.service.index("start_with_retry start_daemon ||"),
                        self.service.index("start_provider ||"))
        self.assertIn("trap cleanup EXIT", self.service)
        self.assertIn("trap handle_shutdown_signal HUP INT TERM", self.service)
        self.assertIn("stop_and_reap", self.service)
        self.assertIn("--prime-global-camera", self.service)
        self.assertIn('setprop ctl.start "$service_name"', self.service)
        self.assertIn("SAFE_MODE_GLOBAL_PROVIDER_LOST_RESTORING_OEM", self.service)
        self.assertIn("provider_server_pid", self.service)
        self.assertIn('readlink "/proc/$actual_pid/exe"', self.service)
        self.assertIn("oem_provider_binary_sha256", self.service)

    def test_ffmpeg_and_license_are_mandatory(self):
        for item in ("bin/ffmpeg", "ffmpeg.LICENSE.json",
                     "licenses/FFmpeg-LGPL-2.1.txt"):
            self.assertIn(item, self.service)
            self.assertIn(item, self.customize)
        for protocol in ("http", "https", "ffrtmpcrypt", "ffrtmphttp",
                         "rtmp", "rtmps", "rtmpe", "rtmpt",
                         "rtmpte", "rtmpts", "srt", "rist", "rtp", "srtp",
                         "udp", "tcp", "mmsh", "mmst"):
            self.assertIn(protocol, self.service)
        self.assertNotIn("for protocol in http https rtmp rtmps rtsp", self.service)
        for decoder in ("h264", "hevc", "vp8", "vp9", "av1", "mpeg4", "mjpeg"):
            self.assertIn(decoder, self.service)
        for filter_name in ("fps", "scale", "pad"):
            self.assertIn(filter_name, self.service)

    def test_kernelsu_policy_domain_is_specialized(self):
        self.assertIn('${KSU:-false}', self.customize)
        self.assertIn("s/magisk_file/ksu_file/g; s/magisk/ksu/g", self.customize)

    def test_controller_identity_is_uid_and_apk_hash_bound(self):
        self.assertIn("cmd package list packages -U", self.service)
        self.assertIn("controller_apk_sha256", self.service)
        self.assertIn("cmd package path", self.service)
        self.assertIn("CONTROLLER_APK_HASH_MISMATCH", self.service)


if __name__ == "__main__":
    unittest.main()
