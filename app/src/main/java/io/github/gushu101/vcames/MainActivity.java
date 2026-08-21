package io.github.gushu101.vcames;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.content.pm.PackageInfo;
import android.graphics.Color;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class MainActivity extends Activity {
    private static final long STATUS_INTERVAL_MS = 1000;
    private static final int PICK_VIDEO_REQUEST = 1001;
    private static final int EXPORT_DIAGNOSTICS_REQUEST = 1002;
    private static final int NOTIFICATION_PERMISSION_REQUEST = 1003;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final DeploymentManager deploymentManager = new DeploymentManager();
    private final ExecutorService ioExecutor = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "vcames-ui-io");
        thread.setDaemon(true);
        return thread;
    });

    private EditText urlInput;
    private Spinner sourceInput;
    private Spinner rotationInput;
    private CheckBox mirrorInput;
    private CheckBox bootInput;
    private TextView statusView;
    private TextView localMediaView;
    private TextView deploymentView;
    private String localMediaUri = "";
    private String pendingDiagnostics = "";
    private boolean polling;

    private final Runnable statusPoll = new Runnable() {
        @Override
        public void run() {
            if (!polling) {
                return;
            }
            ioExecutor.execute(() -> {
                try {
                    showStatus(DaemonClient.status());
                } catch (IOException e) {
                    showStatus("{\"running\":false,\"error\":\"无法连接 vcamesd\"}");
                }
            });
            mainHandler.postDelayed(this, STATUS_INTERVAL_MS);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(buildContent());
        populate(VCamConfig.load(this));
        localMediaUri = VCamConfig.loadLocalUri(this);
        updateLocalMediaLabel();
        checkRootAccess();
    }

    @Override
    protected void onResume() {
        super.onResume();
        polling = true;
        mainHandler.post(statusPoll);
    }

    @Override
    protected void onPause() {
        polling = false;
        mainHandler.removeCallbacks(statusPoll);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        ioExecutor.shutdownNow();
        super.onDestroy();
    }

    @SuppressLint("SetTextI18n")
    private View buildContent() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(18), dp(20), dp(28));
        root.setBackgroundColor(Color.rgb(245, 247, 250));
        scroll.addView(root, matchWrap());

        TextView title = new TextView(this);
        title.setText("VCamES " + appVersion() + " · System Camera");
        title.setTextSize(24);
        title.setTextColor(Color.rgb(13, 27, 42));
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        root.addView(title, matchWrap());

        TextView subtitle = new TextView(this);
        subtitle.setText("Pixel 5 (redfin) · 原厂 Android 11–14 · 预先 ROOT");
        subtitle.setTextSize(14);
        subtitle.setTextColor(Color.DKGRAY);
        subtitle.setPadding(0, dp(4), 0, dp(16));
        root.addView(subtitle, matchWrap());

        statusView = new TextView(this);
        statusView.setText("正在读取系统服务状态…");
        statusView.setTextColor(Color.WHITE);
        statusView.setTextSize(14);
        statusView.setPadding(dp(14), dp(12), dp(14), dp(12));
        statusView.setBackgroundColor(Color.rgb(13, 71, 102));
        root.addView(statusView, matchWithBottom(16));

        deploymentView = new TextView(this);
        deploymentView.setText("正在请求 su 并检测 uid 0。APK 不会安装、升级或配置 KSU/Magisk。");
        deploymentView.setTextSize(13);
        deploymentView.setTextColor(Color.rgb(45, 55, 72));
        root.addView(deploymentView, matchWithBottom(6));

        Button deploy = new Button(this);
        deploy.setText(deploymentManager.actionLabel());
        deploy.setOnClickListener(view -> checkRootAccess());
        root.addView(deploy, matchWithBottom(14));

        Button diagnostics = new Button(this);
        diagnostics.setText("运行自检并导出诊断");
        diagnostics.setOnClickListener(view -> exportDiagnostics());
        root.addView(diagnostics, matchWithBottom(14));

        urlInput = addTextField(root, "流媒体地址", InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_VARIATION_URI);
        root.addView(fieldLabel("视频来源"), matchWrap());
        sourceInput = new Spinner(this);
        sourceInput.setAdapter(new ArrayAdapter<>(
                this,
                android.R.layout.simple_spinner_dropdown_item,
                new String[]{"网络流（HTTP/RTMP/RTSP/SRT/RIST/RTP 等）", "本地视频（循环）"}));
        root.addView(sourceInput, matchWithBottom(6));

        LinearLayout localMediaRow = horizontalRow();
        Button chooseMedia = new Button(this);
        chooseMedia.setText("选择本地视频");
        chooseMedia.setOnClickListener(view -> chooseLocalMedia());
        localMediaRow.addView(chooseMedia, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
        localMediaView = new TextView(this);
        localMediaView.setTextSize(12);
        localMediaView.setTextColor(Color.DKGRAY);
        LinearLayout.LayoutParams mediaParams = weightedWrap(1);
        mediaParams.setMarginStart(dp(10));
        localMediaRow.addView(localMediaView, mediaParams);
        root.addView(localMediaRow, matchWithBottom(8));

        TextView rotationLabel = fieldLabel("顺时针旋转");
        root.addView(rotationLabel, matchWrap());
        rotationInput = new Spinner(this);
        rotationInput.setAdapter(new ArrayAdapter<>(
                this,
                android.R.layout.simple_spinner_dropdown_item,
                new String[]{"0°", "90°", "180°", "270°"}));
        root.addView(rotationInput, matchWithBottom(8));

        mirrorInput = new CheckBox(this);
        mirrorInput.setText("水平镜像输出");
        root.addView(mirrorInput, matchWrap());

        bootInput = new CheckBox(this);
        bootInput.setText("开机自动恢复");
        root.addView(bootInput, matchWithBottom(8));

        LinearLayout actions = horizontalRow();
        Button start = new Button(this);
        start.setText("保存并启动");
        start.setOnClickListener(view -> startCamera());
        actions.addView(start, weightedWrap(1));

        Button stop = new Button(this);
        stop.setText("停止");
        stop.setOnClickListener(view -> stopCamera());
        LinearLayout.LayoutParams stopParams = weightedWrap(1);
        stopParams.setMarginStart(dp(10));
        actions.addView(stop, stopParams);
        root.addView(actions, matchWrap());

        TextView note = new TextView(this);
        note.setText("Pixel 5 专用运行时在 CameraService 下方接管 legacy/0 Provider，系统相机 0（后置）"
                + "和 1（前置）默认同时映射到 /dev/video100；不需要选择应用或摄像头。"
                + "支持 HTTP(S)/HLS/DASH、RTMP(S)、RTSP(S)、SRT、RIST、RTP/SRTP、UDP/TCP 等"
                + "FFmpeg 网络输入。本地文件只通过系统文件选择器读取。ROOT 必须由用户预先配置并授权。");
        note.setTextSize(13);
        note.setTextColor(Color.DKGRAY);
        note.setPadding(0, dp(16), 0, 0);
        root.addView(note, matchWrap());
        return scroll;
    }

    @SuppressLint("SetTextI18n")
    private void populate(VCamConfig config) {
        urlInput.setText(config.url);
        sourceInput.setSelection(config.url.equals("push://local") ? 1 : 0);
        rotationInput.setSelection(config.rotation / 90);
        mirrorInput.setChecked(config.mirror);
        bootInput.setChecked(config.startOnBoot);
    }

    private void startCamera() {
        final VCamConfig config;
        try {
            config = readConfig();
            if (config.url.equals("push://local") && localMediaUri.isEmpty()) {
                throw new IllegalArgumentException("请先选择本地视频");
            }
            config.save(this);
        } catch (IllegalArgumentException e) {
            toast(e.getMessage());
            return;
        }
        ioExecutor.execute(() -> {
            try {
                DeploymentManager.StartReadiness readiness =
                        deploymentManager.checkStartReadiness(this);
                if (!readiness.ready) {
                    throw new IllegalArgumentException(
                            readiness.state + "：" + readiness.message);
                }
                showStatus(DaemonClient.start(this, config));
                if (config.url.equals("push://local")) {
                    LocalMediaService.start(this, localMediaUri);
                } else {
                    LocalMediaService.stop(this);
                }
                toast("VCamES 已启动");
            } catch (IOException | IllegalArgumentException e) {
                toast("启动失败：" + e.getMessage());
            }
        });
    }

    private void stopCamera() {
        ioExecutor.execute(() -> {
            try {
                LocalMediaService.stop(this);
                showStatus(DaemonClient.stop());
                toast("VCamES 已停止");
            } catch (IOException e) {
                toast("停止失败：" + e.getMessage());
            }
        });
    }

    private VCamConfig readConfig() {
        return new VCamConfig(
                sourceInput.getSelectedItemPosition() == 1
                        ? "push://local"
                        : urlInput.getText().toString().trim(),
                rotationInput.getSelectedItemPosition() * 90,
                mirrorInput.isChecked(),
                bootInput.isChecked());
    }

    private void chooseLocalMedia() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("video/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, PICK_VIDEO_REQUEST);
    }

    @SuppressLint("SetTextI18n")
    private void checkRootAccess() {
        deploymentView.setText("正在请求 su 并检测 uid 0…");
        ioExecutor.execute(() -> {
            String result = deploymentManager.checkRootAccess(this);
            mainHandler.post(() -> deploymentView.setText(result));
        });
    }

    private void exportDiagnostics() {
        deploymentView.setText(R.string.diagnostics_collecting);
        ioExecutor.execute(() -> {
            pendingDiagnostics = buildDiagnosticsReport();
            mainHandler.post(() -> {
                Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("application/zip");
                intent.putExtra(Intent.EXTRA_TITLE, "vcames-diagnostics.zip");
                startActivityForResult(intent, EXPORT_DIAGNOSTICS_REQUEST);
            });
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null) {
            return;
        }
        Uri uri = data.getData();
        if (uri == null) {
            return;
        }
        if (requestCode == EXPORT_DIAGNOSTICS_REQUEST) {
            try (OutputStream output = getContentResolver().openOutputStream(uri, "wt")) {
                if (output == null) {
                    throw new IOException("文档提供器没有返回输出流");
                }
                writeDiagnosticsZip(output, pendingDiagnostics);
                deploymentView.setText(R.string.diagnostics_exported);
            } catch (IOException e) {
                toast("诊断导出失败：" + e.getMessage());
            }
            return;
        }
        if (requestCode != PICK_VIDEO_REQUEST) {
            return;
        }
        try {
            getContentResolver().takePersistableUriPermission(
                    uri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {
            // Some document providers grant access without a persistable permission.
        }
        localMediaUri = uri.toString();
        VCamConfig.saveLocalUri(this, localMediaUri);
        sourceInput.setSelection(1);
        updateLocalMediaLabel();
        requestNotificationPermissionForLocalPlayback();
    }

    @SuppressLint("SetTextI18n")
    private void updateLocalMediaLabel() {
        if (localMediaView != null) {
            localMediaView.setText(localMediaUri.isEmpty() ? "未选择" : localMediaUri);
        }
    }

    private void requestNotificationPermissionForLocalPlayback() {
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                        != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},
                    NOTIFICATION_PERMISSION_REQUEST);
        }
    }

    @SuppressLint("SetTextI18n")
    private void showStatus(String rawStatus) {
        String readable = ProductStatusPresenter.render(rawStatus);
        int statusColor;
        if (readable.contains("SAFE_MODE")) {
            statusColor = Color.rgb(143, 35, 35);
        } else if (readable.contains("LIMITED")) {
            statusColor = Color.rgb(151, 94, 18);
        } else if (readable.contains("GLOBAL_REPLACEMENT_ACTIVE")) {
            statusColor = Color.rgb(31, 112, 78);
        } else {
            statusColor = Color.rgb(13, 71, 102);
        }
        mainHandler.post(() -> {
            statusView.setText(readable);
            statusView.setBackgroundColor(statusColor);
        });
    }

    private String buildDiagnosticsReport() {
        JSONObject report = new JSONObject();
        try {
            report.put("report_schema", 1);
            report.put("app_version", appVersion());
            report.put("generated_at_ms", System.currentTimeMillis());
            report.put("product_scope", "pixel5-redfin-stock-android11-14");
            String rootDiagnostics = deploymentManager.diagnostics(this);
            report.put("device_probe", DeviceProbe.collect(this));
            report.put("self_test", SelfTest.run(rootDiagnostics));
            report.put("root_diagnostics", rootDiagnostics);
            report.put("user_media_included", false);
            report.put("privacy_note", "不包含用户视频、帧内容、账号或网络凭据");
            return report.toString(2);
        } catch (JSONException failure) {
            return "{\"report_schema\":1,\"error\":\"diagnostic serialization failed\","
                    + "\"user_media_included\":false}";
        }
    }

    private static void writeDiagnosticsZip(OutputStream output, String report) throws IOException {
        try (ZipOutputStream zip = new ZipOutputStream(new BufferedOutputStream(output))) {
            zip.putNextEntry(new ZipEntry("report.json"));
            zip.write(report.getBytes(StandardCharsets.UTF_8));
            zip.closeEntry();
            zip.putNextEntry(new ZipEntry("README.txt"));
            zip.write(("VCamES 兼容性诊断包\n"
                    + "只包含 Pixel 5 设备、Camera2、ROOT、全局 Provider 与进程状态；不包含任何用户媒体。\n"
                    + "GLOBAL_REPLACEMENT_ACTIVE 仍需在对应原厂 OTA 上完成功能与压力验收。\n")
                    .getBytes(StandardCharsets.UTF_8));
            zip.closeEntry();
        }
    }

    private String appVersion() {
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            return info.versionName == null ? "unknown" : info.versionName;
        } catch (PackageManager.NameNotFoundException impossible) {
            return "unknown";
        }
    }

    private void toast(String message) {
        mainHandler.post(() -> Toast.makeText(this, message, Toast.LENGTH_LONG).show());
    }

    @SuppressLint("SetTextI18n")
    private EditText addTextField(LinearLayout parent, String label, int inputType) {
        parent.addView(fieldLabel(label), matchWrap());
        EditText input = new EditText(this);
        input.setSingleLine(true);
        input.setInputType(inputType);
        input.setTextSize(15);
        parent.addView(input, matchWithBottom(10));
        return input;
    }

    @SuppressLint("SetTextI18n")
    private TextView fieldLabel(String label) {
        TextView view = new TextView(this);
        view.setText(label);
        view.setTextSize(13);
        view.setTextColor(Color.DKGRAY);
        return view;
    }

    private LinearLayout horizontalRow() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        return row;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private static LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams matchWithBottom(int bottomDp) {
        LinearLayout.LayoutParams params = matchWrap();
        params.bottomMargin = dp(bottomDp);
        return params;
    }

    private static LinearLayout.LayoutParams weightedWrap(int weight) {
        return new LinearLayout.LayoutParams(
                0,
                LinearLayout.LayoutParams.WRAP_CONTENT,
                weight);
    }
}
