package io.github.gushu101.vcames;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;

/** Root capability discovery and module lifecycle operations for the Root flavor. */
final class RootManager {
    private static final int MAX_COMMAND_OUTPUT = 64 * 1024;

    enum Provider {
        KERNEL_SU,
        MAGISK,
        UNKNOWN
    }

    static final class Probe {
        final boolean granted;
        final Provider provider;
        final String output;

        Probe(boolean granted, Provider provider, String output) {
            this.granted = granted;
            this.provider = provider;
            this.output = output;
        }

        String providerName() {
            switch (provider) {
                case KERNEL_SU:
                    return "KernelSU";
                case MAGISK:
                    return "Magisk";
                default:
                    return "未知 su 管理器";
            }
        }
    }

    private RootManager() {}

    static Probe probe() {
        CommandResult result = runSu(
                "printf 'uid='; id -u; "
                        + "if command -v ksud >/dev/null 2>&1 || [ -x /data/adb/ksud ]; then "
                        + "printf '\\nroot_provider=kernelsu'; "
                        + "(ksud -V 2>/dev/null || /data/adb/ksud -V 2>/dev/null || true); "
                        + "printf '\\ncap_module_lifecycle=1'; "
                        + "printf '\\ncap_sepolicy=1'; "
                        + "printf '\\ncap_system_overlay=requires_metamodule'; "
                        + "elif command -v magisk >/dev/null 2>&1; then "
                        + "printf '\\nroot_provider=magisk'; magisk -V 2>/dev/null || true; "
                        + "printf '\\ncap_module_lifecycle=1'; "
                        + "printf '\\ncap_sepolicy=1'; "
                        + "printf '\\ncap_system_overlay=1'; "
                        + "else printf '\\nroot_provider=unknown'; fi",
                15);
        Provider provider = Provider.UNKNOWN;
        if (result.output.contains("root_provider=kernelsu")) {
            provider = Provider.KERNEL_SU;
        } else if (result.output.contains("root_provider=magisk")) {
            provider = Provider.MAGISK;
        }
        return new Probe(
                result.completed && result.exitCode == 0 && result.output.contains("uid=0"),
                provider,
                result.summary());
    }

    static String diagnostics(String extraCommand) {
        return runSu(extraCommand, 20).summary();
    }

    static CommandResult installModule(Provider provider, String modulePath) {
        String quoted = shellQuote(modulePath);
        switch (provider) {
            case KERNEL_SU:
                return runSu(
                        "installer=$(command -v ksud 2>/dev/null || true); "
                                + "[ -n \"$installer\" ] || installer=/data/adb/ksud; "
                                + "[ -x \"$installer\" ] || exit 127; "
                                + "\"$installer\" module install " + quoted,
                        60);
            case MAGISK:
                return runSu("magisk --install-module " + quoted, 60);
            default:
                return new CommandResult(false, -1, "未识别可用的 KernelSU/Magisk 模块安装器");
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
                new String(output.toByteArray(), StandardCharsets.UTF_8));
    }

    private static void collect(InputStream input, ByteArrayOutputStream output) {
        byte[] buffer = new byte[2048];
        int count;
        try (InputStream source = input) {
            while ((count = source.read(buffer)) >= 0 && output.size() < MAX_COMMAND_OUTPUT) {
                output.write(buffer, 0,
                        Math.min(count, MAX_COMMAND_OUTPUT - output.size()));
            }
        } catch (IOException ignored) {
            // Preserve output collected before EOF.
        }
    }

    static final class CommandResult {
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
            return output.trim().isEmpty() ? state : state + "\n" + output.trim();
        }
    }
}
