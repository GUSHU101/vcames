package io.github.gushu101.vcames;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;

/** Minimal uid-0 capability boundary. It deliberately does not identify a Root product. */
final class RootAccess {
    private static final int MAX_COMMAND_OUTPUT = 64 * 1024;

    private RootAccess() {}

    static boolean isGranted() {
        CommandResult result = run("id -u", 15);
        return result.completed && result.exitCode == 0 && "0".equals(result.output.trim());
    }

    static CommandResult installModule(String modulePath) {
        String module = shellQuote(modulePath);
        return run(
                "if command -v magisk >/dev/null 2>&1 && "
                        + "magisk --install-module " + module + "; then exit 0; fi; "
                        + "installer=$(command -v ksud 2>/dev/null || true); "
                        + "[ -n \"$installer\" ] || installer=/data/adb/ksud; "
                        + "if [ -x \"$installer\" ] && \"$installer\" module install "
                        + module + "; then exit 0; fi; exit 127",
                60);
    }

    static CommandResult run(String command, int timeoutSeconds) {
        Process process;
        try {
            process = new ProcessBuilder("su", "-c", command)
                    .redirectErrorStream(true)
                    .start();
        } catch (IOException failure) {
            return new CommandResult(false, -1, failure.getMessage());
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
        } catch (InterruptedException failure) {
            Thread.currentThread().interrupt();
            process.destroyForcibly();
            return new CommandResult(false, -1, "ROOT operation interrupted");
        }
        return new CommandResult(completed, completed ? process.exitValue() : -1,
                new String(output.toByteArray(), StandardCharsets.UTF_8));
    }

    private static String shellQuote(String value) {
        return "'" + value.replace("'", "'\\''") + "'";
    }

    private static void collect(InputStream input, ByteArrayOutputStream output) {
        byte[] buffer = new byte[2048];
        int count;
        try (InputStream source = input) {
            while ((count = source.read(buffer)) >= 0 && output.size() < MAX_COMMAND_OUTPUT) {
                output.write(buffer, 0, Math.min(count, MAX_COMMAND_OUTPUT - output.size()));
            }
        } catch (IOException ignored) {
            // Preserve the bounded output collected before EOF.
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
            String state = completed ? "exit=" + exitCode : "operation timed out";
            return output.trim().isEmpty() ? state : state + "\n" + output.trim();
        }
    }
}
