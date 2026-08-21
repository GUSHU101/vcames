package io.github.gushu101.vcames;

import android.os.Build;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.Arrays;
import java.util.Locale;

/** Candidate classification only; exact-build hashes and device self-tests remain mandatory. */
final class CompatibilityEngine {
    static final int MIN_API = 30;
    static final int MAX_API = 33;

    private CompatibilityEngine() {}

    static JSONObject evaluate() throws JSONException {
        JSONObject result = new JSONObject();
        JSONArray blockers = new JSONArray();
        JSONArray warnings = new JSONArray();
        int api = Build.VERSION.SDK_INT;
        String vendorFamily = vendorFamily(Build.MANUFACTURER, Build.BRAND);
        String socFamily = socFamily();
        boolean apiSupported = api >= MIN_API && api <= MAX_API;
        boolean arm64 = Arrays.asList(Build.SUPPORTED_ABIS).contains("arm64-v8a");
        boolean vendorSupported = !"unsupported".equals(vendorFamily);

        if (!apiSupported) {
            blockers.put("仅支持 API 30–33（Android 11–13）");
        }
        if (!arm64) {
            blockers.put("第一阶段系统组件只构建 arm64-v8a");
        }
        if (!vendorSupported) {
            blockers.put("仅接受 Google、Xiaomi/Redmi/POCO、Samsung 设备");
        }
        if ("unknown".equals(socFamily)) {
            warnings.put("应用沙箱无法确认 SoC family；打包 replacement 前必须导出 ROOT 完整画像");
        }

        String strategy = strategyFamily(vendorFamily, socFamily);
        boolean candidate = apiSupported && arm64 && vendorSupported;
        result.put("vendor_family", vendorFamily);
        result.put("soc_family", socFamily);
        result.put("strategy_family", strategy);
        result.put("camera_interface", "REQUIRES_ROOT_VINTF_SERVICE_PROBE");
        result.put("external_candidate", candidate);
        result.put("replacement_candidate", candidate);
        result.put("replacement_state", candidate
                ? "EXACT_BUILD_ADAPTER_REQUIRED"
                : "UNSUPPORTED_OR_PROFILE_INCOMPLETE");
        result.put("verification", "UNVERIFIED_UNTIL_CONTENT_AND_STRESS_TEST");
        result.put("blockers", blockers);
        result.put("warnings", warnings);
        return result;
    }

    static String vendorFamily(String manufacturer, String brand) {
        String joined = ((manufacturer == null ? "" : manufacturer) + "|"
                + (brand == null ? "" : brand)).toLowerCase(Locale.US);
        if (joined.contains("google")) {
            return "google";
        }
        if (joined.contains("xiaomi") || joined.contains("redmi")
                || joined.contains("poco")) {
            return "xiaomi";
        }
        if (joined.contains("samsung")) {
            return "samsung";
        }
        return "unsupported";
    }

    static String socFamily() {
        String socManufacturer = "";
        String socModel = "";
        if (Build.VERSION.SDK_INT >= 31) {
            socManufacturer = Build.SOC_MANUFACTURER;
            socModel = Build.SOC_MODEL;
        }
        String identity = (socManufacturer + "|" + socModel + "|" + Build.HARDWARE
                + "|" + Build.BOARD).toLowerCase(Locale.US);
        if (identity.contains("tensor") || identity.contains("gs101")
                || identity.contains("gs201")) {
            return "tensor";
        }
        if (identity.contains("qualcomm") || identity.contains("snapdragon")
                || identity.contains("qcom") || identity.contains("msm")
                || identity.matches(".*\\bsm[0-9]{3,4}.*")) {
            return "qualcomm";
        }
        if (identity.contains("exynos")) {
            return "exynos";
        }
        if (identity.contains("mediatek") || identity.contains("mtk")
                || identity.matches(".*\\bmt[0-9]{4}.*")) {
            return "mediatek";
        }
        return "unknown";
    }

    private static String strategyFamily(String vendor, String soc) {
        if ("qualcomm".equals(soc)) {
            return vendor + "-qualcomm-provider-probe";
        }
        if ("google".equals(vendor) && "tensor".equals(soc)) {
            return "google-tensor-provider-probe";
        }
        if ("samsung".equals(vendor) && "exynos".equals(soc)) {
            return "samsung-exynos-provider-probe";
        }
        return vendor + "-" + soc + "-provider-probe";
    }
}
