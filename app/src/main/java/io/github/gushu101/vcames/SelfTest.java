package io.github.gushu101.vcames;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.util.Locale;

/** End-to-end readiness checks included in every exported diagnostic. */
final class SelfTest {
    private SelfTest() {}

    static JSONObject run(String rootDiagnostics, JSONObject profileResolution) {
        JSONObject result = new JSONObject();
        JSONArray checks = new JSONArray();
        boolean ready = true;
        try {
            ready &= add(checks, "root_uid_0", rootDiagnostics.contains("root_granted=true"),
                    "explicit uid-0 grant required");
            ready &= add(checks, "selinux_enforcing",
                    rootDiagnostics.toLowerCase(Locale.US).contains("selinux=enforcing"),
                    "SELinux must remain Enforcing");
            boolean exactProfile = profileResolution.optString("state")
                    .startsWith("EXACT_VERIFIED_");
            ready &= add(checks, "exact_signed_profile", exactProfile,
                    profileResolution.optString("state", "NO_PROFILE_STATE"));
            try {
                JSONObject daemon = new JSONObject(DaemonClient.status());
                boolean adapter = daemon.optBoolean("replacement_attached", false);
                ready &= add(checks, "daemon_and_adapter", daemon.optBoolean("running", false)
                                && adapter,
                        adapter ? "running and attached" : "adapter not attached");
            } catch (IOException | JSONException unavailable) {
                ready = false;
                add(checks, "daemon_and_adapter", false, unavailable.getMessage());
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
