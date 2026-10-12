package com.galaxy.installer;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class CookerEngine {
    private static final String TAG = "CookerEngine";

    static {
        try {
            System.loadLibrary("installer");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load native library: libinstaller.so", e);
        }
    }

    public interface CookerCallback {
        void onLog(String line);
        void onProgress(int percent, String message);
        void onFinished(boolean success, String message);
    }

    public static native int runCooker(String pythonHome, String scriptsDir, String inputDir, String outputDir, boolean withMovies, CookerCallback callback);
    public static native boolean isWiiDisc(String filePath);
    public static native int extractDisc(String imagePath, String outDir, boolean includeMovies, CookerCallback callback);

    public static boolean extractZip(File zipFile, File outDir, CookerCallback callback) {
        if (callback != null) callback.onLog("[Extractor] Unpacking ZIP archive: " + zipFile.getName() + "...");
        byte[] buffer = new byte[65536];
        try (InputStream is = new java.io.FileInputStream(zipFile);
             ZipInputStream zis = new ZipInputStream(is)) {
            ZipEntry entry;
            int count = 0;
            while ((entry = zis.getNextEntry()) != null) {
                File outFile = new File(outDir, entry.getName());
                if (entry.isDirectory()) {
                    outFile.mkdirs();
                } else {
                    outFile.getParentFile().mkdirs();
                    try (FileOutputStream fos = new FileOutputStream(outFile)) {
                        int len;
                        while ((len = zis.read(buffer)) > 0) {
                            fos.write(buffer, 0, len);
                        }
                    }
                }
                zis.closeEntry();
                count++;
                if (count % 100 == 0 && callback != null) {
                    callback.onProgress(-1, "Unpacked " + count + " files...");
                }
            }
            if (callback != null) callback.onLog("[Extractor] Finished unpacking ZIP archive (" + count + " items).");
            return true;
        } catch (Exception e) {
            Log.e(TAG, "Failed to unpack zip: " + zipFile.getAbsolutePath(), e);
            if (callback != null) callback.onLog("[Error] Failed to unpack zip: " + e.getMessage());
            return false;
        }
    }

    public static void deleteRecursive(File fileOrDirectory) {
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

    public static void prepareAssets(Context context, CookerCallback callback) throws Exception {
        File filesDir = context.getFilesDir();
        File pythonHomeDir = new File(filesDir, "pythonhome");
        File scriptsDir = new File(filesDir, "scripts");

        if (!pythonHomeDir.exists()) {
            pythonHomeDir.mkdirs();
        }
        if (!scriptsDir.exists()) {
            scriptsDir.mkdirs();
        }

        // Unpack pythonhome.zip if not already extracted
        File stdlibMarker = new File(pythonHomeDir, ".extracted");
        if (!stdlibMarker.exists()) {
            if (callback != null) callback.onLog("[Setup] Unpacking embedded Python standard library...");
            try (InputStream is = context.getAssets().open("pythonhome.zip");
                 ZipInputStream zis = new ZipInputStream(is)) {
                ZipEntry entry;
                byte[] buffer = new byte[8192];
                while ((entry = zis.getNextEntry()) != null) {
                    File outFile = new File(pythonHomeDir, entry.getName());
                    if (entry.isDirectory()) {
                        outFile.mkdirs();
                    } else {
                        outFile.getParentFile().mkdirs();
                        try (FileOutputStream fos = new FileOutputStream(outFile)) {
                            int len;
                            while ((len = zis.read(buffer)) > 0) {
                                fos.write(buffer, 0, len);
                            }
                        }
                    }
                    zis.closeEntry();
                }
            }
            stdlibMarker.createNewFile();
            if (callback != null) callback.onLog("[Setup] Python runtime ready.");
        }

        // Copy Python cooking scripts from assets/scripts/ into files/scripts/
        String[] scriptAssets = context.getAssets().list("scripts");
        if (scriptAssets != null) {
            byte[] buffer = new byte[8192];
            for (String file : scriptAssets) {
                File dest = new File(scriptsDir, file);
                try (InputStream in = context.getAssets().open("scripts/" + file);
                     OutputStream out = new FileOutputStream(dest)) {
                    int len;
                    while ((len = in.read(buffer)) > 0) {
                        out.write(buffer, 0, len);
                    }
                }
            }
        }
    }
}
