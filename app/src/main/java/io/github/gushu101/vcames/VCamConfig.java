package io.github.gushu101.vcames;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.Locale;

final class VCamConfig {
    static final String PREFS = "vcames";

    final String url;
    final String device;
    final int width;
    final int height;
    final int fps;
    final int rotation;
    final boolean mirror;
    final boolean holdLast;
    final int staleTimeoutMs;
    final int jpegQuality;
    final boolean startOnBoot;

    VCamConfig(
            String url,
            String device,
            int width,
            int height,
            int fps,
            int rotation,
            boolean mirror,
            boolean holdLast,
            int staleTimeoutMs,
            int jpegQuality,
            boolean startOnBoot) {
        this.url = url;
        this.device = device;
        this.width = width;
        this.height = height;
        this.fps = fps;
        this.rotation = rotation;
        this.mirror = mirror;
        this.holdLast = holdLast;
        this.staleTimeoutMs = staleTimeoutMs;
        this.jpegQuality = jpegQuality;
        this.startOnBoot = startOnBoot;
    }

    static VCamConfig load(Context context) {
        SharedPreferences p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        return new VCamConfig(
                p.getString("url", "http://192.168.1.10:8888/live.mjpg"),
                p.getString("device", "/dev/video100"),
                p.getInt("width", 1280),
                p.getInt("height", 720),
                p.getInt("fps", 30),
                normalizeRotation(p.getInt("rotation", 0)),
                p.getBoolean("mirror", false),
                p.getBoolean("hold_last", true),
                p.getInt("stale_timeout_ms", 3000),
                p.getInt("jpeg_quality", 90),
                p.getBoolean("start_on_boot", false));
    }

    static String loadLocalUri(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getString("local_uri", "");
    }

    static void saveLocalUri(Context context, String uri) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit()
                .putString("local_uri", uri)
                .apply();
    }

    void save(Context context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit()
                .putString("url", url)
                .putString("device", device)
                .putInt("width", width)
                .putInt("height", height)
                .putInt("fps", fps)
                .putInt("rotation", rotation)
                .putBoolean("mirror", mirror)
                .putBoolean("hold_last", holdLast)
                .putInt("stale_timeout_ms", staleTimeoutMs)
                .putInt("jpeg_quality", jpegQuality)
                .putBoolean("start_on_boot", startOnBoot)
                .apply();
    }

    String toStartCommand() {
        validate();
        return String.format(
                Locale.US,
                "START\nurl=%s\ndevice=%s\nwidth=%d\nheight=%d\nfps=%d\nrotation=%d\n"
                        + "mirror=%d\nhold_last=%d\nstale_timeout_ms=%d\njpeg_quality=%d\n.\n",
                url,
                device,
                width,
                height,
                fps,
                rotation,
                mirror ? 1 : 0,
                holdLast ? 1 : 0,
                staleTimeoutMs,
                jpegQuality);
    }

    private void validate() {
        if (!url.startsWith("http://") && !url.equals("push://local")) {
            throw new IllegalArgumentException("来源必须是 http:// MJPEG 或本地推帧");
        }
        if (url.indexOf('\n') >= 0 || url.indexOf('\r') >= 0) {
            throw new IllegalArgumentException("URL 不能包含换行");
        }
        if (!device.matches("/dev/video[0-9]+")) {
            throw new IllegalArgumentException("设备路径应类似 /dev/video100");
        }
        if (width < 160 || width > 3840 || height < 120 || height > 2160) {
            throw new IllegalArgumentException("分辨率超出 160×120 到 3840×2160");
        }
        if ((width & 1) != 0 || (height & 1) != 0) {
            throw new IllegalArgumentException("宽度和高度必须是偶数");
        }
        if (fps < 1 || fps > 60) {
            throw new IllegalArgumentException("FPS 必须为 1–60");
        }
        if (jpegQuality < 40 || jpegQuality > 100) {
            throw new IllegalArgumentException("JPEG 质量必须为 40–100");
        }
        if (staleTimeoutMs < 250 || staleTimeoutMs > 60000) {
            throw new IllegalArgumentException("断流超时必须为 250–60000 ms");
        }
    }

    private static int normalizeRotation(int rotation) {
        return rotation == 90 || rotation == 180 || rotation == 270 ? rotation : 0;
    }
}
