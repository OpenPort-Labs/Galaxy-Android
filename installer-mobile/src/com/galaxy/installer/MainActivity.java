package com.galaxy.installer;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
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
    private static final String TAG = "GalaxyInstaller";
    private static final int REQ_PICK_DISC = 1001;
    private static final int REQ_PICK_FOLDER = 1002;

    private TextView txtSourcePath;
    private CheckBox chkIncludeMovies;
    private Button btnSelectDisc;
    private Button btnSelectFolder;
    private Button btnAutoDetect;
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
        btnSelectDisc = findViewById(R.id.btnSelectDisc);
        btnSelectFolder = findViewById(R.id.btnSelectFolder);
        btnAutoDetect = findViewById(R.id.btnAutoDetect);
        btnStartCooking = findViewById(R.id.btnStartCooking);
        btnLaunchGame = findViewById(R.id.btnLaunchGame);
        progressBar = findViewById(R.id.progressBar);
        txtProgressStatus = findViewById(R.id.txtProgressStatus);
        txtLogOutput = findViewById(R.id.txtLogOutput);

        txtLogOutput.setMovementMethod(new ScrollingMovementMethod());

        checkStoragePermissions();
        autoDetectSource();

        btnSelectDisc.setOnClickListener(v -> openFilePicker());
        btnSelectFolder.setOnClickListener(v -> openFolderPicker());
        btnAutoDetect.setOnClickListener(v -> autoDetectSource());

        btnStartCooking.setOnClickListener(v -> {
            if (mSelectedSourcePath.isEmpty()) {
                Toast.makeText(this, "Please select a game disc or extracted folder first", Toast.LENGTH_SHORT).show();
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
        appendLog("[Scan] Checking for GalaxyData folder and game dumps...");

        // 1. Check if /sdcard/GalaxyData already exists
        File galaxyDataDir = new File(Environment.getExternalStorageDirectory(), "GalaxyData");
        if (galaxyDataDir.exists()) {
            File fstCheck = new File(galaxyDataDir, "sys/fst.bin");
            File filesCheck = new File(galaxyDataDir, "files");
            if (fstCheck.exists() && filesCheck.exists()) {
                appendLog("[Scan] Found ready game data in " + galaxyDataDir.getAbsolutePath() + "!");
                mHandler.post(() -> {
                    btnLaunchGame.setVisibility(View.VISIBLE);
                    txtProgressStatus.setText("Cooked game data ready in GalaxyData!");
                });
            }
            // Check for game dumps inside GalaxyData
            File[] gFiles = galaxyDataDir.listFiles();
            if (gFiles != null) {
                for (File f : gFiles) {
                    String name = f.getName().toLowerCase();
                    if (name.endsWith(".rvz") || name.endsWith(".wbfs") || name.endsWith(".iso")) {
                        selectSource(f.getAbsolutePath(), "Auto-detected disc in GalaxyData: " + f.getName());
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
                // Priority 1: .rvz, .wbfs, or .iso files
                for (File f : files) {
                    String name = f.getName().toLowerCase();
                    if (name.endsWith(".rvz") || name.endsWith(".wbfs") || name.endsWith(".iso")) {
                        selectSource(f.getAbsolutePath(), "Auto-detected disc: " + f.getName());
                        return;
                    }
                }
                // Priority 2: .zip files
                for (File f : files) {
                    String name = f.getName().toLowerCase();
                    if (name.contains("galaxy") && name.endsWith(".zip")) {
                        selectSource(f.getAbsolutePath(), "Auto-detected archive: " + f.getName());
                        return;
                    }
                }
                // Priority 3: extracted directories
                for (File f : files) {
                    if (f.isDirectory()) {
                        String name = f.getName().toLowerCase();
                        if (name.equals("extracted") || name.equals("data") || name.contains("galaxy")) {
                            File testFiles = new File(f, "files");
                            File testData = new File(f, "DATA");
                            if (testFiles.exists() || testData.exists()) {
                                selectSource(f.getAbsolutePath(), "Auto-detected extracted folder: " + f.getName());
                                return;
                            }
                        }
                    }
                }
            }
        }

        // Check fallback root paths
        String[] fallbackCandidates = {
            "/sdcard/GalaxyData/Super Mario Galaxy.rvz",
            "/sdcard/GalaxyData/Super Mario Galaxy.wbfs",
            "/sdcard/GalaxyData/Super Mario Galaxy.iso",
            "/sdcard/Download/Super Mario Galaxy.rvz",
            "/sdcard/Download/Super Mario Galaxy.wbfs",
            "/sdcard/Download/Super Mario Galaxy.iso",
            "/sdcard/Download/extracted",
            "/sdcard/Download/DATA",
            "/sdcard/GalaxyQuest/extracted",
            "/sdcard/extracted"
        };
        for (String c : fallbackCandidates) {
            File f = new File(c);
            if (f.exists()) {
                selectSource(c, "Auto-detected: " + c);
                return;
            }
        }

        appendLog("[Scan] No disc dump found automatically. Please select your file manually.");
    }

    private void selectSource(String path, String label) {
        mSelectedSourcePath = path;
        txtSourcePath.setText(label);
        appendLog("[Source] " + label + " (" + path + ")");
    }

    private void openFilePicker() {
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.setType("*/*");
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        try {
            startActivityForResult(Intent.createChooser(intent, "Select Super Mario Galaxy (.wbfs / .iso / .zip)"), REQ_PICK_DISC);
        } catch (Exception e) {
            Toast.makeText(this, "Failed to open file picker: " + e.getMessage(), Toast.LENGTH_SHORT).show();
        }
    }

    private void openFolderPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(intent, REQ_PICK_FOLDER);
        } catch (Exception e) {
            Toast.makeText(this, "Failed to open folder picker: " + e.getMessage(), Toast.LENGTH_SHORT).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null) return;

        Uri uri = data.getData();
        if (uri == null) return;

        String resolvedPath = resolvePathFromUri(uri);
        if (resolvedPath != null && !resolvedPath.isEmpty()) {
            selectSource(resolvedPath, "Selected: " + new File(resolvedPath).getName());
        } else {
            selectSource(uri.toString(), "Selected: " + uri.getLastPathSegment());
        }
    }

    private String resolvePathFromUri(Uri uri) {
        if ("file".equalsIgnoreCase(uri.getScheme())) {
            return uri.getPath();
        }

        // Try to resolve standard storage tree/document path
        String path = uri.getPath();
        if (path != null && path.contains(":")) {
            String[] parts = path.split(":");
            if (parts.length > 1) {
                String subPath = parts[1];
                File direct = new File(Environment.getExternalStorageDirectory(), subPath);
                if (direct.exists()) return direct.getAbsolutePath();
                File downloadDirect = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), subPath);
                if (downloadDirect.exists()) return downloadDirect.getAbsolutePath();
                return "/sdcard/" + subPath;
            }
        }

        // Try getting display name and matching in Download folder
        try (Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (nameIndex != -1) {
                    String displayName = cursor.getString(nameIndex);
                    File dl = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), displayName);
                    if (dl.exists()) {
                        return dl.getAbsolutePath();
                    }
                }
            }
        } catch (Exception ignored) {}

        return uri.getPath();
    }

    private void startCookingProcess() {
        if (mIsCooking) return;
        mIsCooking = true;

        btnStartCooking.setEnabled(false);
        btnSelectDisc.setEnabled(false);
        btnSelectFolder.setEnabled(false);
        btnAutoDetect.setEnabled(false);
        progressBar.setVisibility(View.VISIBLE);
        txtProgressStatus.setText(R.string.status_cooking);

        final String sourcePath = mSelectedSourcePath;
        File galaxyDataDir = new File(Environment.getExternalStorageDirectory(), "GalaxyData");
        if (!galaxyDataDir.exists()) {
            boolean created = galaxyDataDir.mkdirs();
            appendLog("[Directory] Created GalaxyData directory: " + galaxyDataDir.getAbsolutePath());
        } else {
            appendLog("[Directory] Using existing GalaxyData directory: " + galaxyDataDir.getAbsolutePath());
        }
        final String outputDir = galaxyDataDir.getAbsolutePath();
        final boolean withMovies = chkIncludeMovies.isChecked();

        appendLog("==============================================");
        appendLog("[Pipeline] Starting PC-less on-device preparation");
        appendLog("[Pipeline] Source: " + sourcePath);
        appendLog("[Pipeline] Target: " + outputDir);
        appendLog("==============================================");

        new Thread(() -> {
            File tempExtractionDir = null;
            try {
                // Step 1: Ensure Python runtime and cooker scripts are unpacked
                CookerEngine.prepareAssets(MainActivity.this, new CookerEngine.CookerCallback() {
                    @Override public void onLog(String line) { appendLog(line); }
                    @Override public void onProgress(int percent, String message) {}
                    @Override public void onFinished(boolean success, String message) {}
                });

                String pythonHome = new File(getFilesDir(), "pythonhome").getAbsolutePath();
                String scriptsDir = new File(getFilesDir(), "scripts").getAbsolutePath();

                String cookInputPath = sourcePath;
                File sourceFile = new File(sourcePath);

                // Step 2: Handle Wii Disc Image (.rvz / .wbfs / .iso / .wia / .ciso) extraction
                String lower = sourcePath.toLowerCase();
                boolean isDisc = sourceFile.isFile() && (
                    lower.endsWith(".rvz") || lower.endsWith(".wbfs") || lower.endsWith(".iso") ||
                    lower.endsWith(".wia") || lower.endsWith(".ciso") || CookerEngine.isWiiDisc(sourcePath)
                );

                if (isDisc) {
                    appendLog("[Stage 1/2] Native Wii Disc detected (" + sourceFile.getName() + ").");
                    appendLog("[Stage 1/2] Decrypting and extracting game data on-device...");
                    mHandler.post(() -> txtProgressStatus.setText("Extracting Wii disc image..."));

                    tempExtractionDir = new File(getFilesDir(), "extracted_cache");
                    File fstCheck = new File(tempExtractionDir, "sys/fst.bin");
                    if (!fstCheck.exists()) {
                        fstCheck = new File(tempExtractionDir, "DATA/sys/fst.bin");
                    }
                    boolean extractSuccess = true;
                    if (fstCheck.exists() && fstCheck.length() > 0) {
                        appendLog("[Stage 1/2] Valid disc cache found from previous extraction. Skipping re-extract.");
                    } else {
                        CookerEngine.deleteRecursive(tempExtractionDir);
                        tempExtractionDir.mkdirs();

                        extractSuccess = CookerEngine.extractWithNodtool(
                            MainActivity.this,
                            sourcePath,
                            tempExtractionDir.getAbsolutePath(),
                            withMovies,
                            new CookerEngine.CookerCallback() {
                                @Override public void onLog(String line) { appendLog(line); }
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
                                @Override public void onFinished(boolean success, String message) {}
                            }
                        );

                        if (!extractSuccess && (lower.endsWith(".wbfs") || lower.endsWith(".iso"))) {
                            appendLog("[Fallback] Retrying with secondary native C++ decryptor...");
                            int extractRet = CookerEngine.extractDisc(
                                sourcePath,
                                tempExtractionDir.getAbsolutePath(),
                                withMovies,
                                new CookerEngine.CookerCallback() {
                                    @Override public void onLog(String line) { appendLog(line); }
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
                                    @Override public void onFinished(boolean success, String message) {}
                                }
                            );
                            extractSuccess = (extractRet == 0);
                        }
                    }

                    if (!extractSuccess) {
                        throw new RuntimeException("Disc extraction failed.");
                    }

                    appendLog("[Stage 1/2] Disc extraction complete.");
                    cookInputPath = tempExtractionDir.getAbsolutePath();
                } else if (sourceFile.isFile() && sourcePath.toLowerCase().endsWith(".zip")) {
                    appendLog("[Stage 1/2] ZIP Archive detected. Unpacking on-device...");
                    mHandler.post(() -> txtProgressStatus.setText("Unpacking ZIP archive..."));

                    tempExtractionDir = new File(getFilesDir(), "extracted_cache");
                    CookerEngine.deleteRecursive(tempExtractionDir);
                    tempExtractionDir.mkdirs();

                    boolean unzipped = CookerEngine.extractZip(sourceFile, tempExtractionDir, new CookerEngine.CookerCallback() {
                        @Override public void onLog(String line) { appendLog(line); }
                        @Override public void onProgress(int percent, String message) {
                            mHandler.post(() -> {
                                if (message != null) txtProgressStatus.setText(message);
                            });
                        }
                        @Override public void onFinished(boolean success, String message) {}
                    });

                    if (!unzipped) {
                        throw new RuntimeException("Failed to unpack ZIP archive.");
                    }
                    cookInputPath = tempExtractionDir.getAbsolutePath();
                }

                // Step 3: Run the asset converter (cook.py)
                appendLog("[Stage 2/2] Running asset cooker engine (textures, models, audio)...");
                mHandler.post(() -> {
                    progressBar.setIndeterminate(true);
                    txtProgressStatus.setText("Cooking game assets...");
                });

                File outFolder = new File(outputDir);
                if (!outFolder.exists()) {
                    outFolder.mkdirs();
                }

                int ret = CookerEngine.runCooker(
                    pythonHome, scriptsDir, cookInputPath, outputDir, withMovies,
                    new CookerEngine.CookerCallback() {
                        @Override public void onLog(String line) { appendLog(line); }
                        @Override
                        public void onProgress(int percent, String message) {
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
                    mHandler.post(() -> onCookingCompleted(false, "Cooker returned error code: " + ret));
                } else {
                    // Clean up temporary cache on success to free device storage
                    if (tempExtractionDir != null && tempExtractionDir.exists()) {
                        appendLog("[Clean] Freeing temporary cache storage...");
                        CookerEngine.deleteRecursive(tempExtractionDir);
                    }
                    mHandler.post(() -> onCookingCompleted(true, "All game assets cooked successfully!"));
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
        btnSelectFolder.setEnabled(true);
        btnAutoDetect.setEnabled(true);

        if (success) {
            txtProgressStatus.setText(R.string.status_done);
            appendLog("[SUCCESS] " + (message != null ? message : "Conversion complete!"));
            btnLaunchGame.setVisibility(View.VISIBLE);
            Toast.makeText(this, "Game data ready! You can now launch GalaxyMobile.", Toast.LENGTH_LONG).show();
        } else {
            txtProgressStatus.setText(R.string.status_error);
            appendLog("[FAILURE] " + (message != null ? message : "Error during conversion."));
            Toast.makeText(this, "Preparation failed. Check terminal log.", Toast.LENGTH_LONG).show();
        }
    }

    private void launchGalaxyMobile() {
        Intent launchIntent = getPackageManager().getLaunchIntentForPackage("com.galaxy.mobile.plus");
        if (launchIntent == null) {
            launchIntent = getPackageManager().getLaunchIntentForPackage("com.galaxy.mobile");
        }
        if (launchIntent != null) {
            startActivity(launchIntent);
        } else {
            Toast.makeText(this, "Neither GalaxyMobile nor GalaxyMobile+ is installed yet!", Toast.LENGTH_LONG).show();
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
