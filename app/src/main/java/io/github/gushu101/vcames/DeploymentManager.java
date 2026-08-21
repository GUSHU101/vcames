package io.github.gushu101.vcames;

import android.content.Context;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

/** The single supported deployment path: an ordinary APK using an explicitly granted uid-0 shell. */
final class DeploymentManager {
    private static final String MODULE_ASSET = "vcames-root-bridge.zip";

    String actionLabel() {
        return "授权 ROOT 并部署";
    }

    String authorizeAndDeploy(Context context) {
        if (!RootAccess.isGranted()) {
            return "未获得 ROOT。请在设备的 ROOT 授权界面允许 VCamES。";
        }
        final RootBridgeManifest manifest;
        try {
            manifest = RootBridgeManifest.fromAsset(context);
        } catch (IOException invalidPayload) {
            return "ROOT 已授权，但 APK 内置 Root Bridge 无效：" + invalidPayload.getMessage();
        }
        String diagnostic = diagnostics(context);
        if (diagnostic.contains("module=installed") && manifest.matchesInstalled(diagnostic)) {
            return "ROOT 已授权，Root Bridge " + manifest.versionName + " 已匹配。\n" + diagnostic;
        }

        File payload = new File(context.getCacheDir(), MODULE_ASSET);
        try (InputStream input = context.getAssets().open(MODULE_ASSET);
             FileOutputStream output = new FileOutputStream(payload, false)) {
            byte[] buffer = new byte[16 * 1024];
            int count;
            long total = 0;
            while ((count = input.read(buffer)) != -1) {
                total += count;
                if (total > 128L * 1024L * 1024L) {
                    throw new IOException("Root Bridge exceeds the 128 MiB safety limit");
                }
                output.write(buffer, 0, count);
            }
        } catch (IOException noPayload) {
            return "ROOT 已授权，但此 APK 没有内置 Root Bridge。请安装 release APK。";
        }
        RootAccess.CommandResult install = RootAccess.installModule(payload.getAbsolutePath());
        //noinspection ResultOfMethodCallIgnored
        payload.delete();
        if (!install.completed || install.exitCode != 0) {
            return "Root Bridge 安装失败；SELinux 未被关闭。\n" + install.summary();
        }
        return "Root Bridge 已安装。请重启后检查 READY/SAFE_MODE 状态。\n"
                + install.output.trim();
    }

    String diagnostics(Context context) {
        if (!RootAccess.isGranted()) {
            return "root_granted=false";
        }
        String command = "printf 'root_granted=true\\n'; "
                + "if [ -x /data/adb/modules/vcames_root_bridge/device-probe.sh ]; then "
                + "sh /data/adb/modules/vcames_root_bridge/device-probe.sh; "
                + "else printf 'compatibility_id=UNAVAILABLE_MODULE_NOT_INSTALLED\\n'; fi; "
                + "printf 'status='; cat /data/adb/vcames/status.txt 2>/dev/null || printf UNKNOWN; "
                + "if [ -d /data/adb/modules/vcames_root_bridge ]; then "
                + "printf '\\nmodule=installed module_version='; "
                + "sed -n 's/^versionCode=//p' /data/adb/modules/vcames_root_bridge/module.prop | head -n 1; "
                + "printf ' module_bridge_schema='; sed -n 's/^bridge_schema=//p' "
                + "/data/adb/modules/vcames_root_bridge/bridge.properties | head -n 1; "
                + "printf ' module_daemon_protocol='; sed -n 's/^daemon_protocol=//p' "
                + "/data/adb/modules/vcames_root_bridge/bridge.properties | head -n 1; "
                + "printf ' module_frame_bus_version='; sed -n 's/^frame_bus_version=//p' "
                + "/data/adb/modules/vcames_root_bridge/bridge.properties | head -n 1; "
                + "printf ' module_profile_schema='; sed -n 's/^profile_schema=//p' "
                + "/data/adb/modules/vcames_root_bridge/bridge.properties | head -n 1; "
                + "else printf '\\nmodule=missing'; fi";
        return RootAccess.run(command, 20).summary();
    }
}
