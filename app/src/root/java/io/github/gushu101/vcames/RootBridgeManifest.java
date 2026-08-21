package io.github.gushu101.vcames;

import android.content.Context;

import java.io.IOException;
import java.io.InputStream;
import java.util.Properties;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Version and wire-contract metadata embedded inside the installable Root module. */
final class RootBridgeManifest {
    private static final String ASSET = "vcames-root-bridge.zip";
    private static final String ENTRY = "bridge.properties";

    final String versionName;
    final int versionCode;
    final int bridgeSchema;
    final int daemonProtocol;
    final int frameBusVersion;
    final int profileSchema;

    private RootBridgeManifest(Properties values) throws IOException {
        versionName = required(values, "version_name");
        versionCode = positiveInt(values, "version_code");
        bridgeSchema = positiveInt(values, "bridge_schema");
        daemonProtocol = positiveInt(values, "daemon_protocol");
        frameBusVersion = positiveInt(values, "frame_bus_version");
        profileSchema = positiveInt(values, "profile_schema");
    }

    static RootBridgeManifest fromAsset(Context context) throws IOException {
        try (InputStream raw = context.getAssets().open(ASSET);
             ZipInputStream zip = new ZipInputStream(raw)) {
            ZipEntry entry;
            int entries = 0;
            while ((entry = zip.getNextEntry()) != null) {
                if (++entries > 4096) {
                    throw new IOException("Root Bridge ZIP entry count exceeds safety limit");
                }
                String name = entry.getName().replace('\\', '/');
                if (!entry.isDirectory() && (ENTRY.equals(name) || name.endsWith("/" + ENTRY))) {
                    if (entry.getSize() > 64L * 1024L) {
                        throw new IOException("bridge.properties exceeds safety limit");
                    }
                    Properties values = new Properties();
                    values.load(zip);
                    return new RootBridgeManifest(values);
                }
            }
        }
        throw new IOException("Root Bridge is missing bridge.properties");
    }

    boolean matchesInstalled(String diagnostics) {
        return intField(diagnostics, "module_version=") == versionCode
                && intField(diagnostics, "module_bridge_schema=") == bridgeSchema
                && intField(diagnostics, "module_daemon_protocol=") == daemonProtocol
                && intField(diagnostics, "module_frame_bus_version=") == frameBusVersion
                && intField(diagnostics, "module_profile_schema=") == profileSchema;
    }

    private static String required(Properties values, String key) throws IOException {
        String value = values.getProperty(key, "").trim();
        if (value.isEmpty()) {
            throw new IOException("bridge.properties is missing " + key);
        }
        return value;
    }

    private static int positiveInt(Properties values, String key) throws IOException {
        String value = required(values, key);
        try {
            int parsed = Integer.parseInt(value);
            if (parsed <= 0) {
                throw new NumberFormatException("not positive");
            }
            return parsed;
        } catch (NumberFormatException invalid) {
            throw new IOException("bridge.properties has invalid " + key, invalid);
        }
    }

    private static int intField(String output, String prefix) {
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
        } catch (NumberFormatException invalid) {
            return -1;
        }
    }
}
