package com.openport.galaxyandroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.text.method.ScrollingMovementMethod;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;

public class MainActivity extends Activity {
    private static final String PREFS_NAME = "GalaxyPrefs";
    private static final int REQ_PICK_DISC = 1001;
    private static final int REQ_PICK_DRIVER = 1002;

    private TextView txtGameStatusBadge, txtGameStatusDetail;
    private TextView txtGpuHardwareInfo, txtDriverStatusBadge, txtDriverDetail;
    private TextView txtSourcePath, txtProgressStatus, txtLogOutput;
    private CheckBox chkIncludeMovies;
    private Button btnSelectDisc, btnAutoDetect, btnStartCooking, btnLaunchGame, btnResetData;
    private Button btnInstallDriver, btnResetDriver, btnRes480, btnRes720, btnRes1080;
    private ProgressBar progressBar;

    private String mSelectedSourcePath = "";
    private Uri mSelectedSourceUri = null;
    private String mSelectedDisplayName = "";
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private boolean mIsCooking = false;
    private SharedPreferences mPrefs;

    public static boolean checkDataDir(File dataDir) { return GameDataLocator.checkDataDir(dataDir); }
    public static File findGameDataFolder(Context context) { return GameDataLocator.findGameDataFolder(context); }
    public static boolean isGameDataReady() { return GameDataLocator.isGameDataReady(); }
    public static boolean isGameDataReady(Context context) { return GameDataLocator.isGameDataReady(context); }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        setContentView(R.layout.activity_main);
        mPrefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);

        initViews();
        setupHardwareAndDriverInfo();
        StorageUtils.checkStoragePermissions(this);
        autoDetectSource();
        refreshGameStatus();
        setupListeners();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshGameStatus();
        CustomDriverInstaller.refreshDriverStatus(this, mPrefs, txtDriverStatusBadge, txtDriverDetail);
        PresetManager.refreshResolutionButtons(this, mPrefs, btnRes480, btnRes720, btnRes1080);
    }

    private void initViews() {
        txtGameStatusBadge = findViewById(R.id.txtGameStatusBadge);
        txtGameStatusDetail = findViewById(R.id.txtGameStatusDetail);
        txtSourcePath = findViewById(R.id.txtSourcePath);
        chkIncludeMovies = findViewById(R.id.chkIncludeMovies);
        btnSelectDisc = findViewById(R.id.btnSelectDisc);
        btnAutoDetect = findViewById(R.id.btnAutoDetect);
        btnStartCooking = findViewById(R.id.btnStartCooking);
        btnLaunchGame = findViewById(R.id.btnLaunchGame);
        btnResetData = findViewById(R.id.btnResetData);

        txtGpuHardwareInfo = findViewById(R.id.txtGpuHardwareInfo);
        txtDriverStatusBadge = findViewById(R.id.txtDriverStatusBadge);
        txtDriverDetail = findViewById(R.id.txtDriverDetail);
        btnInstallDriver = findViewById(R.id.btnInstallDriver);
        btnResetDriver = findViewById(R.id.btnResetDriver);

        btnRes480 = findViewById(R.id.btnRes480);
        btnRes720 = findViewById(R.id.btnRes720);
        btnRes1080 = findViewById(R.id.btnRes1080);

        progressBar = findViewById(R.id.progressBar);
        txtProgressStatus = findViewById(R.id.txtProgressStatus);
        txtLogOutput = findViewById(R.id.txtLogOutput);
        txtLogOutput.setMovementMethod(new ScrollingMovementMethod());
        Button btnClearLog = findViewById(R.id.btnClearLog);
        if (btnClearLog != null) btnClearLog.setOnClickListener(v -> txtLogOutput.setText(""));
    }

    private void setupListeners() {
        if (btnRes480 != null) btnRes480.setOnClickListener(v -> applyResolutionPreset(0));
        if (btnRes720 != null) btnRes720.setOnClickListener(v -> applyResolutionPreset(1));
        if (btnRes1080 != null) btnRes1080.setOnClickListener(v -> applyResolutionPreset(2));
        PresetManager.refreshResolutionButtons(this, mPrefs, btnRes480, btnRes720, btnRes1080);

        btnSelectDisc.setOnClickListener(v -> openPicker(REQ_PICK_DISC));
        btnAutoDetect.setOnClickListener(v -> autoDetectSource());
        btnInstallDriver.setOnClickListener(v -> openPicker(REQ_PICK_DRIVER));
        btnResetDriver.setOnClickListener(v -> {
            CustomDriverInstaller.resetToSystemDriver(this, mPrefs, this::appendLog);
            CustomDriverInstaller.refreshDriverStatus(this, mPrefs, txtDriverStatusBadge, txtDriverDetail);
        });

        btnStartCooking.setOnClickListener(v -> {
            if (mSelectedSourcePath.isEmpty()) {
                Toast.makeText(this, "Please select a game disc image (.rvz / .iso / .wbfs)", Toast.LENGTH_SHORT).show();
                return;
            }
            startCookingProcess();
        });

        btnLaunchGame.setOnClickListener(v -> launchGameDirectly());
        btnResetData.setOnClickListener(v -> showResetDialog());
    }

    private void applyResolutionPreset(int scale) {
        String label = PresetManager.setResolutionPreset(this, mPrefs, scale);
        PresetManager.refreshResolutionButtons(this, mPrefs, btnRes480, btnRes720, btnRes1080);
        appendLog("[Preset] Resolution set to: " + label);
        Toast.makeText(this, "Preset: " + label, Toast.LENGTH_SHORT).show();
    }

    private void setupHardwareAndDriverInfo() {
        txtGpuHardwareInfo.setText(CustomDriverInstaller.getHardwareInfo());
        CustomDriverInstaller.refreshDriverStatus(this, mPrefs, txtDriverStatusBadge, txtDriverDetail);
    }

    private void refreshGameStatus() {
        File foundDir = GameDataLocator.findGameDataFolder(this);
        if (foundDir != null) {
            btnLaunchGame.setVisibility(View.VISIBLE);
            txtGameStatusBadge.setText("READY TO PLAY");
            txtGameStatusBadge.setBackgroundResource(R.drawable.badge_bg_green);
            txtGameStatusBadge.setTextColor(getColor(R.color.success_green));
            txtGameStatusDetail.setText("Game data verified: " + foundDir.getAbsolutePath() + ". Tap Launch to play!");
            txtProgressStatus.setText("Ready to play! Tap Launch Game.");
        } else {
            btnLaunchGame.setVisibility(View.GONE);
            txtGameStatusBadge.setText("INSTALLATION NEEDED");
            txtGameStatusBadge.setBackgroundResource(R.drawable.badge_bg_amber);
            txtGameStatusBadge.setTextColor(getColor(R.color.warning_amber));
            txtGameStatusDetail.setText("Game data not found in /sdcard/GalaxyData or MicroSD. Select a disc below to install.");
            txtProgressStatus.setText("Awaiting game disc selection...");
        }
    }

    private void showResetDialog() {
        new AlertDialog.Builder(this)
            .setTitle("Reset Game Data")
            .setMessage("Are you sure you want to delete /sdcard/GalaxyData? This removes partial or broken installation files.")
            .setPositiveButton("Delete and Reset", (dialog, which) -> {
                File dataDir = new File("/sdcard/GalaxyData");
                StorageUtils.deleteRecursive(dataDir);
                refreshGameStatus();
                appendLog("[Reset] Cleared game directory: " + dataDir.getAbsolutePath());
                Toast.makeText(this, "Game data reset successfully.", Toast.LENGTH_SHORT).show();
            })
            .setNegativeButton("Cancel", null)
            .show();
    }

    private void launchGameDirectly() {
        startActivity(new Intent(this, GameActivity.class));
        finish();
    }

    private void autoDetectSource() {
        appendLog("[Scan] Searching for game dumps and extracted folders...");
        if (GameDataLocator.isGameDataReady()) {
            appendLog("[Scan] Game data is ready in /sdcard/GalaxyData");
            mHandler.post(this::refreshGameStatus);
        }
        File disc = GameDataLocator.autoDetectDiscImage();
        if (disc != null) {
            selectSource(disc.getAbsolutePath(), "Detected disc: " + disc.getName());
            return;
        }
        appendLog("[Scan] No disc image auto-detected. Use 'Select Disc File' button.");
    }

    private void selectSource(String path, String description) {
        mSelectedSourcePath = path;
        appendLog("[Selected] " + description);
        mHandler.post(() -> {
            txtSourcePath.setText(path);
            txtProgressStatus.setText("Disc image selected. Ready to install.");
            btnStartCooking.setEnabled(true);
        });
    }

    private void openPicker(int reqCode) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, reqCode);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        if (requestCode == REQ_PICK_DISC) {
            mSelectedSourceUri = uri;
            mSelectedDisplayName = StorageUtils.getDisplayName(this, uri);
            String path = StorageUtils.getPathFromUri(this, uri);
            mSelectedSourcePath = path != null ? path : uri.toString();
            String name = !mSelectedDisplayName.isEmpty() ? mSelectedDisplayName : (path != null ? new File(path).getName() : uri.toString());
            selectSource(mSelectedSourcePath, "Selected disc: " + name);
        } else if (requestCode == REQ_PICK_DRIVER) {
            CustomDriverInstaller.installDriverPackage(this, uri, mPrefs, mHandler, this::appendLog, () -> {
                CustomDriverInstaller.refreshDriverStatus(this, mPrefs, txtDriverStatusBadge, txtDriverDetail);
                txtProgressStatus.setText("Custom graphics driver installed and active!");
            });
        }
    }

    private void startCookingProcess() {
        if (mIsCooking) return;
        mIsCooking = true;
        btnStartCooking.setEnabled(false);
        btnSelectDisc.setEnabled(false);
        btnAutoDetect.setEnabled(false);
        progressBar.setVisibility(View.VISIBLE);
        progressBar.setIndeterminate(true);
        txtProgressStatus.setText("Preparing game installation...");

        CookerTask.start(this, mHandler, mSelectedSourcePath, mSelectedSourceUri, mSelectedDisplayName,
                chkIncludeMovies.isChecked(), new CookerTask.CookerTaskListener() {
            @Override public void onLog(String line) { appendLog(line); }
            @Override public void onProgress(int percent, String message) {
                mHandler.post(() -> {
                    if (percent >= 0) {
                        progressBar.setIndeterminate(false);
                        progressBar.setProgress(percent);
                    }
                    if (message != null) txtProgressStatus.setText(message);
                });
            }
            @Override public void onCompleted(boolean success, String message) {
                onCookingCompleted(success, message);
            }
        });
    }

    private void onCookingCompleted(boolean success, String message) {
        mIsCooking = false;
        progressBar.setVisibility(View.GONE);
        btnStartCooking.setEnabled(true);
        btnSelectDisc.setEnabled(true);
        btnAutoDetect.setEnabled(true);

        if (success) {
            try {
                new File(Environment.getExternalStorageDirectory(), "GalaxyData/.installed_ok").createNewFile();
            } catch (Exception ignored) {}
            txtProgressStatus.setText(R.string.status_done);
            appendLog("[SUCCESS] " + (message != null ? message : "Conversion complete!"));
            refreshGameStatus();
            Toast.makeText(this, "Game ready! Launching game now...", Toast.LENGTH_SHORT).show();
            mHandler.postDelayed(this::launchGameDirectly, 1000);
        } else {
            try {
                new File(Environment.getExternalStorageDirectory(), "GalaxyData/.installed_ok").delete();
            } catch (Exception ignored) {}
            txtProgressStatus.setText(R.string.status_error);
            appendLog("[FAILURE] " + (message != null ? message : "Error during installation."));
            Toast.makeText(this, "Installation failed. Check log.", Toast.LENGTH_LONG).show();
        }
    }

    private void appendLog(String line) {
        mHandler.post(() -> {
            txtLogOutput.append(line + "\n");
            final int scrollAmount = txtLogOutput.getLayout() != null ?
                    txtLogOutput.getLayout().getLineTop(txtLogOutput.getLineCount()) - txtLogOutput.getHeight() : 0;
            if (scrollAmount > 0) {
                txtLogOutput.scrollTo(0, scrollAmount);
            }
        });
    }
}
