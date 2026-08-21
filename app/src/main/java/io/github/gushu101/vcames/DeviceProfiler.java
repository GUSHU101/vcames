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

/** Read-only app-side inventory; ROOT diagnostics add VINTF, service and binary hashes. */
final class DeviceProfiler {
    private DeviceProfiler() {}

    static String collect(Context context) {
        JSONObject root = new JSONObject();
        try {
            String vendorFamily = CompatibilityEngine.vendorFamily(
                    Build.MANUFACTURER, Build.BRAND);
            String socFamily = CompatibilityEngine.socFamily();
            root.put("schema", 2);
            root.put("scope", "google-xiaomi-api30-33-profile-v1");
            root.put("manufacturer", Build.MANUFACTURER);
            root.put("brand", Build.BRAND);
            root.put("vendor_family", vendorFamily);
            root.put("model", Build.MODEL);
            root.put("product", Build.PRODUCT);
            root.put("device", Build.DEVICE);
            root.put("board", Build.BOARD);
            root.put("hardware", Build.HARDWARE);
            root.put("soc_family", socFamily);
            root.put("api", Build.VERSION.SDK_INT);
            root.put("release", Build.VERSION.RELEASE);
            root.put("security_patch", Build.VERSION.SECURITY_PATCH);
            root.put("build_id", Build.ID);
            root.put("incremental", Build.VERSION.INCREMENTAL);
            root.put("system_fingerprint_sha256", sha256(Build.FINGERPRINT));
            root.put("vendor_fingerprint_sha256", "REQUIRES_ROOT_GETPROP");
            root.put("camera_hal_transport", "REQUIRES_ROOT_VINTF_SERVICE_PROBE");
            root.put("root_provider", "REQUIRES_ROOT_FLAVOR_PROBE");
            root.put("abis", new JSONArray(Arrays.asList(Build.SUPPORTED_ABIS)));
            if (Build.VERSION.SDK_INT >= 31) {
                root.put("soc_manufacturer", Build.SOC_MANUFACTURER);
                root.put("soc_model", Build.SOC_MODEL);
            }
            root.put("cameras", cameraInventory(context));
            String identity = String.format(
                    Locale.US,
                    "%s|%s|%s|%s|%s|%d|%s",
                    vendorFamily,
                    socFamily,
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
                camera.put("sensor_orientation", valueOrUnknown(
                        characteristics.get(CameraCharacteristics.SENSOR_ORIENTATION)));
                camera.put("hardware_level", valueOrUnknown(
                        characteristics.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL)));
                int[] capabilities = characteristics.get(
                        CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES);
                camera.put("capabilities", capabilities == null
                        ? new JSONArray()
                        : new JSONArray(toIntegerArray(capabilities)));
                Range<Integer>[] fpsRanges = characteristics.get(
                        CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);
                JSONArray fps = new JSONArray();
                if (fpsRanges != null) {
                    for (Range<Integer> range : fpsRanges) {
                        fps.put(range.getLower() + "-" + range.getUpper());
                    }
                }
                camera.put("fps_ranges", fps);
                StreamConfigurationMap map = characteristics.get(
                        CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
                JSONArray yuvSizes = new JSONArray();
                if (map != null) {
                    Size[] sizes = map.getOutputSizes(android.graphics.ImageFormat.YUV_420_888);
                    if (sizes != null) {
                        for (Size size : sizes) {
                            yuvSizes.put(size.getWidth() + "x" + size.getHeight());
                        }
                    }
                }
                camera.put("yuv420_output_sizes", yuvSizes);
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
