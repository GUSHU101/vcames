package io.github.gushu101.vcames;

import android.content.Context;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

final class VariantDeploymentBridge implements DeploymentBridge {
    private static final String MODULE_ASSET = "vcames-root-bridge.zip";
    private static final int BRIDGE_VERSION_CODE = 20000;

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
        String diagnostics = RootManager.diagnostics(diagnosticCommand());
        if (diagnostics.contains("module=installed")
                && parseIntField(diagnostics, "module_version=") >= BRIDGE_VERSION_CODE) {
            return "ROOT 已授权（" + probe.providerName() + "），Root Bridge 已安装。\n"
                    + probe.output + "\n" + diagnostics;
        }

        File payload = new File(context.getCacheDir(), MODULE_ASSET);
        try (InputStream input = context.getAssets().open(MODULE_ASSET);
             FileOutputStream output = new FileOutputStream(payload, false)) {
            byte[] buffer = new byte[16 * 1024];
            int count;
            long total = 0;
            while ((count = input.read(buffer)) >= 0) {
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
                + "printf ' product='; getprop ro.product.name; "
                + "printf ' manufacturer='; getprop ro.product.manufacturer; "
                + "printf ' soc='; getprop ro.soc.model; "
                + "printf ' api='; getprop ro.build.version.sdk; "
                + "printf ' kernel='; uname -r; "
                + "printf ' selinux='; getenforce; "
                + "printf '\\nsystem_fingerprint_sha256='; "
                + "printf '%s' \"$(getprop ro.build.fingerprint)\" | sha256sum | cut -d' ' -f1; "
                + "printf ' vendor_fingerprint_sha256='; "
                + "printf '%s' \"$(getprop ro.vendor.build.fingerprint)\" | sha256sum | cut -d' ' -f1; "
                + "printf '\\ncameraserver_sha256='; "
                + "sha256sum /system/bin/cameraserver 2>/dev/null | cut -d' ' -f1; "
                + "printf ' status='; cat /data/adb/vcames/status.txt 2>/dev/null || printf UNKNOWN; "
                + "if [ -d /data/adb/modules/vcames_root_bridge ]; then "
                + "printf '\\nmodule=installed module_version='; "
                + "sed -n 's/^versionCode=//p' "
                + "/data/adb/modules/vcames_root_bridge/module.prop | head -n 1; "
                + "else printf '\\nmodule=missing'; fi";
    }

    private static int parseIntField(String output, String prefix) {
        int start = output.indexOf(prefix);
        if (start < 0) {
            return -1;
        }
        start += prefix.length();
        int end = start;
        while (end < output.length() && Character.isDigit(output.charAt(end))) {
            end++;
        }
        try {
            return Integer.parseInt(output.substring(start, end));
        } catch (NumberFormatException e) {
            return -1;
        }
    }
}
