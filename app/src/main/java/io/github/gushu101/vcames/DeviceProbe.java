package io.github.gushu101.vcames;

import android.content.Context;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.os.Build;
import android.util.Range;
import android.util.Size;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;
import java.util.Locale;

/** Read-only facts. It never predicts support; exact support comes from a signed Profile match. */
final class DeviceProbe {
    private DeviceProbe() {}

    static JSONObject collect(Context context) {
        JSONObject root = new JSONObject();
        try {
            root.put("schema", 3);
            root.put("scope", "google-xiaomi-api30-33-exact-profile-v1");
            root.put("manufacturer", Build.MANUFACTURER);
            root.put("brand", Build.BRAND);
            root.put("model", Build.MODEL);
            root.put("product", Build.PRODUCT);
            root.put("device", Build.DEVICE);
            root.put("board", Build.BOARD);
            root.put("hardware", Build.HARDWARE);
            root.put("api", Build.VERSION.SDK_INT);
            root.put("release", Build.VERSION.RELEASE);
            root.put("security_patch", Build.VERSION.SECURITY_PATCH);
            root.put("build_id", Build.ID);
            root.put("incremental", Build.VERSION.INCREMENTAL);
            root.put("system_fingerprint_sha256", sha256(Build.FINGERPRINT));
            root.put("abis", new JSONArray(Arrays.asList(Build.SUPPORTED_ABIS)));
            if (Build.VERSION.SDK_INT >= 31) {
                root.put("soc_manufacturer", Build.SOC_MANUFACTURER);
                root.put("soc_model", Build.SOC_MODEL);
            }
            root.put("cameras", cameraInventory(context));
            root.put("compatibility_id", "REQUIRES_ROOT_DEVICE_PROBE");
        } catch (JSONException failure) {
            try {
                root.put("error", "device probe serialization failed");
            } catch (JSONException impossible) {
                throw new AssertionError(impossible);
            }
        }
        return root;
    }

    private static JSONArray cameraInventory(Context context) throws JSONException {
        JSONArray cameras = new JSONArray();
        CameraManager manager = context.getSystemService(CameraManager.class);
        if (manager == null) {
            return cameras;
        }
        try {
            for (String id : manager.getCameraIdList()) {
                CameraCharacteristics values = manager.getCameraCharacteristics(id);
                JSONObject camera = new JSONObject();
                camera.put("id", id);
                camera.put("facing", facingName(values.get(CameraCharacteristics.LENS_FACING)));
                camera.put("sensor_orientation", valueOrUnknown(
                        values.get(CameraCharacteristics.SENSOR_ORIENTATION)));
                camera.put("hardware_level", valueOrUnknown(
                        values.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL)));
                int[] capabilities = values.get(
                        CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES);
                camera.put("capabilities", capabilities == null
                        ? new JSONArray() : new JSONArray(toIntegerArray(capabilities)));
                Range<Integer>[] fpsRanges = values.get(
                        CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);
                JSONArray fps = new JSONArray();
                if (fpsRanges != null) {
                    for (Range<Integer> range : fpsRanges) {
                        fps.put(range.getLower() + "-" + range.getUpper());
                    }
                }
                camera.put("fps_ranges", fps);
                StreamConfigurationMap map = values.get(
                        CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
                JSONArray sizesJson = new JSONArray();
                if (map != null) {
                    Size[] sizes = map.getOutputSizes(android.graphics.ImageFormat.YUV_420_888);
                    if (sizes != null) {
                        for (Size size : sizes) {
                            sizesJson.put(size.getWidth() + "x" + size.getHeight());
                        }
                    }
                }
                camera.put("yuv420_output_sizes", sizesJson);
                cameras.put(camera);
            }
        } catch (Exception failure) {
            JSONObject error = new JSONObject();
            error.put("error", failure.getClass().getSimpleName() + ": "
                    + failure.getMessage());
            cameras.put(error);
        }
        return cameras;
    }

    private static String facingName(Integer facing) {
        if (facing == null) return "unknown";
        if (facing == CameraCharacteristics.LENS_FACING_FRONT) return "front";
        if (facing == CameraCharacteristics.LENS_FACING_BACK) return "back";
        if (facing == CameraCharacteristics.LENS_FACING_EXTERNAL) return "external";
        return Integer.toString(facing);
    }

    private static String valueOrUnknown(Integer value) {
        return value == null ? "unknown" : Integer.toString(value);
    }

    private static Integer[] toIntegerArray(int[] source) {
        Integer[] values = new Integer[source.length];
        for (int i = 0; i < source.length; ++i) values[i] = source[i];
        return values;
    }

    private static String sha256(String value) {
        try {
            byte[] bytes = MessageDigest.getInstance("SHA-256").digest(
                    value.getBytes(StandardCharsets.UTF_8));
            StringBuilder hex = new StringBuilder(bytes.length * 2);
            for (byte item : bytes) hex.append(String.format(Locale.US, "%02x", item & 0xff));
            return hex.toString();
        } catch (NoSuchAlgorithmException impossible) {
            throw new AssertionError(impossible);
        }
    }
}
