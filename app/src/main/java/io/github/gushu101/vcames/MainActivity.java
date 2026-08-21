package io.github.gushu101.vcames;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.pm.PackageManager;
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

import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final long STATUS_INTERVAL_MS = 1000;
    private static final int PICK_VIDEO_REQUEST = 1001;
    private static final int EXPORT_DIAGNOSTICS_REQUEST = 1002;
    private static final int NOTIFICATION_PERMISSION_REQUEST = 1003;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final DeploymentBridge deploymentBridge = DeploymentBridge.create();
    private final ExecutorService ioExecutor = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "vcames-ui-io");
        thread.setDaemon(true);
        return thread;
    });

    private EditText urlInput;
    private EditText deviceInput;
    private EditText widthInput;
    private EditText heightInput;
    private EditText fpsInput;
    private EditText staleInput;
    private EditText qualityInput;
    private Spinner sourceInput;
    private Spinner targetInput;
    private Spinner rotationInput;
    private CheckBox mirrorInput;
    private CheckBox holdLastInput;
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
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                        != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(
                    new String[]{Manifest.permission.POST_NOTIFICATIONS},
                    NOTIFICATION_PERMISSION_REQUEST);
        }
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
        title.setText("VCamES 2.1 · System Camera");
        title.setTextSize(24);
        title.setTextColor(Color.rgb(13, 27, 42));
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        root.addView(title, matchWrap());

        TextView subtitle = new TextView(this);
        subtitle.setText("Google / 小米 / 三星 · Android 11–13 · ROOT · 无 Xposed");
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
        deploymentView.setText("部署状态尚未检查。ROOT 版支持 KernelSU/Magisk 能力检测；"
                + "系统版不会调用 su。");
        deploymentView.setTextSize(13);
        deploymentView.setTextColor(Color.rgb(45, 55, 72));
        root.addView(deploymentView, matchWithBottom(6));

        Button deploy = new Button(this);
        deploy.setText(deploymentBridge.actionLabel());
        deploy.setOnClickListener(view -> authorizeAndDeploy());
        root.addView(deploy, matchWithBottom(14));

        Button diagnostics = new Button(this);
        diagnostics.setText("生成并导出兼容性诊断");
        diagnostics.setOnClickListener(view -> exportDiagnostics());
        root.addView(diagnostics, matchWithBottom(14));

        urlInput = addTextField(root, "HTTP MJPEG 地址", InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_VARIATION_URI);
        root.addView(fieldLabel("视频来源"), matchWrap());
        sourceInput = new Spinner(this);
        sourceInput.setAdapter(new ArrayAdapter<>(
                this,
                android.R.layout.simple_spinner_dropdown_item,
                new String[]{"HTTP MJPEG", "本地视频（循环）"}));
        root.addView(sourceInput, matchWithBottom(6));

        root.addView(fieldLabel("替换目标"), matchWrap());
        targetInput = new Spinner(this);
        targetInput.setAdapter(new ArrayAdapter<>(
                this,
                android.R.layout.simple_spinner_dropdown_item,
                new String[]{"外置相机（通用）", "前置摄像头", "后置摄像头", "前置 + 后置"}));
        root.addView(targetInput, matchWithBottom(8));

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

        deviceInput = addTextField(root, "V4L2 设备", InputType.TYPE_CLASS_TEXT);

        LinearLayout dimensions = horizontalRow();
        widthInput = addCompactNumber(dimensions, "宽度");
        heightInput = addCompactNumber(dimensions, "高度");
        fpsInput = addCompactNumber(dimensions, "FPS");
        root.addView(dimensions, matchWithBottom(10));

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

        holdLastInput = new CheckBox(this);
        holdLastInput.setText("断流后保持最后一帧");
        root.addView(holdLastInput, matchWrap());

        bootInput = new CheckBox(this);
        bootInput.setText("开机自动恢复");
        root.addView(bootInput, matchWithBottom(8));

        LinearLayout advanced = horizontalRow();
        staleInput = addCompactNumber(advanced, "断流 ms");
        qualityInput = addCompactNumber(advanced, "JPEG 质量");
        root.addView(advanced, matchWithBottom(14));

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
        note.setText("“外置相机”使用 AOSP external/0 Provider。前置/后置保留 OEM camera ID"
                + "和 metadata，通过共享 FrameBus 向精确固件适配器供帧。适配器必须同时匹配"
                + "厂商、SoC、HIDL/AIDL 实测结果和完整系统哈希；不匹配时拒绝启动并保留原相机。");
        note.setTextSize(13);
        note.setTextColor(Color.DKGRAY);
        note.setPadding(0, dp(16), 0, 0);
        root.addView(note, matchWrap());
        return scroll;
    }

    @SuppressLint("SetTextI18n")
    private void populate(VCamConfig config) {
        urlInput.setText(config.url);
        deviceInput.setText(config.device);
        widthInput.setText(Integer.toString(config.width));
        heightInput.setText(Integer.toString(config.height));
        fpsInput.setText(Integer.toString(config.fps));
        staleInput.setText(Integer.toString(config.staleTimeoutMs));
        qualityInput.setText(Integer.toString(config.jpegQuality));
        sourceInput.setSelection(config.url.equals("push://local") ? 1 : 0);
        targetInput.setSelection(targetPosition(config.target));
        rotationInput.setSelection(config.rotation / 90);
        mirrorInput.setChecked(config.mirror);
        holdLastInput.setChecked(config.holdLast);
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
                showStatus(DaemonClient.start(config));
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
                deviceInput.getText().toString().trim(),
                selectedTarget(),
                parseInt(widthInput, "宽度"),
                parseInt(heightInput, "高度"),
                parseInt(fpsInput, "FPS"),
                rotationInput.getSelectedItemPosition() * 90,
                mirrorInput.isChecked(),
                holdLastInput.isChecked(),
                parseInt(staleInput, "断流超时"),
                parseInt(qualityInput, "JPEG 质量"),
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

    private void authorizeAndDeploy() {
        deploymentView.setText("正在请求授权并检查部署…");
        ioExecutor.execute(() -> {
            String result = deploymentBridge.authorizeAndDeploy(this);
            mainHandler.post(() -> deploymentView.setText(result));
        });
    }

    private void exportDiagnostics() {
        deploymentView.setText(R.string.diagnostics_collecting);
        ioExecutor.execute(() -> {
            pendingDiagnostics = "VCamES 2.1 compatibility report\n"
                    + "generated_at_ms=" + System.currentTimeMillis() + "\n\n"
                    + DeviceProfiler.collect(this) + "\n\n"
                    + deploymentBridge.diagnostics(this) + "\n";
            mainHandler.post(() -> {
                Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("text/plain");
                intent.putExtra(Intent.EXTRA_TITLE, "vcames-compatibility-report.txt");
                startActivityForResult(intent, EXPORT_DIAGNOSTICS_REQUEST);
            });
        });
    }

    private String selectedTarget() {
        switch (targetInput.getSelectedItemPosition()) {
            case 1:
                return "front";
            case 2:
                return "back";
            case 3:
                return "both";
            default:
                return "external";
        }
    }

    private static int targetPosition(String target) {
        if (target.equals("front")) {
            return 1;
        }
        if (target.equals("back")) {
            return 2;
        }
        if (target.equals("both")) {
            return 3;
        }
        return 0;
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
                output.write(pendingDiagnostics.getBytes(StandardCharsets.UTF_8));
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
    }

    @SuppressLint("SetTextI18n")
    private void updateLocalMediaLabel() {
        if (localMediaView != null) {
            localMediaView.setText(localMediaUri.isEmpty() ? "未选择" : localMediaUri);
        }
    }

    private static int parseInt(EditText input, String label) {
        try {
            return Integer.parseInt(input.getText().toString().trim());
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException(label + "不是有效整数", e);
        }
    }

    @SuppressLint("SetTextI18n")
    private void showStatus(String rawStatus) {
        String readable = rawStatus
                .replace("{", "")
                .replace("}", "")
                .replace("\"", "")
                .replace(",", "  ·  ")
                .replace(":", ": ");
        mainHandler.post(() -> statusView.setText(readable));
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
    private EditText addCompactNumber(LinearLayout parent, String hint) {
        EditText input = new EditText(this);
        input.setHint(hint);
        input.setSingleLine(true);
        input.setInputType(InputType.TYPE_CLASS_NUMBER);
        input.setTextSize(14);
        LinearLayout.LayoutParams params = weightedWrap(1);
        if (parent.getChildCount() > 0) {
            params.setMarginStart(dp(8));
        }
        parent.addView(input, params);
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
