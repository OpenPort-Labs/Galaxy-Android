package com.openport.galaxyandroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
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

import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class MainActivity extends Activity {
    private static final String TAG = "GalaxyAndroid";
    private static final String PREFS_NAME = "GalaxyPrefs";
    private static final int REQ_PICK_DISC = 1001;
    private static final int REQ_PICK_DRIVER = 1002;

    // Header & State
    private TextView txtHeaderTitle;
    private TextView txtHeaderSubtitle;
    private TextView txtGameStatusBadge;
    private TextView txtGameStatusDetail;

    // Disc Installer
    private TextView txtSourcePath;
    private CheckBox chkIncludeMovies;
    private Button btnSelectDisc;
    private Button btnAutoDetect;
    private Button btnStartCooking;
    private Button btnLaunchGame;
    private Button btnResetData;
    private Button btnRes480;
    private Button btnRes720;
    private Button btnRes1080;

    // GPU Driver Manager
    private TextView txtGpuHardwareInfo;
    private TextView txtDriverStatusBadge;
    private TextView txtDriverDetail;
    private Button btnInstallDriver;
    private Button btnResetDriver;

    // Console & Progress
    private ProgressBar progressBar;
    private TextView txtProgressStatus;
    private TextView txtLogOutput;
    private Button btnClearLog;

    private String mSelectedSourcePath = "";
    private Uri mSelectedSourceUri = null;
    private String mSelectedDisplayName = "";
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private boolean mIsCooking = false;
    private SharedPreferences mPrefs;

    public static boolean checkDataDir(File dataDir) {
        if (dataDir == null || !dataDir.exists()) return false;
        File sentinel = new File(dataDir, ".installed_ok");
        File fst = new File(dataDir, "sys/fst.bin");
        File files = new File(dataDir, "files");
        File coin = new File(dataDir, "files/ObjectData/Coin.arc");
        if (!fst.exists() || !fst.isFile() || !files.exists() || !files.isDirectory() || !coin.exists()) {
            return false;
        }
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

    public static File findGameDataFolder(android.content.Context context) {
        List<File> candidates = new ArrayList<>();
        candidates.add(new File("/sdcard/GalaxyData"));
        candidates.add(new File(Environment.getExternalStorageDirectory(), "GalaxyData"));
        if (context != null) {
            candidates.add(new File(context.getExternalFilesDir(null), "GalaxyData"));
            candidates.add(new File(context.getFilesDir(), "GalaxyData"));
        }
        File storageDir = new File("/storage");
        if (storageDir.exists() && storageDir.isDirectory()) {
            File[] mounts = storageDir.listFiles();
            if (mounts != null) {
                for (File m : mounts) {
                    if (m.isDirectory() && !m.getName().equals("emulated") && !m.getName().equals("self")) {
                        candidates.add(new File(m, "GalaxyData"));
                        candidates.add(new File(m, "Android/data/com.openport.galaxyandroid/files/GalaxyData"));
                    }
                }
            }
        }
        File mediaDir = new File("/mnt/media_rw");
        if (mediaDir.exists() && mediaDir.isDirectory()) {
            File[] mounts = mediaDir.listFiles();
            if (mounts != null) {
                for (File m : mounts) {
                    if (m.isDirectory()) {
                        candidates.add(new File(m, "GalaxyData"));
                        candidates.add(new File(m, "Android/data/com.openport.galaxyandroid/files/GalaxyData"));
                    }
                }
            }
        }
        for (File dir : candidates) {
            if (checkDataDir(dir)) return dir;
        }
        return null;
    }

    public static boolean isGameDataReady() {
        return findGameDataFolder(null) != null;
    }

    public static boolean isGameDataReady(android.content.Context context) {
        return findGameDataFolder(context) != null;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);

        setContentView(R.layout.activity_main);
        mPrefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);

        // Header & State
        txtHeaderTitle = findViewById(R.id.txtHeaderTitle);
        txtHeaderSubtitle = findViewById(R.id.txtHeaderSubtitle);
        txtGameStatusBadge = findViewById(R.id.txtGameStatusBadge);
        txtGameStatusDetail = findViewById(R.id.txtGameStatusDetail);

        // Disc Installer
        txtSourcePath = findViewById(R.id.txtSourcePath);
        chkIncludeMovies = findViewById(R.id.chkIncludeMovies);
        btnSelectDisc = findViewById(R.id.btnSelectDisc);
        btnAutoDetect = findViewById(R.id.btnAutoDetect);
        btnStartCooking = findViewById(R.id.btnStartCooking);
        btnLaunchGame = findViewById(R.id.btnLaunchGame);
        btnResetData = findViewById(R.id.btnResetData);

        // GPU Driver Manager
        txtGpuHardwareInfo = findViewById(R.id.txtGpuHardwareInfo);
        txtDriverStatusBadge = findViewById(R.id.txtDriverStatusBadge);
        txtDriverDetail = findViewById(R.id.txtDriverDetail);
        btnInstallDriver = findViewById(R.id.btnInstallDriver);
        btnResetDriver = findViewById(R.id.btnResetDriver);

        // Resolution & Performance Presets
        btnRes480 = findViewById(R.id.btnRes480);
        btnRes720 = findViewById(R.id.btnRes720);
        btnRes1080 = findViewById(R.id.btnRes1080);

        if (btnRes480 != null) btnRes480.setOnClickListener(v -> setResolutionPreset(0));
        if (btnRes720 != null) btnRes720.setOnClickListener(v -> setResolutionPreset(1));
        if (btnRes1080 != null) btnRes1080.setOnClickListener(v -> setResolutionPreset(2));
        refreshResolutionButtons();

        // Console & Progress
        progressBar = findViewById(R.id.progressBar);
        txtProgressStatus = findViewById(R.id.txtProgressStatus);
        txtLogOutput = findViewById(R.id.txtLogOutput);
        btnClearLog = findViewById(R.id.btnClearLog);

        txtLogOutput.setMovementMethod(new ScrollingMovementMethod());

        // Setup hardware info & driver status
        setupHardwareAndDriverInfo();

        checkStoragePermissions();
        autoDetectSource();
        refreshGameStatus();

        // Listeners
        btnSelectDisc.setOnClickListener(v -> openFilePicker());
        btnAutoDetect.setOnClickListener(v -> autoDetectSource());
        btnInstallDriver.setOnClickListener(v -> openDriverPicker());
        btnResetDriver.setOnClickListener(v -> resetToSystemDriver());

        if (btnClearLog != null) {
            btnClearLog.setOnClickListener(v -> txtLogOutput.setText(""));
        }

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
                .setMessage("Are you sure you want to delete /sdcard/GalaxyData? This removes partial or broken installation files so you can reinstall cleanly.")
                .setPositiveButton("Delete and Reset", (dialog, which) -> {
                    File dataDir = new File("/sdcard/GalaxyData");
                    deleteRecursive(dataDir);
                    refreshGameStatus();
                    appendLog("[Reset] Cleared game directory: " + dataDir.getAbsolutePath());
                    Toast.makeText(this, "Game data reset successfully.", Toast.LENGTH_SHORT).show();
                })
                .setNegativeButton("Cancel", null)
                .show();
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshGameStatus();
        refreshDriverStatus();
        refreshResolutionButtons();
    }

    private void refreshGameStatus() {
        File foundDir = findGameDataFolder(this);
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

    private int getCurrentResolutionPreset() {
        return mPrefs.getInt("render_scale", 1); // Default to 720p Balanced
    }

    private void setResolutionPreset(int scale) {
        mPrefs.edit().putInt("render_scale", scale).apply();

        // Update touch_settings.ini in internal storage
        File nandDir = new File(getFilesDir(), "nand");
        nandDir.mkdirs();
        File iniFile = new File(nandDir, "touch_settings.ini");
        updateIniRenderScale(iniFile, scale);

        refreshResolutionButtons();
        String label = (scale == 0) ? "480p (Native Wii / Max Performance)" :
                       (scale == 1) ? "720p (Balanced HD)" : "1080p (Full HD)";
        appendLog("[Preset] Resolution set to: " + label);
        Toast.makeText(this, "Preset: " + label, Toast.LENGTH_SHORT).show();
    }

    private void refreshResolutionButtons() {
        int current = getCurrentResolutionPreset();
        if (btnRes480 != null) {
            btnRes480.setBackgroundResource(current == 0 ? R.drawable.btn_primary : R.drawable.btn_secondary);
            btnRes480.setTextColor(current == 0 ? 0xFFFFFFFF : getColor(R.color.text_secondary));
        }
        if (btnRes720 != null) {
            btnRes720.setBackgroundResource(current == 1 ? R.drawable.btn_primary : R.drawable.btn_secondary);
            btnRes720.setTextColor(current == 1 ? 0xFFFFFFFF : getColor(R.color.text_secondary));
        }
        if (btnRes1080 != null) {
            btnRes1080.setBackgroundResource(current == 2 ? R.drawable.btn_primary : R.drawable.btn_secondary);
            btnRes1080.setTextColor(current == 2 ? 0xFFFFFFFF : getColor(R.color.text_secondary));
        }
    }

    private void updateIniRenderScale(File iniFile, int scale) {
        try {
            StringBuilder sb = new StringBuilder();
            boolean found = false;
            if (iniFile.exists()) {
                try (java.io.BufferedReader reader = new java.io.BufferedReader(new java.io.FileReader(iniFile))) {
                    String line;
                    while ((line = reader.readLine()) != null) {
                        if (line.startsWith("render_scale=")) {
                            sb.append("render_scale=").append(scale).append("\n");
                            found = true;
                        } else {
                            sb.append(line).append("\n");
                        }
                    }
                }
            }
            if (!found) {
                sb.append("render_scale=").append(scale).append("\n");
            }
            try (FileOutputStream fos = new FileOutputStream(iniFile)) {
                fos.write(sb.toString().getBytes(StandardCharsets.UTF_8));
            }
        } catch (Exception ignored) {}
    }

    private void setupHardwareAndDriverInfo() {
        String hw = Build.HARDWARE != null ? Build.HARDWARE : "unknown";
        String board = Build.BOARD != null ? Build.BOARD : "";
        txtGpuHardwareInfo.setText("Hardware: " + hw + (!board.isEmpty() ? " (" + board + ")" : ""));
        refreshDriverStatus();
    }

    private void refreshDriverStatus() {
        File driverDir = new File(getFilesDir(), "custom_driver");
        File activeMarker = new File(driverDir, "active_driver.txt");

        boolean customEnabled = mPrefs.getBoolean("custom_driver_enabled", false);
        if (customEnabled && activeMarker.exists()) {
            String driverName = mPrefs.getString("custom_driver_name", "Custom Vulkan Driver");
            String driverPath = mPrefs.getString("custom_driver_path", "");

            txtDriverStatusBadge.setText("CUSTOM DRIVER");
            txtDriverStatusBadge.setBackgroundResource(R.drawable.badge_bg_green);
            txtDriverStatusBadge.setTextColor(getColor(R.color.success_green));

            String desc = mPrefs.getString("custom_driver_desc", "");
            String display = "Active: " + driverName;
            if (!desc.isEmpty()) display += "\n" + desc;
            txtDriverDetail.setText(display);
        } else {
            txtDriverStatusBadge.setText("SYSTEM DRIVER");
            txtDriverStatusBadge.setBackgroundResource(R.drawable.badge_bg_blue);
            txtDriverStatusBadge.setTextColor(getColor(R.color.primary_accent));
            txtDriverDetail.setText("Active: System Default Vulkan Driver (OEM)");
        }
    }

    private void openDriverPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQ_PICK_DRIVER);
    }

    private void resetToSystemDriver() {
        File driverDir = new File(getFilesDir(), "custom_driver");
        File activeMarker = new File(driverDir, "active_driver.txt");
        if (activeMarker.exists()) activeMarker.delete();

        File sdcardMarker = new File("/sdcard/GalaxyData/custom_driver/active_driver.txt");
        if (sdcardMarker.exists()) sdcardMarker.delete();

        mPrefs.edit().putBoolean("custom_driver_enabled", false).apply();
        refreshDriverStatus();
        appendLog("[Driver] Reset graphics driver to System Default.");
        Toast.makeText(this, "Reverted to System Default Driver.", Toast.LENGTH_SHORT).show();
    }

    private void installCustomDriverPackage(Uri uri) {
        appendLog("[Driver] Reading custom graphics driver package...");
        txtProgressStatus.setText("Extracting graphics driver package...");

        new Thread(() -> {
            try {
                File driverDir = new File(getFilesDir(), "custom_driver");
                deleteRecursive(driverDir);
                driverDir.mkdirs();

                String driverName = "Custom Vulkan Driver";
                String driverDesc = "";
                String driverVersion = "";
                String targetLibraryName = "";

                // Extract Zip contents
                try (InputStream is = getContentResolver().openInputStream(uri);
                     BufferedInputStream bis = new BufferedInputStream(is);
                     ZipInputStream zis = new ZipInputStream(bis)) {

                    ZipEntry entry;
                    byte[] buffer = new byte[64 * 1024];
                    while ((entry = zis.getNextEntry()) != null) {
                        String name = entry.getName();
                        if (name.contains("..")) continue;

                        File outFile = new File(driverDir, name);
                        if (entry.isDirectory()) {
                            outFile.mkdirs();
                        } else {
                            File parent = outFile.getParentFile();
                            if (parent != null && !parent.exists()) parent.mkdirs();

                            try (FileOutputStream fos = new FileOutputStream(outFile)) {
                                int count;
                                while ((count = zis.read(buffer)) != -1) {
                                    fos.write(buffer, 0, count);
                                }
                            }

                            // Inspect metadata JSON if found
                            if (name.equalsIgnoreCase("meta.json") || name.equalsIgnoreCase("driver.json") || name.endsWith("/meta.json")) {
                                try (FileInputStream metaFis = new FileInputStream(outFile);
                                     InputStreamReader reader = new InputStreamReader(metaFis, StandardCharsets.UTF_8)) {
                                    StringBuilder sb = new StringBuilder();
                                    char[] cbuf = new char[1024];
                                    int read;
                                    while ((read = reader.read(cbuf)) != -1) {
                                        sb.append(cbuf, 0, read);
                                    }
                                    JSONObject json = new JSONObject(sb.toString());
                                    if (json.has("name")) driverName = json.getString("name");
                                    if (json.has("description")) driverDesc = json.getString("description");
                                    if (json.has("packageVersion")) driverVersion = json.getString("packageVersion");
                                    if (json.has("libraryName")) targetLibraryName = json.getString("libraryName");
                                } catch (Exception ignored) {}
                            }
                        }
                        zis.closeEntry();
                    }
                }

                // Locate the driver .so library
                File primarySo = null;
                if (!targetLibraryName.isEmpty()) {
                    File candidate = new File(driverDir, targetLibraryName);
                    if (candidate.exists()) primarySo = candidate;
                }

                if (primarySo == null) {
                    primarySo = findFirstSharedLib(driverDir);
                }

                if (primarySo == null) {
                    mHandler.post(() -> {
                        appendLog("[Driver] Error: No .so shared library found in the driver package.");
                        Toast.makeText(this, "No .so library found in driver zip.", Toast.LENGTH_LONG).show();
                        txtProgressStatus.setText("Driver installation failed.");
                    });
                    return;
                }

                // Set file permissions (readable, executable, and read-only for Android 14)
                setDriverPermissionsRecursive(driverDir);

                // Write active_driver.txt
                File activeMarker = new File(driverDir, "active_driver.txt");
                try (FileOutputStream fos = new FileOutputStream(activeMarker)) {
                    String markerContent = primarySo.getAbsolutePath() + "\n" + driverName + "\n1\n";
                    fos.write(markerContent.getBytes(StandardCharsets.UTF_8));
                }

                // Mirror to /sdcard/GalaxyData/custom_driver/
                try {
                    File sdcardDriverDir = new File("/sdcard/GalaxyData/custom_driver");
                    sdcardDriverDir.mkdirs();
                    File sdcardMarker = new File(sdcardDriverDir, "active_driver.txt");
                    try (FileOutputStream fos = new FileOutputStream(sdcardMarker)) {
                        String markerContent = primarySo.getAbsolutePath() + "\n" + driverName + "\n1\n";
                        fos.write(markerContent.getBytes(StandardCharsets.UTF_8));
                    }
                } catch (Exception ignored) {}

                final String finalName = driverName + (!driverVersion.isEmpty() ? " (v" + driverVersion + ")" : "");
                final String finalDesc = driverDesc;
                final String finalPath = primarySo.getAbsolutePath();

                mPrefs.edit()
                    .putBoolean("custom_driver_enabled", true)
                    .putString("custom_driver_name", finalName)
                    .putString("custom_driver_desc", finalDesc)
                    .putString("custom_driver_path", finalPath)
                    .apply();

                mHandler.post(() -> {
                    refreshDriverStatus();
                    appendLog("[Driver] Successfully installed: " + finalName);
                    appendLog("[Driver] Driver binary: " + finalPath);
                    txtProgressStatus.setText("Custom graphics driver installed and active!");
                    Toast.makeText(this, "Installed: " + finalName, Toast.LENGTH_SHORT).show();
                });

            } catch (Exception e) {
                Log.e(TAG, "Driver installation failed", e);
                mHandler.post(() -> {
                    appendLog("[Driver] Failed to install driver: " + e.getMessage());
                    txtProgressStatus.setText("Driver installation error.");
                    Toast.makeText(this, "Failed to install driver: " + e.getMessage(), Toast.LENGTH_LONG).show();
                });
            }
        }).start();
    }

    private File findFirstSharedLib(File dir) {
        if (dir == null || !dir.exists()) return null;
        File[] files = dir.listFiles();
        if (files == null) return null;

        // First pass: look for files named vulkan.*.so
        for (File f : files) {
            if (f.isFile() && f.getName().toLowerCase().contains("vulkan") && f.getName().endsWith(".so")) {
                return f;
            }
        }
        // Second pass: any .so in current directory
        for (File f : files) {
            if (f.isFile() && f.getName().endsWith(".so")) {
                return f;
            }
        }
        // Recurse subdirectories
        for (File f : files) {
            if (f.isDirectory()) {
                File sub = findFirstSharedLib(f);
                if (sub != null) return sub;
            }
        }
        return null;
    }

    private void setDriverPermissionsRecursive(File fileOrDir) {
        if (fileOrDir == null || !fileOrDir.exists()) return;
        fileOrDir.setReadable(true, false);
        fileOrDir.setExecutable(true, false);
        if (fileOrDir.isDirectory()) {
            File[] children = fileOrDir.listFiles();
            if (children != null) {
                for (File child : children) {
                    setDriverPermissionsRecursive(child);
                }
            }
        } else if (fileOrDir.getName().endsWith(".so")) {
            if (Build.VERSION.SDK_INT >= 29) {
                fileOrDir.setReadOnly();
            }
        }
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
                appendLog("[Scan] Game data is ready in " + galaxyDataDir.getAbsolutePath());
                mHandler.post(this::refreshGameStatus);
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

        // 3. Scan external MicroSD cards (/storage/*)
        File storageDir = new File("/storage");
        if (storageDir.exists() && storageDir.isDirectory()) {
            File[] mounts = storageDir.listFiles();
            if (mounts != null) {
                for (File m : mounts) {
                    if (m.isDirectory() && !m.getName().equals("emulated") && !m.getName().equals("self")) {
                        appendLog("[Storage] MicroSD Card detected: " + m.getAbsolutePath());
                        File[] sFiles = m.listFiles();
                        if (sFiles != null) {
                            for (File f : sFiles) {
                                String name = f.getName().toLowerCase();
                                if (name.endsWith(".rvz") || name.endsWith(".wbfs") || name.endsWith(".iso")) {
                                    selectSource(f.getAbsolutePath(), "Detected disc on MicroSD: " + f.getName());
                                    return;
                                }
                            }
                        }
                    }
                }
            }
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
        } else if (requestCode == REQ_PICK_DRIVER) {
            installCustomDriverPackage(uri);
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
                        int colon = docId.indexOf(':');
                        if (colon >= 0) {
                            String type = docId.substring(0, colon);
                            String rel = docId.substring(colon + 1);
                            if ("primary".equalsIgnoreCase(type)) {
                                File f = new File(Environment.getExternalStorageDirectory(), rel);
                                if (f.exists()) return f.getAbsolutePath();
                            } else {
                                File f = new File("/storage/" + type, rel);
                                if (f.exists()) return f.getAbsolutePath();
                                File mnt = new File("/mnt/media_rw/" + type, rel);
                                if (mnt.exists()) return mnt.getAbsolutePath();
                            }
                        }
                    }
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

        final boolean includeMovies = chkIncludeMovies.isChecked();

        new Thread(() -> {
            try {
                File outputDir = new File(Environment.getExternalStorageDirectory(), "GalaxyData");
                if (!outputDir.exists() && !outputDir.mkdirs()) {
                    appendLog("[Storage] Primary storage not directly writable; using app-specific external storage.");
                    File appExt = getExternalFilesDir(null);
                    if (appExt != null) {
                        outputDir = new File(appExt, "GalaxyData");
                        outputDir.mkdirs();
                    } else {
                        outputDir = new File(getFilesDir(), "GalaxyData");
                        outputDir.mkdirs();
                    }
                }
                appendLog("[Storage] Target installation directory: " + outputDir.getAbsolutePath());

                String testName = !mSelectedDisplayName.isEmpty() ? mSelectedDisplayName : mSelectedSourcePath;
                String lowerName = testName.toLowerCase();
                boolean isDiscImage = lowerName.endsWith(".rvz") || lowerName.endsWith(".wbfs") || lowerName.endsWith(".iso") || lowerName.endsWith(".gcm");

                File resolvedDiscFile = null;
                if (isDiscImage) {
                    if (new File(mSelectedSourcePath).exists()) {
                        resolvedDiscFile = new File(mSelectedSourcePath);
                    } else if (mSelectedSourceUri != null) {
                        appendLog("[Setup] Reading disc image: " + mSelectedDisplayName);
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
                    CookerEngine.CookerCallback extractCallback = new CookerEngine.CookerCallback() {
                        @Override public void onLog(String line) { appendLog(line); }
                        @Override public void onProgress(int percent, String message) {
                            mHandler.post(() -> {
                                if (message != null) txtProgressStatus.setText(message);
                            });
                        }
                        @Override public void onFinished(boolean success, String message) {}
                    };

                    boolean extractSuccess = false;
                    try {
                        appendLog("[Installer] Attempting fast in-process native disc extraction...");
                        int res = CookerEngine.extractDisc(discPathToExtract, tempExtractionDir.getAbsolutePath(), includeMovies, extractCallback);
                        extractSuccess = (res == 0);
                    } catch (Throwable t) {
                        appendLog("[Installer] In-process extraction exception: " + t.getMessage());
                        extractSuccess = false;
                    }

                    if (!extractSuccess) {
                        appendLog("[Installer] In-process extraction returned non-zero, trying fallback extractor...");
                        extractSuccess = CookerEngine.extractWithNodtool(
                            this, discPathToExtract, tempExtractionDir.getAbsolutePath(), includeMovies, extractCallback
                        );
                    }

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
