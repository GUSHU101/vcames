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
            String error = status.optString("error", "");
            String errors = error.trim();

            String state;
            String next;
            if (errors.toUpperCase(Locale.US).contains("SAFE_MODE")) {
                state = "SAFE_MODE";
                next = "导出诊断，修复适配包后再解除安全模式";
            } else if (!running) {
                state = errors.isEmpty() ? "STOPPED" : "LIMITED";
                next = errors.isEmpty() ? "配置视频源后点击保存并启动" : "导出诊断并检查 Pixel 5 运行时";
            } else if (!connected) {
                state = "LIMITED";
                next = "检查本地媒体授权、网络或流媒体协议参数";
            } else if (cameraReady) {
                state = "GLOBAL_REPLACEMENT_ACTIVE";
                next = "系统相机 0/1 正从虚拟 V4L2 节点取帧，请在任意相机应用中确认画面";
            } else {
                state = "LIMITED";
                next = "核对 /dev/video100 与 Pixel 5 legacy/0 全局 Provider";
            }

            StringBuilder view = new StringBuilder();
            view.append("产品状态：").append(state)
                    .append("\n进程：").append(running ? "运行" : "停止")
                    .append(" · 视频源：").append(connected ? "已连接" : "未连接")
                    .append("\n范围：全局前置+后置 · 相机 ID：0/1")
                    .append(" · 替换输出：").append(cameraReady ? "就绪" : "未就绪")
                    .append("\n链路：流媒体/本地视频 → FFmpeg → V4L2 → legacy/0 Provider → CameraService")
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
