package com.galaxy.installer;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.text.method.ScrollingMovementMethod;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;

public class MainActivity extends Activity {
    private static final String TAG = "GalaxyInstaller";
    private static final int REQ_PICK_SOURCE = 1001;

    private TextView txtSourcePath;
    private CheckBox chkIncludeMovies;
    private Button btnSelectSource;
    private Button btnStartCooking;
    private Button btnLaunchGame;
    private ProgressBar progressBar;
    private TextView txtProgressStatus;
    private TextView txtLogOutput;

    private String mSelectedSourcePath = "";
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private boolean mIsCooking = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        txtSourcePath = findViewById(R.id.txtSourcePath);
        chkIncludeMovies = findViewById(R.id.chkIncludeMovies);
        btnSelectSource = findViewById(R.id.btnSelectSource);
        btnStartCooking = findViewById(R.id.btnStartCooking);
        btnLaunchGame = findViewById(R.id.btnLaunchGame);
        progressBar = findViewById(R.id.progressBar);
        txtProgressStatus = findViewById(R.id.txtProgressStatus);
        txtLogOutput = findViewById(R.id.txtLogOutput);

        txtLogOutput.setMovementMethod(new ScrollingMovementMethod());

        checkStoragePermissions();
        autoDetectSource();

        btnSelectSource.setOnClickListener(v -> openFilePicker());

        btnStartCooking.setOnClickListener(v -> {
            if (mSelectedSourcePath.isEmpty()) {
                Toast.makeText(this, "Please select a game file or extracted folder first", Toast.LENGTH_SHORT).show();
                return;
            }
            startCookingProcess();
        });

