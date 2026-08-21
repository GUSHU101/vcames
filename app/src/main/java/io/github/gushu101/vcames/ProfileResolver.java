package io.github.gushu101.vcames;

import android.content.Context;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;
import java.util.Properties;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Exact compatibility-id lookup only; an absent or unsigned match always fails closed. */
final class ProfileResolver {
    private static final Pattern ID = Pattern.compile(
            "(?:^|\\s)compatibility_id=([0-9a-f]{64})(?:\\s|$)");

    private ProfileResolver() {}

    static JSONObject resolve(Context context, String rootDiagnostics) {
        JSONObject result = new JSONObject();
        try {
            Matcher matcher = ID.matcher(rootDiagnostics);
            if (!matcher.find()) {
                return result.put("state", "NO_MATCH_MISSING_COMPATIBILITY_ID");
            }
            String actual = matcher.group(1);
            result.put("compatibility_id", actual);
            JSONObject packaged = resolvePackagedProfile(context, actual);
            if (packaged != null) {
                return packaged;
            }
            JSONObject catalog = new JSONObject(readAsset(context, "profile-catalog.json"));
            if (!"SIGNED_VERIFIED_PROFILES".equals(catalog.optString("catalog_status"))
                    || !"SIGNED".equals(catalog.getJSONObject("signature")
                            .optString("status"))) {
                return result.put("state", "NO_MATCH_UNSIGNED_OR_EMPTY_CATALOG");
            }
            JSONArray entries = catalog.getJSONArray("entries");
            for (int i = 0; i < entries.length(); ++i) {
                JSONObject entry = entries.getJSONObject(i);
                if (actual.equals(entry.optString("compatibility_id"))
                        && "VERIFIED".equals(entry.optString("status"))) {
                    return result.put("state", "EXACT_VERIFIED_PROFILE_MATCH")
                            .put("profile", entry.optString("profile"));
                }
            }
            return result.put("state", "NO_MATCH_EXACT_PROFILE_REQUIRED");
        } catch (IOException | JSONException failure) {
            try {
                return result.put("state", "NO_MATCH_CATALOG_INVALID")
                        .put("error", failure.getMessage());
            } catch (JSONException impossible) {
                throw new AssertionError(impossible);
            }
        }
    }

    private static JSONObject resolvePackagedProfile(Context context, String actual)
            throws IOException, JSONException {
        Properties runtime = null;
        byte[] profile = null;
        boolean hasSignature = false;
        try (InputStream raw = context.getAssets().open("vcames-root-bridge.zip");
             ZipInputStream zip = new ZipInputStream(raw)) {
            ZipEntry entry;
            int entries = 0;
            while ((entry = zip.getNextEntry()) != null) {
                if (++entries > 4096) throw new IOException("Root Bridge has too many entries");
                String name = entry.getName().replace('\\', '/');
                if ("profile.runtime.properties".equals(name)) {
                    runtime = new Properties();
                    runtime.load(zip);
                } else if ("profile.json".equals(name)) {
                    profile = readBounded(zip, 1024 * 1024);
                } else if ("profile.sig".equals(name)) {
                    hasSignature = readBounded(zip, 4096).length > 0;
                }
            }
        }
        if (runtime == null || profile == null || !hasSignature
                || !"VERIFIED".equals(runtime.getProperty("validation_status"))
                || !actual.equals(runtime.getProperty("compatibility_id"))
                || !sha256(profile).equals(runtime.getProperty("profile_sha256"))) {
            return null;
        }
        return new JSONObject()
                .put("state", "EXACT_VERIFIED_PACKAGED_PROFILE_MATCH")
                .put("compatibility_id", actual);
    }

    private static byte[] readBounded(InputStream input, int limit) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096];
        int count;
        while ((count = input.read(buffer)) != -1) {
            if (output.size() + count > limit) throw new IOException("Profile entry too large");
            output.write(buffer, 0, count);
        }
        return output.toByteArray();
    }

    private static String sha256(byte[] value) {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(value);
            StringBuilder hex = new StringBuilder(digest.length * 2);
            for (byte item : digest) hex.append(String.format(Locale.US, "%02x", item & 0xff));
            return hex.toString();
        } catch (NoSuchAlgorithmException impossible) {
            throw new AssertionError(impossible);
        }
    }

    private static String readAsset(Context context, String name) throws IOException {
        try (InputStream input = context.getAssets().open(name);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int count;
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            return new String(output.toByteArray(), StandardCharsets.UTF_8);
        }
    }
}
