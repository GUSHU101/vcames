package io.github.gushu101.vcames;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.util.Locale;

/** End-to-end readiness checks included in every exported diagnostic. */
final class SelfTest {
    private SelfTest() {}

    static JSONObject run(String rootDiagnostics) {
        JSONObject result = new JSONObject();
        JSONArray checks = new JSONArray();
        boolean ready = true;
        try {
            ready &= add(checks, "root_uid_0", rootDiagnostics.contains("root_granted=true"),
                    "explicit uid-0 grant required");
            ready &= add(checks, "pixel5_platform",
                    rootDiagnostics.contains("platform_supported=true"),
                    "Google Pixel 5 redfin, arm64, API 30-34 required");
            ready &= add(checks, "selinux_enforcing",
                    rootDiagnostics.toLowerCase(Locale.US).contains("selinux=enforcing"),
                    "SELinux must remain Enforcing");
            ready &= add(checks, "global_provider_runtime",
                    rootDiagnostics.contains("runtime_status=READY_GLOBAL_FRONT_BACK")
                            && !rootDiagnostics.contains("provider_pid=stopped"),
                    "Pixel 5 legacy/0 Provider must own camera IDs 0 and 1");
            ready &= add(checks, "ffmpeg_runtime",
                    rootDiagnostics.contains("ffmpeg=ready"),
                    "validated Android FFmpeg runtime is required for network streams");
            try {
                JSONObject daemon = new JSONObject(DaemonClient.status());
                boolean output = daemon.optBoolean("camera_ready", false);
                ready &= add(checks, "global_frame_pipeline",
                        daemon.optBoolean("running", false) && output,
                        output ? "global camera 0/1 V4L2 output is active"
                                : "global replacement frame pipeline is not active");
            } catch (IOException | JSONException unavailable) {
                ready = false;
                add(checks, "global_frame_pipeline", false, unavailable.getMessage());
            }
            result.put("status", ready ? "PASS" : "NOT_READY_FAIL_CLOSED");
            result.put("checks", checks);
            result.put("user_media_included", false);
            return result;
        } catch (JSONException impossible) {
            throw new AssertionError(impossible);
        }
    }

    private static boolean add(JSONArray checks, String id, boolean pass, String detail)
            throws JSONException {
        checks.put(new JSONObject()
                .put("id", id)
                .put("status", pass ? "PASS" : "FAIL")
                .put("detail", detail == null ? "" : detail));
        return pass;
    }
}
