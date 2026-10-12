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

    public static boolean extractWithNodtool(Context context, String imagePath, String outDir, boolean includeMovies, CookerCallback callback) {
        File libDir = new File(context.getApplicationInfo().nativeLibraryDir);
        File nodtool = new File(libDir, "libnodtool.so");
        if (!nodtool.exists()) {
            nodtool = new File(context.getFilesDir(), "bin/nodtool");
        }
        nodtool.setExecutable(true, false);

        if (callback != null) callback.onLog("[Extractor] Invoking native disc extractor on: " + new File(imagePath).getName());

        try {
            ProcessBuilder pb = new ProcessBuilder(
                nodtool.getAbsolutePath(), "extract", "-p", "data", imagePath, outDir
            );
            pb.redirectErrorStream(true);
            Process proc = pb.start();

            try (java.io.BufferedReader reader = new java.io.BufferedReader(new java.io.InputStreamReader(proc.getInputStream()))) {
                String line;
                int count = 0;
                while ((line = reader.readLine()) != null) {
                    if (line.startsWith("Extracting ")) {
                        count++;
                        if (count % 35 == 0 && callback != null) {
                            String rel = line.replace("Extracting ", "").trim();
                            callback.onProgress(-1, "Extracting: " + rel);
                        }
                    } else if (callback != null && !line.trim().isEmpty()) {
                        callback.onLog(line.trim());
                    }
                }
            }

            int exitCode = proc.waitFor();
            if (exitCode != 0) {
                if (callback != null) callback.onLog("[Error] Disc extractor failed with exit code: " + exitCode);
                return false;
            }

            if (!includeMovies) {
                File movieDir = new File(outDir, "files/MovieData");
                if (movieDir.exists()) {
                    if (callback != null) callback.onLog("[Extractor] Omitting cutscenes (saving 2.3 GB space)...");
                    deleteRecursive(movieDir);
                }
            }

            if (callback != null) callback.onLog("[Extractor] Disc extraction successfully finished.");
            return true;
        } catch (Exception e) {
            Log.e(TAG, "Nodtool extraction error", e);
            if (callback != null) callback.onLog("[Error] Failed to extract disc: " + e.getMessage());
            return false;
        }
    }

    public static void prepareAssets(Context context, CookerCallback callback) throws Exception {
        File filesDir = context.getFilesDir();
        File pythonHomeDir = new File(filesDir, "pythonhome");
        File scriptsDir = new File(filesDir, "scripts");

        if (!pythonHomeDir.exists()) pythonHomeDir.mkdirs();
        if (!scriptsDir.exists()) scriptsDir.mkdirs();

        // Unpack pythonhome.zip if not already extracted (force re-extract for ARM64)
        File stdlibMarker = new File(pythonHomeDir, ".extracted_arm64_v1");
        if (!stdlibMarker.exists()) {
            if (callback != null) callback.onLog("[Setup] Unpacking embedded Python standard library (ARM64)...");
            deleteRecursive(pythonHomeDir);
            pythonHomeDir.mkdirs();
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
                        if (outFile.getName().endsWith(".so")) {
                            outFile.setReadable(true, false);
                            outFile.setExecutable(true, false);
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
