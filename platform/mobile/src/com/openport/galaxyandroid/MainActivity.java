package com.openport.galaxyandroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
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
    private static final String TAG = "GalaxyAndroid";
    private static final int REQ_PICK_DISC = 1001;

    private TextView txtHeaderTitle;
    private TextView txtSourcePath;
    private CheckBox chkIncludeMovies;
    private Button btnSelectDisc;
    private Button btnAutoDetect;
    private Button btnStartCooking;
    private Button btnLaunchGame;
    private Button btnResetData;
    private ProgressBar progressBar;
    private TextView txtProgressStatus;
    private TextView txtLogOutput;

    private String mSelectedSourcePath = "";
    private Uri mSelectedSourceUri = null;
    private String mSelectedDisplayName = "";
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private boolean mIsCooking = false;

    public static boolean isGameDataReady() {
        File dataDir = new File("/sdcard/GalaxyData");
        if (!dataDir.exists()) {
            dataDir = new File(Environment.getExternalStorageDirectory(), "GalaxyData");
            if (!dataDir.exists()) return false;
        }
        File sentinel = new File(dataDir, ".installed_ok");
        File fst = new File(dataDir, "sys/fst.bin");
        File files = new File(dataDir, "files");
        File coin = new File(dataDir, "files/ObjectData/Coin.arc");
        if (!fst.exists() || !fst.isFile() || !files.exists() || !files.isDirectory() || !coin.exists()) {
            return false;
        }
        // Verify Coin.arc is converted to little-endian ARM64 ("CRAR")
        try (java.io.FileInputStream fis = new java.io.FileInputStream(coin)) {
            byte[] header = new byte[4];
            if (fis.read(header) == 4 && header[0] == 'C' && header[1] == 'R' && header[2] == 'A' && header[3] == 'R') {
                if (!sentinel.exists()) {
                    try { sentinel.createNewFile(); } catch (Exception ignored) {}
                }
                return true;
            }
        } catch (Exception ignored) {}
        return false;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);

        setContentView(R.layout.activity_main);

        txtHeaderTitle = findViewById(R.id.txtHeaderTitle);
        txtSourcePath = findViewById(R.id.txtSourcePath);
        chkIncludeMovies = findViewById(R.id.chkIncludeMovies);
        btnSelectDisc = findViewById(R.id.btnSelectDisc);
        btnAutoDetect = findViewById(R.id.btnAutoDetect);
        btnStartCooking = findViewById(R.id.btnStartCooking);
        btnLaunchGame = findViewById(R.id.btnLaunchGame);
        btnResetData = findViewById(R.id.btnResetData);
        progressBar = findViewById(R.id.progressBar);
        txtProgressStatus = findViewById(R.id.txtProgressStatus);
        txtLogOutput = findViewById(R.id.txtLogOutput);

        txtLogOutput.setMovementMethod(new ScrollingMovementMethod());

        checkStoragePermissions();
        autoDetectSource();

        if (isGameDataReady()) {
            btnLaunchGame.setVisibility(View.VISIBLE);
            btnLaunchGame.setText("Launch Installed Game");
            txtProgressStatus.setText("Installed game data verified! Tap to launch or select an ISO below to reinstall.");
        }

        btnSelectDisc.setOnClickListener(v -> openFilePicker());
        btnAutoDetect.setOnClickListener(v -> autoDetectSource());

        btnStartCooking.setOnClickListener(v -> {
            if (mSelectedSourcePath.isEmpty()) {
                Toast.makeText(this, "Please select a game disc image (.rvz / .iso / .wbfs)", Toast.LENGTH_SHORT).show();
                return;
            }
            startCookingProcess();
        });

        btnLaunchGame.setOnClickListener(v -> launchGameDirectly());

        btnResetData.setOnClickListener(v -> {
            new AlertDialog.Builder(this)
                .setTitle("Reset Game Data")
                .setMessage("Are you sure you want to delete /sdcard/GalaxyData? This removes partial or broken installation files so you can install cleanly.")
                .setPositiveButton("Delete & Reset", (dialog, which) -> {
                    File dataDir = new File("/sdcard/GalaxyData");
                    deleteRecursive(dataDir);
                    btnLaunchGame.setVisibility(View.GONE);
                    txtProgressStatus.setText("Cleaned /sdcard/GalaxyData. Ready for fresh install.");
                    appendLog("[Reset] Cleared game directory: " + dataDir.getAbsolutePath());
                    Toast.makeText(this, "Game data reset successfully.", Toast.LENGTH_SHORT).show();
                })
                .setNegativeButton("Cancel", null)
                .show();
        });
    }

    private void deleteRecursive(File fileOrDirectory) {
        if (fileOrDirectory == null || !fileOrDirectory.exists()) return;
        if (fileOrDirectory.isDirectory()) {
            File[] children = fileOrDirectory.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        fileOrDirectory.delete();
    }

    private void launchGameDirectly() {
        Intent intent = new Intent(this, GameActivity.class);
        startActivity(intent);
        finish();
    }

    private void checkStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                appendLog("[Permission] Requesting storage access for game data...");
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + getPackageName()));
                try {
                    startActivity(intent);
                } catch (Exception e) {
                    Intent generic = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    startActivity(generic);
                }
            }
        } else {
            if (checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                appendLog("[Permission] Requesting legacy storage permission for game data...");
                requestPermissions(new String[]{
                    android.Manifest.permission.READ_EXTERNAL_STORAGE,
                    android.Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, 2001);
            }
        }
    }

    private void autoDetectSource() {
        appendLog("[Scan] Searching for game dumps and extracted folders...");

        // 1. Check if /sdcard/GalaxyData already exists
        File galaxyDataDir = new File(Environment.getExternalStorageDirectory(), "GalaxyData");
        if (galaxyDataDir.exists()) {
            if (isGameDataReady()) {
                appendLog("[Scan] Game data is ready in " + galaxyDataDir.getAbsolutePath() + "!");
                mHandler.post(() -> {
                    btnLaunchGame.setVisibility(View.VISIBLE);
                    txtProgressStatus.setText("Game data ready! Tap Launch to play.");
                });
            }
            File[] gFiles = galaxyDataDir.listFiles();
            if (gFiles != null) {
                for (File f : gFiles) {
                    String name = f.getName().toLowerCase();
                    if (name.endsWith(".rvz") || name.endsWith(".wbfs") || name.endsWith(".iso")) {
                        selectSource(f.getAbsolutePath(), "Detected disc in GalaxyData: " + f.getName());
                        return;
                    }
                }
            }
        }

        // 2. Scan Downloads folder
        File downloadDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
        if (downloadDir != null && downloadDir.exists()) {
            File[] files = downloadDir.listFiles();
            if (files != null) {
                for (File f : files) {
                    String name = f.getName().toLowerCase();
                    if (name.endsWith(".rvz") || name.endsWith(".wbfs") || name.endsWith(".iso")) {
                        selectSource(f.getAbsolutePath(), "Detected disc in Downloads: " + f.getName());
                        return;
                    }
                }
                for (File f : files) {
                    if (f.isDirectory()) {
                        File testFiles = new File(f, "files");
                        File testData = new File(f, "DATA");
                        if (testFiles.exists() || testData.exists()) {
                            selectSource(f.getAbsolutePath(), "Detected extracted folder: " + f.getName());
                            return;
                        }
                    }
                }
            }
        }

        appendLog("[Scan] No disc image auto-detected. Use 'Choose Disc Image' button.");
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

    private void openFilePicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQ_PICK_DISC);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) return;

        Uri uri = data.getData();
        if (requestCode == REQ_PICK_DISC) {
            mSelectedSourceUri = uri;
            mSelectedDisplayName = getDisplayName(uri);
            String path = getPathFromUri(uri);
            if (path != null) {
                mSelectedSourcePath = path;
                selectSource(path, "Selected disc: " + (mSelectedDisplayName.isEmpty() ? new File(path).getName() : mSelectedDisplayName));
            } else {
                mSelectedSourcePath = uri.toString();
                selectSource(mSelectedDisplayName.isEmpty() ? uri.toString() : mSelectedDisplayName,
                        "Selected disc: " + (mSelectedDisplayName.isEmpty() ? uri.toString() : mSelectedDisplayName));
            }
        }
    }

    private String getDisplayName(Uri uri) {
        String displayName = null;
        if ("content".equalsIgnoreCase(uri.getScheme())) {
            try (Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    int nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (nameIndex >= 0) {
                        displayName = cursor.getString(nameIndex);
                    }
                }
            } catch (Exception ignored) {}
        }
        if (displayName == null || displayName.isEmpty()) {
            displayName = uri.getLastPathSegment();
            if (displayName != null && displayName.contains("/")) {
                displayName = displayName.substring(displayName.lastIndexOf('/') + 1);
            }
        }
        return displayName != null ? displayName : "";
    }

    private String getPathFromUri(Uri uri) {
        if ("file".equalsIgnoreCase(uri.getScheme())) {
            return uri.getPath();
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
            try {
                if (DocumentsContract.isDocumentUri(this, uri)) {
                    String docId = DocumentsContract.getDocumentId(uri);
                    if (docId != null) {
                        if (docId.startsWith("raw:")) {
                            return docId.substring(4);
                        }
                        if (docId.startsWith("primary:")) {
                            File f = new File(Environment.getExternalStorageDirectory(), docId.substring(8));
                            if (f.exists()) return f.getAbsolutePath();
                        }
                    }
                }
                String displayName = getDisplayName(uri);
                if (!displayName.isEmpty()) {
                    File downloadDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
                    File testFile = new File(downloadDir, displayName);
                    if (testFile.exists()) return testFile.getAbsolutePath();

                    File extDir = Environment.getExternalStorageDirectory();
                    File testFile2 = new File(extDir, displayName);
                    if (testFile2.exists()) return testFile2.getAbsolutePath();
                }
            } catch (Exception ignored) {}
        }
        return null;
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

        new Thread(() -> {
            try {
                File outputDir = new File(Environment.getExternalStorageDirectory(), "GalaxyData");
                if (!outputDir.exists()) outputDir.mkdirs();

                boolean includeMovies = chkIncludeMovies.isChecked();
                String testName = !mSelectedDisplayName.isEmpty() ? mSelectedDisplayName : mSelectedSourcePath;
                String lowerName = testName.toLowerCase();
                boolean isDiscImage = lowerName.endsWith(".rvz") || lowerName.endsWith(".wbfs") || lowerName.endsWith(".iso") || lowerName.endsWith(".gcm");

                File resolvedDiscFile = null;
                if (isDiscImage) {
                    if (new File(mSelectedSourcePath).exists()) {
                        resolvedDiscFile = new File(mSelectedSourcePath);
                    } else if (mSelectedSourceUri != null) {
                        appendLog("[Setup] Reading disc image: " + mSelectedDisplayName + "...");
                        mHandler.post(() -> txtProgressStatus.setText("Caching disc image for extraction..."));
                        String ext = lowerName.endsWith(".rvz") ? "rvz" : lowerName.endsWith(".wbfs") ? "wbfs" : "iso";
                        File tempDisc = new File(getFilesDir(), "temp_disc." + ext);
                        if (tempDisc.exists()) tempDisc.delete();
                        try (java.io.InputStream in = getContentResolver().openInputStream(mSelectedSourceUri);
                             java.io.FileOutputStream out = new java.io.FileOutputStream(tempDisc)) {
                            byte[] buf = new byte[1024 * 1024];
                            int read;
                            long total = 0;
                            while ((read = in.read(buf)) > 0) {
                                out.write(buf, 0, read);
                                total += read;
                                if (total % (50 * 1024 * 1024) == 0) {
                                    final long mb = total / (1024 * 1024);
                                    mHandler.post(() -> txtProgressStatus.setText("Buffered " + mb + " MB..."));
                                }
                            }
                        }
                        resolvedDiscFile = tempDisc;
                    }
                }

                File extractionDir = outputDir;
                File tempExtractionDir = null;

                // Step 1: If source is disc image, extract with native nodtool
                if (isDiscImage && resolvedDiscFile != null && resolvedDiscFile.exists()) {
                    tempExtractionDir = new File(getFilesDir(), "temp_extracted");
                    CookerEngine.deleteRecursive(tempExtractionDir);
                    tempExtractionDir.mkdirs();

                    final String discPathToExtract = resolvedDiscFile.getAbsolutePath();
                    mHandler.post(() -> txtProgressStatus.setText("Extracting disc partition..."));
                    boolean extractSuccess = CookerEngine.extractWithNodtool(
                        this, discPathToExtract, tempExtractionDir.getAbsolutePath(), includeMovies,
                        new CookerEngine.CookerCallback() {
                            @Override public void onLog(String line) { appendLog(line); }
                            @Override public void onProgress(int percent, String message) {
                                mHandler.post(() -> {
                                    if (message != null) txtProgressStatus.setText(message);
                                });
                            }
                            @Override public void onFinished(boolean success, String message) {}
                        }
                    );

                    // If we created a temporary cached disc file from a content URI, free it now
                    if (resolvedDiscFile.getAbsolutePath().startsWith(getFilesDir().getAbsolutePath())) {
                        resolvedDiscFile.delete();
                    }

                    if (!extractSuccess) {
                        mHandler.post(() -> onCookingCompleted(false, "Native disc extraction failed."));
                        return;
                    }
                    extractionDir = tempExtractionDir;
                }

                // Step 2: Prepare converter environment and python runtime
                mHandler.post(() -> txtProgressStatus.setText("Setting up converter engine..."));
                CookerEngine.prepareAssets(this, new CookerEngine.CookerCallback() {
                    @Override public void onLog(String line) { appendLog(line); }
                    @Override public void onProgress(int percent, String message) {}
                    @Override public void onFinished(boolean success, String message) {}
                });

                File filesDir = getFilesDir();
                File pythonHome = new File(filesDir, "pythonhome");
                File scriptsDir = new File(filesDir, "scripts");

                // Step 3: Run asset converter into GalaxyData
                mHandler.post(() -> txtProgressStatus.setText("Converting assets to little-endian ARM64..."));
                int ret = CookerEngine.runCooker(
                    pythonHome.getAbsolutePath(),
                    scriptsDir.getAbsolutePath(),
                    extractionDir.getAbsolutePath(),
                    outputDir.getAbsolutePath(),
                    includeMovies,
                    new CookerEngine.CookerCallback() {
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
                        @Override public void onFinished(boolean success, String message) {
                            mHandler.post(() -> onCookingCompleted(success, message));
                        }
                    }
                );

                if (ret != 0) {
                    mHandler.post(() -> onCookingCompleted(false, "Converter returned code: " + ret));
                } else {
                    if (tempExtractionDir != null && tempExtractionDir.exists()) {
                        appendLog("[Clean] Freeing temporary extraction cache...");
                        CookerEngine.deleteRecursive(tempExtractionDir);
                    }
                    mHandler.post(() -> onCookingCompleted(true, "Installation complete!"));
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
        btnSelectDisc.setEnabled(true);
        btnAutoDetect.setEnabled(true);

        if (success) {
            try {
                new File(Environment.getExternalStorageDirectory(), "GalaxyData/.installed_ok").createNewFile();
            } catch (Exception ignored) {}
            txtProgressStatus.setText(R.string.status_done);
            appendLog("[SUCCESS] " + (message != null ? message : "Conversion complete!"));
            btnLaunchGame.setVisibility(View.VISIBLE);
            btnLaunchGame.setText("Launch Game");
            Toast.makeText(this, "Game ready! Launching game now...", Toast.LENGTH_SHORT).show();
            // Automatically launch into game!
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
