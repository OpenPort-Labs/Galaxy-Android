package com.openport.galaxyandroid;

import android.app.Activity;
import android.net.Uri;
import android.os.Environment;
import android.os.Handler;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

public final class CookerTask {
    private static final String TAG = "CookerTask";

    public interface CookerTaskListener {
        void onLog(String line);
        void onProgress(int percent, String message);
        void onCompleted(boolean success, String message);
    }

    private CookerTask() {}

    public static void start(Activity activity, Handler handler, String selectedPath, Uri selectedUri,
                             String selectedDisplayName, boolean includeMovies, CookerTaskListener listener) {
        new Thread(() -> {
            try {
                File outputDir = new File(Environment.getExternalStorageDirectory(), "GalaxyData");
                if (!outputDir.exists() && !outputDir.mkdirs()) {
                    if (listener != null) listener.onLog("[Storage] Primary storage not directly writable; using app external storage.");
                    File appExt = activity.getExternalFilesDir(null);
                    if (appExt != null) {
                        outputDir = new File(appExt, "GalaxyData");
                        outputDir.mkdirs();
                    } else {
                        outputDir = new File(activity.getFilesDir(), "GalaxyData");
                        outputDir.mkdirs();
                    }
                }
                if (listener != null) listener.onLog("[Storage] Target installation directory: " + outputDir.getAbsolutePath());

                String testName = !selectedDisplayName.isEmpty() ? selectedDisplayName : selectedPath;
                String lowerName = testName.toLowerCase();
                boolean isDiscImage = lowerName.endsWith(".rvz") || lowerName.endsWith(".wbfs")
                        || lowerName.endsWith(".iso") || lowerName.endsWith(".gcm");

                File resolvedDiscFile = null;
                if (isDiscImage) {
                    if (new File(selectedPath).exists()) {
                        resolvedDiscFile = new File(selectedPath);
                    } else if (selectedUri != null) {
                        if (listener != null) {
                            listener.onLog("[Setup] Reading disc image: " + selectedDisplayName);
                            listener.onProgress(-1, "Caching disc image for extraction...");
                        }
                        String ext = lowerName.endsWith(".rvz") ? "rvz" : lowerName.endsWith(".wbfs") ? "wbfs" : "iso";
                        File tempDisc = new File(activity.getFilesDir(), "temp_disc." + ext);
                        if (tempDisc.exists()) tempDisc.delete();
                        try (InputStream in = activity.getContentResolver().openInputStream(selectedUri);
                             FileOutputStream out = new FileOutputStream(tempDisc)) {
                            byte[] buf = new byte[1024 * 1024];
                            int read;
                            long total = 0;
                            while ((read = in.read(buf)) > 0) {
                                out.write(buf, 0, read);
                                total += read;
                                if (total % (50 * 1024 * 1024) == 0) {
                                    final long mb = total / (1024 * 1024);
                                    if (listener != null) listener.onProgress(-1, "Buffered " + mb + " MB...");
                                }
                            }
                        }
                        resolvedDiscFile = tempDisc;
                    }
                }

                File extractionDir = outputDir;
                File tempExtractionDir = null;

                if (isDiscImage && resolvedDiscFile != null && resolvedDiscFile.exists()) {
                    tempExtractionDir = new File(activity.getFilesDir(), "temp_extracted");
                    CookerEngine.deleteRecursive(tempExtractionDir);
                    tempExtractionDir.mkdirs();

                    final String discPathToExtract = resolvedDiscFile.getAbsolutePath();
                    if (listener != null) listener.onProgress(-1, "Extracting disc partition...");

                    CookerEngine.CookerCallback extractCallback = new CookerEngine.CookerCallback() {
                        @Override public void onLog(String line) {
                            if (listener != null) listener.onLog(line);
                        }
                        @Override public void onProgress(int percent, String message) {
                            if (listener != null) listener.onProgress(percent, message);
                        }
                        @Override public void onFinished(boolean success, String message) {}
                    };

                    boolean extractSuccess = false;
                    try {
                        if (listener != null) listener.onLog("[Installer] Attempting fast in-process native disc extraction...");
                        int res = CookerEngine.extractDisc(discPathToExtract, tempExtractionDir.getAbsolutePath(), includeMovies, extractCallback);
                        extractSuccess = (res == 0);
                    } catch (Throwable t) {
                        if (listener != null) listener.onLog("[Installer] In-process extraction exception: " + t.getMessage());
                        extractSuccess = false;
                    }

                    if (!extractSuccess) {
                        if (listener != null) listener.onLog("[Installer] In-process extraction returned non-zero, trying fallback extractor...");
                        extractSuccess = CookerEngine.extractWithNodtool(
                            activity, discPathToExtract, tempExtractionDir.getAbsolutePath(), includeMovies, extractCallback
                        );
                    }

                    if (resolvedDiscFile.getAbsolutePath().startsWith(activity.getFilesDir().getAbsolutePath())) {
                        resolvedDiscFile.delete();
                    }

                    if (!extractSuccess) {
                        handler.post(() -> {
                            if (listener != null) listener.onCompleted(false, "Native disc extraction failed.");
                        });
                        return;
                    }
                    extractionDir = tempExtractionDir;
                }

                if (listener != null) listener.onProgress(-1, "Setting up converter engine...");
                CookerEngine.prepareAssets(activity, new CookerEngine.CookerCallback() {
                    @Override public void onLog(String line) {
                        if (listener != null) listener.onLog(line);
                    }
                    @Override public void onProgress(int percent, String message) {}
                    @Override public void onFinished(boolean success, String message) {}
                });

                File filesDir = activity.getFilesDir();
                File pythonHome = new File(filesDir, "pythonhome");
                File scriptsDir = new File(filesDir, "scripts");

                if (listener != null) listener.onProgress(-1, "Converting assets to little-endian ARM64...");
                final File finalTempExtraction = tempExtractionDir;
                int ret = CookerEngine.runCooker(
                    pythonHome.getAbsolutePath(),
                    scriptsDir.getAbsolutePath(),
                    extractionDir.getAbsolutePath(),
                    outputDir.getAbsolutePath(),
                    includeMovies,
                    new CookerEngine.CookerCallback() {
                        @Override public void onLog(String line) {
                            if (listener != null) listener.onLog(line);
                        }
                        @Override public void onProgress(int percent, String message) {
                            if (listener != null) listener.onProgress(percent, message);
                        }
                        @Override public void onFinished(boolean success, String message) {
                            if (finalTempExtraction != null && finalTempExtraction.exists()) {
                                if (listener != null) listener.onLog("[Clean] Freeing temporary extraction cache...");
                                CookerEngine.deleteRecursive(finalTempExtraction);
                            }
                            handler.post(() -> {
                                if (listener != null) listener.onCompleted(success, message);
                            });
                        }
                    }
                );

                if (ret != 0) {
                    handler.post(() -> {
                        if (listener != null) listener.onCompleted(false, "Converter returned code: " + ret);
                    });
                }

            } catch (Exception e) {
                Log.e(TAG, "Cooking failed with exception", e);
                if (listener != null) listener.onLog("[ERROR] " + e.getMessage());
                handler.post(() -> {
                    if (listener != null) listener.onCompleted(false, e.getMessage());
                });
            }
        }).start();
    }
}
