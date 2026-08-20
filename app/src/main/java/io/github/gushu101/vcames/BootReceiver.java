package io.github.gushu101.vcames;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

import java.io.IOException;

public final class BootReceiver extends BroadcastReceiver {
    private static final String TAG = "VCamES.Boot";

    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent == null ? null : intent.getAction();
        if (!Intent.ACTION_BOOT_COMPLETED.equals(action)) {
            return;
        }
        VCamConfig config = VCamConfig.load(context);
        if (!config.startOnBoot) {
            return;
        }
        PendingResult pendingResult = goAsync();
        new Thread(() -> {
            try {
                DaemonClient.start(config);
                if (config.url.equals("push://local")) {
                    String uri = VCamConfig.loadLocalUri(context);
                    if (!uri.isEmpty()) {
                        LocalMediaService.start(context, uri);
                    }
                }
            } catch (IOException | IllegalArgumentException e) {
                Log.e(TAG, "Unable to start vcamesd after boot", e);
            } finally {
                pendingResult.finish();
            }
        }, "vcames-boot").start();
    }
}
