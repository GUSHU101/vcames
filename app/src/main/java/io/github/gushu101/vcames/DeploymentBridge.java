package io.github.gushu101.vcames;

import android.content.Context;

interface DeploymentBridge {
    String actionLabel();

    String authorizeAndDeploy(Context context);

    String diagnostics(Context context);

    static DeploymentBridge create() {
        return new VariantDeploymentBridge();
    }
}
