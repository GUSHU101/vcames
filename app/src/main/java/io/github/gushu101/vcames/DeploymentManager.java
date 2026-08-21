package io.github.gushu101.vcames;

import android.content.Context;
import android.os.Build;

/** Checks an already-rooted Pixel 5. It never installs Magisk, KernelSU, or a Root module. */
final class DeploymentManager {
    String actionLabel() {
        return "检测 ROOT 权限";
    }

    String checkRootAccess(Context context) {
        Pixel5Support.Result device = Pixel5Support.inspect();
        if (!device.platformAccepted()) {
            return "设备不在支持范围：" + device.rejection();
        }
        if (!RootAccess.isGranted()) {
            return "未获得 ROOT：请在已安装的 KSU/Magisk 授权界面允许 VCamES，然后重试。"
                    + "APK 不会安装或修改 ROOT 工具。";
        }
        return "ROOT 检测通过：VCamES 的 su 命令得到 uid 0。\n"
                + "设备：Pixel 5 (redfin) · API " + Build.VERSION.SDK_INT + "\n"
                + "后端：" + device.backend + "\n"
                + "APK 未安装、升级或配置任何 ROOT 管理器。";
    }

    StartReadiness checkStartReadiness(Context context) {
        Pixel5Support.Result device = Pixel5Support.inspect();
        if (!device.platformAccepted()) {
            return StartReadiness.blocked("UNSUPPORTED_DEVICE", device.rejection());
        }
        if (!RootAccess.isGranted()) {
            return StartReadiness.blocked(
                    "ROOT_NOT_GRANTED", "su 未返回 uid 0，请先给 VCamES 授予 ROOT 权限");
        }
        String diagnostic = diagnostics(context);
        if (!"ready".equals(field(diagnostic, "video_device="))) {
            return StartReadiness.blocked(
                    "CAMERA_RUNTIME_NOT_READY",
                    "未发现 /dev/video100。APK 只检测和使用已有 ROOT，不会安装 KSU/Magisk 模块或加载未知内核模块");
        }
        if (!"READY_GLOBAL_FRONT_BACK".equals(field(diagnostic, "runtime_status="))
                || "stopped".equals(field(diagnostic, "provider_pid="))) {
            return StartReadiness.blocked(
                    "CAMERA_RUNTIME_NOT_READY",
                    "Pixel 5 legacy/0 全局 Provider 尚未完成安全接管；请预先安装与当前原厂版本精确匹配的独立运行时");
        }
        return StartReadiness.ready(
                "ROOT、Pixel 5 平台、V4L2 节点和相机 0/1 全局 Provider 已就绪");
    }

    String diagnostics(Context context) {
        Pixel5Support.Result device = Pixel5Support.inspect();
        if (!RootAccess.isGranted()) {
            return "root_granted=false\n"
                    + "platform_supported=" + device.platformAccepted() + "\n"
                    + "camera_backend=" + device.backend;
        }
        String command = "printf 'root_granted=true\\nroot_uid='; id -u; "
                + "printf 'selinux='; getenforce 2>/dev/null || printf UNKNOWN; "
                + "printf '\\nkernel_release='; uname -r; "
                + "printf '\\nruntime_status='; tr -d '\\\\r\\\\n ' </data/adb/vcames/status.txt 2>/dev/null || printf UNKNOWN; "
                + "printf '\\nvideo_device='; [ -c /dev/video100 ] && printf ready || printf missing; "
                + "printf '\\nprovider_pid='; pidof vcames-global-camera-provider 2>/dev/null || printf stopped; "
                + "printf '\\nprovider_hal='; lshal 2>/dev/null | grep -F 'android.hardware.camera.provider@2.4::ICameraProvider/legacy/0' | head -n 1 | tr -d '\\\\r\\\\n' || printf missing; "
                + "printf '\\nffmpeg='; [ -x /data/adb/modules/vcames_root_bridge/bin/ffmpeg ] && printf ready || printf missing; "
                + "printf '\\ndaemon_pid='; pidof vcamesd 2>/dev/null || printf stopped";
        return "platform_supported=" + device.platformAccepted() + "\n"
                + "camera_backend=" + device.backend + "\n"
                + RootAccess.run(command, 20).summary();
    }

    private static String field(String output, String prefix) {
        int start = output.indexOf(prefix);
        if (start < 0) return "";
        start += prefix.length();
        int end = start;
        while (end < output.length() && !Character.isWhitespace(output.charAt(end))) end++;
        return output.substring(start, end);
    }

    static final class StartReadiness {
        final boolean ready;
        final String state;
        final String message;

        private StartReadiness(boolean ready, String state, String message) {
            this.ready = ready;
            this.state = state;
            this.message = message;
        }

        static StartReadiness ready(String message) {
            return new StartReadiness(true, "READY", message);
        }

        static StartReadiness blocked(String state, String message) {
            return new StartReadiness(false, state, message);
        }
    }
}
