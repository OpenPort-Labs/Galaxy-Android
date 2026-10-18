package com.openport.galaxyandroid;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.util.Log;
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
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public final class CustomDriverInstaller {
    private static final String TAG = "CustomDriverInstaller";

    public interface LogCallback {
        void log(String message);
    }

    private CustomDriverInstaller() {}

    public static String getHardwareInfo() {
        String hw = Build.HARDWARE != null ? Build.HARDWARE : "unknown";
        String board = Build.BOARD != null ? Build.BOARD : "";
        return "Hardware: " + hw + (!board.isEmpty() ? " (" + board + ")" : "");
    }

    public static void refreshDriverStatus(Context context, SharedPreferences prefs,
                                          TextView txtBadge, TextView txtDetail) {
        File driverDir = new File(context.getFilesDir(), "custom_driver");
        File activeMarker = new File(driverDir, "active_driver.txt");

        boolean customEnabled = prefs.getBoolean("custom_driver_enabled", false);
        if (customEnabled && activeMarker.exists()) {
            String driverName = prefs.getString("custom_driver_name", "Custom Vulkan Driver");

            txtBadge.setText("CUSTOM DRIVER");
            txtBadge.setBackgroundResource(R.drawable.badge_bg_green);
            txtBadge.setTextColor(context.getColor(R.color.success_green));

            String desc = prefs.getString("custom_driver_desc", "");
            String display = "Active: " + driverName;
            if (!desc.isEmpty()) display += "\n" + desc;
            txtDetail.setText(display);
        } else {
            txtBadge.setText("SYSTEM DRIVER");
            txtBadge.setBackgroundResource(R.drawable.badge_bg_blue);
            txtBadge.setTextColor(context.getColor(R.color.primary_accent));
            txtDetail.setText("Active: System Default Vulkan Driver (OEM)");
        }
    }

    public static void resetToSystemDriver(Context context, SharedPreferences prefs, LogCallback logger) {
        File driverDir = new File(context.getFilesDir(), "custom_driver");
        File activeMarker = new File(driverDir, "active_driver.txt");
        if (activeMarker.exists()) activeMarker.delete();

        File sdcardMarker = new File("/sdcard/GalaxyData/custom_driver/active_driver.txt");
        if (sdcardMarker.exists()) sdcardMarker.delete();

        prefs.edit().putBoolean("custom_driver_enabled", false).apply();
        if (logger != null) logger.log("[Driver] Reset graphics driver to System Default.");
        Toast.makeText(context, "Reverted to System Default Driver.", Toast.LENGTH_SHORT).show();
    }

    public static void installDriverPackage(Context context, Uri uri, SharedPreferences prefs,
                                            Handler handler, LogCallback logger, Runnable onCompleted) {
        if (logger != null) logger.log("[Driver] Reading custom graphics driver package...");

        new Thread(() -> {
            try {
                File driverDir = new File(context.getFilesDir(), "custom_driver");
                StorageUtils.deleteRecursive(driverDir);
                driverDir.mkdirs();

                String driverName = "Custom Vulkan Driver";
                String driverDesc = "";
                String driverVersion = "";
                String targetLibraryName = "";

                try (InputStream is = context.getContentResolver().openInputStream(uri);
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

                            if (name.equalsIgnoreCase("meta.json") || name.equalsIgnoreCase("driver.json")
                                    || name.endsWith("/meta.json")) {
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

                File primarySo = null;
                if (!targetLibraryName.isEmpty()) {
                    File candidate = new File(driverDir, targetLibraryName);
                    if (candidate.exists()) primarySo = candidate;
                }
                if (primarySo == null) {
                    primarySo = findFirstSharedLib(driverDir);
                }

                if (primarySo == null) {
                    handler.post(() -> {
                        if (logger != null) logger.log("[Driver] Error: No .so shared library found in the driver package.");
                        Toast.makeText(context, "No .so library found in driver zip.", Toast.LENGTH_LONG).show();
                    });
                    return;
                }

                setDriverPermissionsRecursive(driverDir);

                File activeMarker = new File(driverDir, "active_driver.txt");
                try (FileOutputStream fos = new FileOutputStream(activeMarker)) {
                    String markerContent = primarySo.getAbsolutePath() + "\n" + driverName + "\n1\n";
                    fos.write(markerContent.getBytes(StandardCharsets.UTF_8));
                }

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

                prefs.edit()
                    .putBoolean("custom_driver_enabled", true)
                    .putString("custom_driver_name", finalName)
                    .putString("custom_driver_desc", finalDesc)
                    .putString("custom_driver_path", finalPath)
                    .apply();

                handler.post(() -> {
                    if (logger != null) {
                        logger.log("[Driver] Successfully installed: " + finalName);
                        logger.log("[Driver] Driver binary: " + finalPath);
                    }
                    Toast.makeText(context, "Installed: " + finalName, Toast.LENGTH_SHORT).show();
                    if (onCompleted != null) onCompleted.run();
                });

            } catch (Exception e) {
                Log.e(TAG, "Driver installation failed", e);
                handler.post(() -> {
                    if (logger != null) logger.log("[Driver] Failed to install driver: " + e.getMessage());
                    Toast.makeText(context, "Failed to install driver: " + e.getMessage(), Toast.LENGTH_LONG).show();
                });
            }
        }).start();
    }

    private static File findFirstSharedLib(File dir) {
        if (dir == null || !dir.exists()) return null;
        File[] files = dir.listFiles();
        if (files == null) return null;

        for (File f : files) {
            if (f.isFile() && f.getName().toLowerCase().contains("vulkan") && f.getName().endsWith(".so")) {
                return f;
            }
        }
        for (File f : files) {
            if (f.isFile() && f.getName().endsWith(".so")) {
                return f;
            }
        }
        for (File f : files) {
            if (f.isDirectory()) {
                File sub = findFirstSharedLib(f);
                if (sub != null) return sub;
            }
        }
        return null;
    }

    private static void setDriverPermissionsRecursive(File fileOrDir) {
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
}
