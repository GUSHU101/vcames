package io.github.gushu101.vcames;

import org.json.JSONException;
import org.json.JSONObject;

import java.util.Locale;

/** Converts daemon details into stable product states without overstating device validation. */
final class ProductStatusPresenter {
    private ProductStatusPresenter() {}

    static String render(String rawStatus) {
        try {
            JSONObject status = new JSONObject(rawStatus);
            boolean running = status.optBoolean("running", false);
            boolean connected = status.optBoolean("connected", false);
            boolean cameraReady = status.optBoolean("camera_ready", false);
            boolean frameBusReady = status.optBoolean("frame_bus_ready", false);
            boolean replacementAttached = status.optBoolean("replacement_attached", false);
            String target = status.optString("target", "external");
            String error = status.optString("error", "");
            String adapterError = status.optString("adapter_error", "");
            String errors = (error + " " + adapterError).trim();

            String state;
            String next;
            if (errors.toUpperCase(Locale.US).contains("SAFE_MODE")) {
                state = "SAFE_MODE";
                next = "导出诊断，修复适配包后再解除安全模式";
            } else if (!running) {
                state = errors.isEmpty() ? "STOPPED" : "LIMITED";
                next = errors.isEmpty() ? "配置视频源后点击保存并启动" : "导出诊断并检查 Root 服务";
            } else if (!connected) {
                state = "LIMITED";
                next = "检查本地媒体授权或私网 MJPEG 地址";
            } else if ("external".equals(target) && cameraReady) {
                state = "READY_UNVERIFIED";
                next = "在目标 OTA 上完成画面、方向与压力验收";
            } else if (!"external".equals(target) && frameBusReady && replacementAttached) {
                state = "READY_UNVERIFIED";
                next = "前/后摄替换已附着，仍需目标 OTA 内容验收";
            } else {
                state = "LIMITED";
                next = "当前链路未完全就绪；核对 Provider 或精确固件 Profile";
            }

            StringBuilder view = new StringBuilder();
            view.append("产品状态：").append(state)
                    .append("\n进程：").append(running ? "运行" : "停止")
                    .append(" · 视频源：").append(connected ? "已连接" : "未连接")
                    .append("\n目标：").append(target)
                    .append(" · External：").append(cameraReady ? "就绪" : "未就绪")
                    .append(" · FrameBus：").append(frameBusReady ? "就绪" : "未就绪")
                    .append(" · 适配器：").append(replacementAttached ? "已附着" : "未附着")
                    .append("\n验证级别：UNVERIFIED（必须按设备/OTA 验收）")
                    .append("\n下一步：").append(next);
            if (!errors.isEmpty()) {
                view.append("\n错误：").append(errors);
            }
            return view.toString();
        } catch (JSONException malformed) {
            return "产品状态：LIMITED\n状态数据无法解析，请导出诊断。";
        }
    }
}
