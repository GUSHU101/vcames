package io.github.gushu101.vcames;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.Locale;

/** User choices plus fixed, fail-safe runtime policy for front/back replacement. */
final class VCamConfig {
    static final String PREFS = "vcames";

    final String url;
    final String target;
    final String outputPreset;
    final int width;
    final int height;
    final int fps;
    final int rotation;
    final boolean mirror;
    final boolean startOnBoot;

    VCamConfig(String url, String target, String outputPreset, int rotation,
            boolean mirror, boolean startOnBoot) {
        this.url = url;
        this.target = normalizeTarget(target);
        this.outputPreset = normalizePreset(outputPreset);
        int[] dimensions = presetDimensions(this.outputPreset);
        width = dimensions[0];
        height = dimensions[1];
        fps = dimensions[2];
        this.rotation = normalizeRotation(rotation);
        this.mirror = mirror;
        this.startOnBoot = startOnBoot;
    }

    static VCamConfig load(Context context) {
        SharedPreferences values = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        return new VCamConfig(
                values.getString("url", "http://192.168.1.10:8888/live.mjpg"),
                values.getString("target", "front"),
                values.getString("output_preset", "auto"),
                values.getInt("rotation", 0),
                values.getBoolean("mirror", false),
                values.getBoolean("start_on_boot", false));
    }

    static String loadLocalUri(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getString("local_uri", "");
    }

    static void saveLocalUri(Context context, String uri) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString("local_uri", uri).apply();
    }

    void save(Context context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString("url", url)
                .putString("target", target)
                .putString("output_preset", outputPreset)
                .putInt("rotation", rotation)
                .putBoolean("mirror", mirror)
                .putBoolean("start_on_boot", startOnBoot)
                .remove("device")
                .remove("width")
                .remove("height")
                .remove("fps")
                .remove("hold_last")
                .remove("stale_timeout_ms")
                .remove("jpeg_quality")
                .apply();
    }

    String toStartCommand() {
        validate();
        return String.format(Locale.US,
                "START\nurl=%s\ntarget=%s\nwidth=%d\nheight=%d\nfps=%d\n"
                        + "rotation=%d\nmirror=%d\n.\n",
                url, target, width, height, fps, rotation, mirror ? 1 : 0);
    }

    private void validate() {
        if (!url.startsWith("http://") && !url.equals("push://local")) {
            throw new IllegalArgumentException("来源必须是 http:// MJPEG 或本地视频");
        }
        if (url.indexOf('\n') >= 0 || url.indexOf('\r') >= 0) {
            throw new IllegalArgumentException("URL 不能包含换行");
        }
        if (!target.equals("front") && !target.equals("back") && !target.equals("both")) {
            throw new IllegalArgumentException("摄像头替换目标无效");
        }
    }

    private static int[] presetDimensions(String preset) {
        if ("1080p".equals(preset)) return new int[]{1920, 1080, 30};
        return new int[]{1280, 720, 30};
    }

    private static int normalizeRotation(int rotation) {
        return rotation == 90 || rotation == 180 || rotation == 270 ? rotation : 0;
    }

    private static String normalizeTarget(String target) {
        if ("back".equals(target) || "both".equals(target)) return target;
        return "front";
    }

    private static String normalizePreset(String preset) {
        return "1080p".equals(preset) || "720p".equals(preset) ? preset : "auto";
    }
}
