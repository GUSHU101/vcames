package io.github.gushu101.vcames;

import android.content.Context;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.os.Build;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;
import java.util.Locale;

/** Read-only device and Camera2 inventory used to select exact-build adapters. */
final class DeviceProfiler {
    private DeviceProfiler() {}

    static String collect(Context context) {
        JSONObject root = new JSONObject();
        try {
            root.put("schema", 1);
            root.put("manufacturer", Build.MANUFACTURER);
            root.put("brand", Build.BRAND);
            root.put("model", Build.MODEL);
            root.put("product", Build.PRODUCT);
            root.put("device", Build.DEVICE);
            root.put("hardware", Build.HARDWARE);
            root.put("api", Build.VERSION.SDK_INT);
            root.put("release", Build.VERSION.RELEASE);
            root.put("security_patch", Build.VERSION.SECURITY_PATCH);
            root.put("build_id", Build.ID);
            root.put("incremental", Build.VERSION.INCREMENTAL);
            root.put("fingerprint_sha256", sha256(Build.FINGERPRINT));
            root.put("abis", new JSONArray(Arrays.asList(Build.SUPPORTED_ABIS)));
            if (Build.VERSION.SDK_INT >= 31) {
                root.put("soc_manufacturer", Build.SOC_MANUFACTURER);
                root.put("soc_model", Build.SOC_MODEL);
            }
            root.put("cameras", cameraInventory(context));
            String identity = String.format(
                    Locale.US,
                    "%s|%s|%s|%s|%d|%s",
                    Build.MANUFACTURER,
                    Build.MODEL,
                    Build.PRODUCT,
                    Build.DEVICE,
                    Build.VERSION.SDK_INT,
                    sha256(Build.FINGERPRINT));
            root.put("profile_id", "vcames-" + sha256(identity).substring(0, 24));
            root.put("compatibility_id", "REQUIRES_ROOT_HASH_INVENTORY");
            root.put("compatibility", CompatibilityEngine.evaluate());
            return root.toString(2);
        } catch (JSONException e) {
            return "{\"error\":\"device profile serialization failed\"}";
        }
    }

    private static JSONArray cameraInventory(Context context) throws JSONException {
        JSONArray cameras = new JSONArray();
        CameraManager manager = context.getSystemService(CameraManager.class);
        if (manager == null) {
            return cameras;
        }
        try {
            for (String id : manager.getCameraIdList()) {
                CameraCharacteristics characteristics = manager.getCameraCharacteristics(id);
                JSONObject camera = new JSONObject();
                camera.put("id", id);
                camera.put("facing", facingName(
                        characteristics.get(CameraCharacteristics.LENS_FACING)));
                camera.put("hardware_level", valueOrUnknown(
                        characteristics.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL)));
                int[] capabilities = characteristics.get(
                        CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES);
                camera.put("capabilities", capabilities == null
                        ? new JSONArray()
                        : new JSONArray(toIntegerArray(capabilities)));
                cameras.put(camera);
            }
        } catch (Exception e) {
            JSONObject failure = new JSONObject();
            failure.put("error", e.getClass().getSimpleName() + ": " + e.getMessage());
            cameras.put(failure);
        }
        return cameras;
    }

    private static String facingName(Integer facing) {
        if (facing == null) {
            return "unknown";
        }
        if (facing == CameraCharacteristics.LENS_FACING_FRONT) {
            return "front";
        }
        if (facing == CameraCharacteristics.LENS_FACING_BACK) {
            return "back";
        }
        if (facing == CameraCharacteristics.LENS_FACING_EXTERNAL) {
            return "external";
        }
        return Integer.toString(facing);
    }

    private static String valueOrUnknown(Integer value) {
        return value == null ? "unknown" : Integer.toString(value);
    }

    private static Integer[] toIntegerArray(int[] source) {
        Integer[] values = new Integer[source.length];
        for (int i = 0; i < source.length; ++i) {
            values[i] = source[i];
        }
        return values;
    }

    private static String sha256(String value) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] bytes = digest.digest(value.getBytes(StandardCharsets.UTF_8));
            StringBuilder hex = new StringBuilder(bytes.length * 2);
            for (byte item : bytes) {
                hex.append(String.format(Locale.US, "%02x", item & 0xff));
            }
            return hex.toString();
        } catch (NoSuchAlgorithmException impossible) {
            throw new AssertionError(impossible);
        }
    }
}
