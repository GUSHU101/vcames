package io.github.gushu101.vcames;

import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;

final class VariantDeploymentBridge implements DeploymentBridge {
    private static final String MODULE_ASSET = "vcames-root-bridge.zip";
    private static final int MAX_COMMAND_OUTPUT = 64 * 1024;
    private static final int BRIDGE_VERSION_CODE = 10200;

    @Override
    public String actionLabel() {
        return "授权 ROOT 并部署";
    }

    @Override
    public String authorizeAndDeploy(Context context) {
        CommandResult root = runSu(diagnosticCommand(), 15);
        if (!root.completed || root.exitCode != 0 || !root.output.contains("uid=0")) {
            return "未获得 ROOT。请确认已安装 Magisk，并在授权弹窗中允许 VCamES。\n"
                    + root.summary();
        }
        if (root.output.contains("module=installed")
                && parseIntField(root.output, "module_version=") >= BRIDGE_VERSION_CODE) {
            return "ROOT 已授权，Root Bridge 已安装。\n" + root.output.trim();
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
            return "ROOT 已授权，但此 APK 没有内置 Root Bridge。请使用 "
                    + "tools/root/build-root-module 构建 standalone APK，或在 Magisk 中安装配套 ZIP。\n"
                    + root.output.trim();
        }

        CommandResult install = runSu(
                "magisk --install-module " + shellQuote(payload.getAbsolutePath()), 45);
        //noinspection ResultOfMethodCallIgnored
        payload.delete();
        if (!install.completed || install.exitCode != 0) {
            return "Root Bridge 安装失败；没有修改 SELinux 状态。\n" + install.summary();
        }
        return "Root Bridge 已安装。请重启手机，再打开本应用检查 READY 状态。\n"
                + install.output.trim();
    }

    private static String diagnosticCommand() {
        return "printf 'uid='; id -u; "
                + "printf '\\ndevice='; getprop ro.product.device; "
                + "printf ' api='; getprop ro.build.version.sdk; "
                + "printf ' selinux='; getenforce; "
                + "if [ -d /data/adb/modules/vcames_root_bridge ]; then "
                + "printf '\\nmodule=installed module_version='; "
                + "sed -n 's/^versionCode=//p' "
                + "/data/adb/modules/vcames_root_bridge/module.prop | head -n 1; "
                + "printf ' status='; "
                + "cat /data/adb/vcames/status.txt 2>/dev/null || printf UNKNOWN; "
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

    private static String shellQuote(String value) {
        return "'" + value.replace("'", "'\\''") + "'";
    }

    private static CommandResult runSu(String command, int timeoutSeconds) {
        Process process;
        try {
            process = new ProcessBuilder("su", "-c", command)
                    .redirectErrorStream(true)
                    .start();
        } catch (IOException e) {
            return new CommandResult(false, -1, e.getMessage());
        }

        ByteArrayOutputStream output = new ByteArrayOutputStream();
        Thread collector = new Thread(() -> collect(process.getInputStream(), output),
                "vcames-root-output");
        collector.setDaemon(true);
        collector.start();
        boolean completed;
        try {
            completed = process.waitFor(timeoutSeconds, TimeUnit.SECONDS);
            if (!completed) {
                process.destroyForcibly();
            }
            collector.join(2000);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            process.destroyForcibly();
            return new CommandResult(false, -1, "ROOT 操作被中断");
        }
        return new CommandResult(
                completed,
                completed ? process.exitValue() : -1,
                output.toString(StandardCharsets.UTF_8));
    }

    private static void collect(InputStream input, ByteArrayOutputStream output) {
        byte[] buffer = new byte[2048];
        int count;
        try (input) {
            while ((count = input.read(buffer)) >= 0 && output.size() < MAX_COMMAND_OUTPUT) {
                output.write(buffer, 0,
                        Math.min(count, MAX_COMMAND_OUTPUT - output.size()));
            }
        } catch (IOException ignored) {
            // The process result still contains the output collected before EOF.
        }
    }

    private static final class CommandResult {
        final boolean completed;
        final int exitCode;
        final String output;

        CommandResult(boolean completed, int exitCode, String output) {
            this.completed = completed;
            this.exitCode = exitCode;
            this.output = output == null ? "" : output;
        }

        String summary() {
            String state = completed ? "exit=" + exitCode : "操作超时";
            return output.isBlank() ? state : state + "\n" + output.trim();
        }
    }
}
