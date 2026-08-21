package io.github.gushu101.vcames;

import android.content.Context;
import android.content.SharedPreferences;

import java.net.URI;
import java.net.URISyntaxException;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/** User media choices; camera 0/1 replacement is global and has no per-app setting. */
final class VCamConfig {
    static final String PREFS = "vcames";
    private static final Set<String> NETWORK_SCHEMES = new HashSet<>(Arrays.asList(
            "http", "https", "rtmp", "rtmps", "rtmpe", "rtmpt", "rtmpte", "rtmpts",
            "rtsp", "rtsps", "srt", "rist", "rtp", "srtp", "udp", "tcp", "mmsh",
            "mmst"));

    final String url;
    final int width;
    final int height;
    final int fps;
    final int rotation;
    final boolean mirror;
    final boolean startOnBoot;

    VCamConfig(String url, int rotation,
            boolean mirror, boolean startOnBoot) {
        this.url = url.trim();
        width = 1280;
        height = 720;
        fps = 30;
        this.rotation = normalizeRotation(rotation);
        this.mirror = mirror;
        this.startOnBoot = startOnBoot;
    }

    static VCamConfig load(Context context) {
        SharedPreferences values = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        return new VCamConfig(
                values.getString("url", "http://192.168.1.10:8888/live.mjpg"),
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
                .putInt("rotation", rotation)
                .putBoolean("mirror", mirror)
                .putBoolean("start_on_boot", startOnBoot)
                .remove("device")
                .remove("width")
                .remove("height")
                .remove("fps")
                .remove("output_preset")
                .remove("hold_last")
                .remove("stale_timeout_ms")
                .remove("jpeg_quality")
                .remove("target")
                .remove("package_name")
                .apply();
    }

    String toStartCommand() {
        validate();
        return String.format(Locale.US,
                "START\nurl=%s\nvideo_device=/dev/video100\n"
                        + "width=%d\nheight=%d\nfps=%d\n"
                        + "rotation=%d\nmirror=%d\n.\n",
                url, width, height, fps, rotation, mirror ? 1 : 0);
    }

    private void validate() {
        if (url.indexOf('\n') >= 0 || url.indexOf('\r') >= 0) {
            throw new IllegalArgumentException("URL 不能包含换行");
        }
        if (url.equals("push://local")) {
            return;
        }
        if (url.isEmpty() || url.length() > 4096) {
            throw new IllegalArgumentException("流媒体地址为空或过长");
        }
        try {
            URI parsed = new URI(url);
            String scheme = parsed.getScheme();
            if (scheme == null || !NETWORK_SCHEMES.contains(scheme.toLowerCase(Locale.US))
                    || parsed.getRawAuthority() == null || parsed.getRawAuthority().isEmpty()) {
                throw new IllegalArgumentException("不支持该流媒体协议或地址缺少主机");
            }
        } catch (URISyntaxException malformed) {
            throw new IllegalArgumentException("流媒体地址格式无效");
        }
    }

    private static int normalizeRotation(int rotation) {
        return rotation == 90 || rotation == 180 || rotation == 270 ? rotation : 0;
    }

}
