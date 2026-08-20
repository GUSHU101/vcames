package io.github.gushu101.vcames;

import android.content.Context;

final class VariantDeploymentBridge implements DeploymentBridge {
    @Override
    public String actionLabel() {
        return "检查系统集成";
    }

    @Override
    public String authorizeAndDeploy(Context context) {
        return "系统版不调用 su。请确认 APK 使用平台证书签名，并已把 vcamesd、"
                + "Camera Provider/替换适配器和 SELinux 策略集成进同一 ROM 构建。";
    }

    @Override
    public String diagnostics(Context context) {
        return "deployment=system\nroot_manager=not_used\n"
                + "integration=platform_signature_and_rom_payload_required";
    }
}
