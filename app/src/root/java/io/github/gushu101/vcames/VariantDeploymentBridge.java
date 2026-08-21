package io.github.gushu101.vcames;

import android.content.Context;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

final class VariantDeploymentBridge implements DeploymentBridge {
    private static final String MODULE_ASSET = "vcames-root-bridge.zip";

    @Override
    public String actionLabel() {
        return "授权 ROOT 并部署";
    }

    @Override
    public String authorizeAndDeploy(Context context) {
        RootManager.Probe probe = RootManager.probe();
        if (!probe.granted) {
            return "未获得 ROOT。请在 KernelSU 或 Magisk 中允许 VCamES。\n" + probe.output;
        }
        final RootBridgeManifest manifest;
        try {
            manifest = RootBridgeManifest.fromAsset(context);
        } catch (IOException invalidPayload) {
            return "ROOT 已授权（" + probe.providerName() + "），但 APK 内置 Root Bridge 无效："
                    + invalidPayload.getMessage();
        }
        String diagnostics = RootManager.diagnostics(diagnosticCommand());
        if (diagnostics.contains("module=installed")
                && manifest.matchesInstalled(diagnostics)) {
            return "ROOT 已授权（" + probe.providerName() + "），Root Bridge "
                    + manifest.versionName + " 及协议契约已匹配。\n"
                    + probe.output + "\n" + diagnostics;
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
                    throw new IOException("内置 Root Bridge 超过 128 MiB 安全限制");
                }
                output.write(buffer, 0, count);
            }
        } catch (IOException noPayload) {
            return "ROOT 已授权（" + probe.providerName() + "），但 APK 未内置 Root Bridge。"
                    + "请用 tools/root/build-root-module 构建 standalone APK。\n" + diagnostics;
        }

        RootManager.CommandResult install = RootManager.installModule(
                probe.provider,
                payload.getAbsolutePath());
        //noinspection ResultOfMethodCallIgnored
        payload.delete();
        if (!install.completed || install.exitCode != 0) {
            return "Root Bridge 安装失败；SELinux 未被关闭。\n" + install.summary();
        }
        String kernelSuNotice = probe.provider == RootManager.Provider.KERNEL_SU
                ? "\nKernelSU 的 system/vendor 覆盖还需要设备上已配置兼容 metamodule；"
                        + "脚本、sepolicy 与守护进程不依赖该覆盖。"
                : "";
        return "Root Bridge 已通过 " + probe.providerName()
                + " 安装。请重启后检查 READY/SAFE_MODE 状态。"
                + kernelSuNotice + "\n" + install.output.trim();
    }

    @Override
    public String diagnostics(Context context) {
        RootManager.Probe probe = RootManager.probe();
        return "root_manager=" + probe.providerName() + "\n"
                + probe.output + "\n"
                + RootManager.diagnostics(diagnosticCommand());
    }

    private static String diagnosticCommand() {
        return "printf 'device='; getprop ro.product.device; "
                + "printf ' brand='; getprop ro.product.brand; "
                + "printf ' product='; getprop ro.product.name; "
                + "printf ' model='; getprop ro.product.model; "
                + "printf ' manufacturer='; getprop ro.product.manufacturer; "
                + "printf ' soc_manufacturer='; getprop ro.soc.manufacturer; "
                + "printf ' soc='; getprop ro.soc.model; "
                + "printf ' board_platform='; getprop ro.board.platform; "
                + "printf ' api='; getprop ro.build.version.sdk; "
                + "printf ' build_id='; getprop ro.build.id; "
                + "printf ' security_patch='; getprop ro.build.version.security_patch; "
                + "printf ' region='; r=$(getprop ro.miui.region); "
                + "[ -n \"$r\" ] || r=$(getprop ro.product.mod_device); printf '%s' \"$r\"; "
                + "printf ' kernel='; uname -r; "
                + "printf ' selinux='; getenforce; "
                + "printf '\\nsystem_fingerprint_sha256='; "
                + "printf '%s' \"$(getprop ro.build.fingerprint)\" | sha256sum | cut -d' ' -f1; "
                + "printf ' vendor_fingerprint_sha256='; "
                + "printf '%s' \"$(getprop ro.vendor.build.fingerprint)\" | sha256sum | cut -d' ' -f1; "
                + "printf '\\ncameraserver_sha256='; "
                + "sha256sum /system/bin/cameraserver 2>/dev/null | cut -d' ' -f1; "
                + "printf '\\ncamera_hal_services='; "
                + "(lshal 2>/dev/null; service list 2>/dev/null) | "
                + "grep -i 'camera.provider' | head -n 12 | tr '\\n' ';'; "
                + "printf '\\nstatus='; cat /data/adb/vcames/status.txt 2>/dev/null || printf UNKNOWN; "
                + "if [ -d /data/adb/modules/vcames_root_bridge ]; then "
                + "printf '\\nmodule=installed module_version='; "
                + "sed -n 's/^versionCode=//p' "
                + "/data/adb/modules/vcames_root_bridge/module.prop | head -n 1; "
                + "printf ' module_bridge_schema='; sed -n 's/^bridge_schema=//p' "
                + "/data/adb/modules/vcames_root_bridge/bridge.properties | head -n 1; "
                + "printf ' module_daemon_protocol='; sed -n 's/^daemon_protocol=//p' "
                + "/data/adb/modules/vcames_root_bridge/bridge.properties | head -n 1; "
                + "printf ' module_frame_bus_version='; sed -n 's/^frame_bus_version=//p' "
                + "/data/adb/modules/vcames_root_bridge/bridge.properties | head -n 1; "
                + "printf ' module_profile_schema='; sed -n 's/^profile_schema=//p' "
                + "/data/adb/modules/vcames_root_bridge/bridge.properties | head -n 1; "
                + "else printf '\\nmodule=missing'; fi";
    }
}
