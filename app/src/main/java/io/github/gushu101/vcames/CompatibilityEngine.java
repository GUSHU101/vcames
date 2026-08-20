package io.github.gushu101.vcames;

import android.os.Build;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/** Conservative capability classification; it never treats a model name as verification. */
final class CompatibilityEngine {
    private static final Set<String> PIXEL_4_TO_6 = new HashSet<>(Arrays.asList(
            "flame", "coral", "sunfish", "bramble", "redfin", "barbet",
            "oriole", "raven", "bluejay"));

    private CompatibilityEngine() {}

    static JSONObject evaluate() throws JSONException {
        JSONObject result = new JSONObject();
        JSONArray blockers = new JSONArray();
        boolean apiSupported = Build.VERSION.SDK_INT <= 35;
        boolean arm64 = Arrays.asList(Build.SUPPORTED_ABIS).contains("arm64-v8a");
        boolean targetPixel = "google".equals(Build.MANUFACTURER.toLowerCase(Locale.US))
                && PIXEL_4_TO_6.contains(Build.DEVICE);
        if (!apiSupported) {
            blockers.put("API 必须位于 30–35（Android 11–15）");
        }
        if (!arm64) {
            blockers.put("当前工程只构建 arm64-v8a 系统组件");
        }
        if (!targetPixel) {
            blockers.put("当前验收矩阵仅覆盖 Pixel 4–6；其他厂商需要独立设备适配包");
        }
        result.put("external_candidate", apiSupported && arm64 && targetPixel);
        result.put("replacement_state", "ADAPTER_REQUIRED");
        result.put("verification", "UNVERIFIED_UNTIL_DEVICE_SELF_TEST");
        result.put("blockers", blockers);
        return result;
    }
}