        btnLaunchGame.setOnClickListener(v -> launchGalaxyMobile());
    }

    private void checkStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                appendLog("[Permission] Requesting All Files Access for reading game dumps...");
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + getPackageName()));
                try {
                    startActivity(intent);
                } catch (Exception e) {
                    Intent generic = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    startActivity(generic);
                }
            }
        }
    }

    private void autoDetectSource() {
        String[] candidates = {
            "/sdcard/Download/Super Mario Galaxy.iso",
            "/sdcard/Download/Super Mario Galaxy.rvz",
            "/sdcard/Download/extracted",
            "/sdcard/Download/DATA",
            "/sdcard/GalaxyQuest/extracted",
            "/sdcard/extracted"
        };
        for (String c : candidates) {
            File f = new File(c);
            if (f.exists()) {
                mSelectedSourcePath = c;
                txtSourcePath.setText("Auto-detected: " + c);
                appendLog("[Found] Auto-detected game data at: " + c);
                break;
            }
        }
    }

    private void openFilePicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(intent, REQ_PICK_SOURCE);
        } catch (Exception e) {
            Toast.makeText(this, "Failed to open document picker: " + e.getMessage(), Toast.LENGTH_SHORT).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_PICK_SOURCE && resultCode == RESULT_OK && data != null) {
            Uri treeUri = data.getData();
            if (treeUri != null) {
                // Resolving standard path if possible, or using tree URI
                String path = treeUri.getPath();
                if (path != null && path.contains(":")) {
                    String[] parts = path.split(":");
                    if (parts.length > 1) {
                        mSelectedSourcePath = "/sdcard/" + parts[1];
                    } else {
                        mSelectedSourcePath = path;
                    }
                } else {
                    mSelectedSourcePath = path != null ? path : treeUri.toString();
                }
                txtSourcePath.setText(mSelectedSourcePath);
                appendLog("[Source] Selected: " + mSelectedSourcePath);
            }
        }
    }

    private void startCookingProcess() {
        if (mIsCooking) return;
        mIsCooking = true;

        btnStartCooking.setEnabled(false);
        btnSelectSource.setEnabled(false);
        progressBar.setVisibility(View.VISIBLE);
        txtProgressStatus.setText(R.string.status_cooking);

        final String inputDir = mSelectedSourcePath;
        // Output directory is standard location automatically scanned by GalaxyMobile
        final String outputDir = "/sdcard/Download/game";
        final boolean withMovies = chkIncludeMovies.isChecked();

        appendLog("[Cook] Starting game conversion pipeline...");
        appendLog("[Cook] Input:  " + inputDir);
        appendLog("[Cook] Output: " + outputDir);

        new Thread(() -> {
            try {
                // Step 1: Prepare assets
                CookerEngine.prepareAssets(MainActivity.this, new CookerEngine.CookerCallback() {
                    @Override public void onLog(String line) { appendLog(line); }
                    @Override public void onProgress(int percent, String message) {}
                    @Override public void onFinished(boolean success, String message) {}
                });

                String pythonHome = new File(getFilesDir(), "pythonhome").getAbsolutePath();
                String scriptsDir = new File(getFilesDir(), "scripts").getAbsolutePath();

                // Step 2: Ensure destination directory exists
                File outFolder = new File(outputDir);
                if (!outFolder.exists()) {
                    outFolder.mkdirs();
                }

                // Step 3: Run the native cooker
                int ret = CookerEngine.runCooker(pythonHome, scriptsDir, inputDir, outputDir, withMovies, new CookerEngine.CookerCallback() {
                    @Override
                    public void onLog(String line) {
                        appendLog(line);
                    }

                    @Override
                    public void onProgress(int percent, String message) {
                        mHandler.post(() -> {
                            if (percent >= 0) {
                                progressBar.setIndeterminate(false);
                                progressBar.setProgress(percent);
                            }
                            if (message != null) {
                                txtProgressStatus.setText(message);
                            }
                        });
                    }

                    @Override
                    public void onFinished(boolean success, String message) {
                        mHandler.post(() -> onCookingCompleted(success, message));
                    }
                });

                if (ret != 0) {
                    mHandler.post(() -> onCookingCompleted(false, "Cooker returned error code: " + ret));
                }

            } catch (Exception e) {
                Log.e(TAG, "Cooking failed with exception", e);
                appendLog("[ERROR] " + e.getMessage());
                mHandler.post(() -> onCookingCompleted(false, e.getMessage()));
            }
        }).start();
    }

    private void onCookingCompleted(boolean success, String message) {
        mIsCooking = false;
        progressBar.setVisibility(View.GONE);
        btnStartCooking.setEnabled(true);
        btnSelectSource.setEnabled(true);

        if (success) {
            txtProgressStatus.setText(R.string.status_done);
            appendLog("[SUCCESS] " + (message != null ? message : "Conversion complete!"));
            btnLaunchGame.setVisibility(View.VISIBLE);
            Toast.makeText(this, "Game data successfully prepared!", Toast.LENGTH_LONG).show();
        } else {
            txtProgressStatus.setText(R.string.status_error);
            appendLog("[FAILURE] " + (message != null ? message : "Error during conversion."));
            Toast.makeText(this, "Cooking failed. Check log for details.", Toast.LENGTH_LONG).show();
        }
    }

    private void launchGalaxyMobile() {
        Intent launchIntent = getPackageManager().getLaunchIntentForPackage("com.galaxy.quest");
        if (launchIntent != null) {
            startActivity(launchIntent);
        } else {
            Toast.makeText(this, "GalaxyMobile (com.galaxy.quest) is not installed yet!", Toast.LENGTH_LONG).show();
        }
    }

    private void appendLog(String line) {
        mHandler.post(() -> {
            txtLogOutput.append(line + "\n");
            // Auto-scroll to bottom
            final int scrollAmount = txtLogOutput.getLayout() != null ?
                    txtLogOutput.getLayout().getLineTop(txtLogOutput.getLineCount()) - txtLogOutput.getHeight() : 0;
            if (scrollAmount > 0) {
                txtLogOutput.scrollTo(0, scrollAmount);
            }
        });
    }
}
