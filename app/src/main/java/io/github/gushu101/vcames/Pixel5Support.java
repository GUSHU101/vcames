package io.github.gushu101.vcames;

import android.os.Build;

import java.util.Arrays;
import java.util.Locale;

/** The deliberately narrow product boundary: stock-identity Pixel 5 (redfin), API 30-34. */
final class Pixel5Support {
    static final int MIN_API = 30;
    static final int MAX_API = 34;

    private Pixel5Support() {}

    static Result inspect() {
        String manufacturer = lower(Build.MANUFACTURER);
        String brand = lower(Build.BRAND);
        String device = lower(Build.DEVICE);
        String product = lower(Build.PRODUCT);
        boolean pixel5 = "google".equals(manufacturer)
                && "google".equals(brand)
                && "redfin".equals(device)
                && product.startsWith("redfin");
        boolean arm64 = Arrays.asList(Build.SUPPORTED_ABIS).contains("arm64-v8a");
        // minSdk enforces MIN_API before this code can run.
        boolean api = Build.VERSION.SDK_INT <= MAX_API;
        String backend = "PIXEL5_LEGACY_PROVIDER_HAL_TAKEOVER";
        return new Result(pixel5, arm64, api, backend);
    }

    private static String lower(String value) {
        return value == null ? "" : value.toLowerCase(Locale.US);
    }

    static final class Result {
        final boolean pixel5;
        final boolean arm64;
        final boolean api;
        final String backend;

        Result(boolean pixel5, boolean arm64, boolean api, String backend) {
            this.pixel5 = pixel5;
            this.arm64 = arm64;
            this.api = api;
            this.backend = backend;
        }

        boolean platformAccepted() {
            return pixel5 && arm64 && api;
        }

        String rejection() {
            if (!pixel5) {
                return "仅支持 Google Pixel 5（redfin）原厂系统身份";
            }
            if (!arm64) {
                return "Pixel 5 运行时必须提供 arm64-v8a ABI";
            }
            if (!api) {
                return "仅支持 Pixel 5 Android 11–14（API 30–34）";
            }
            return "";
        }
    }
}
