package com.openport.galaxyandroid;

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
    private static final int MAX_ZIP_ENTRIES = 50000;
    private static final long MAX_ZIP_BYTES = 10L * 1024 * 1024 * 1024; // 10 GB limit

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
        File root = null;
        String rootPath = null;
        try {
            root = outDir.getCanonicalFile();
            rootPath = root.getPath() + File.separator;
        } catch (Exception e) {
            Log.e(TAG, "Cannot resolve canonical path for output directory: " + outDir, e);
            if (callback != null) callback.onLog("[Error] Invalid destination directory: " + e.getMessage());
            return false;
        }

        try (InputStream is = new java.io.FileInputStream(zipFile);
             ZipInputStream zis = new ZipInputStream(is)) {
            ZipEntry entry;
            int count = 0;
            long totalBytes = 0;
            while ((entry = zis.getNextEntry()) != null) {
                count++;
                if (count > MAX_ZIP_ENTRIES) {
                    throw new java.io.IOException("ZIP archive exceeds maximum allowed file count (" + MAX_ZIP_ENTRIES + ")");
                }

                File outFile = new File(root, entry.getName()).getCanonicalFile();
                if (!outFile.getPath().startsWith(rootPath) && !outFile.equals(root)) {
                    throw new java.io.IOException("ZIP entry escapes extraction directory (path traversal blocked): " + entry.getName());
                }

                if (entry.isDirectory()) {
                    outFile.mkdirs();
                } else {
                    File parent = outFile.getParentFile();
                    if (parent != null && !parent.exists()) {
                        parent.mkdirs();
                    }
                    try (FileOutputStream fos = new FileOutputStream(outFile)) {
                        int len;
                        while ((len = zis.read(buffer)) > 0) {
                            totalBytes += len;
                            if (totalBytes > MAX_ZIP_BYTES) {
                                throw new java.io.IOException("ZIP archive exceeds maximum allowed extracted size (10 GB)");
                            }
                            fos.write(buffer, 0, len);
                        }
                    }
                }
                zis.closeEntry();
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
        CookerAssetsHelper.prepareAssets(context, callback);
    }

    public static void extractShaderManifestIfPresent(Context context) {
        CookerAssetsHelper.extractShaderManifestIfPresent(context);
    }
}
