package com.galaxy.installer;

import android.content.Context;
import android.os.Handler;
import android.util.Log;

import java.io.File;

public class CookingTask {
    private static final String TAG = "CookingTask";

    public interface Listener {
        void onLog(String line);
        void onProgress(int percent, String message);
        void onFinished(boolean success, String message);
    }

    public static void runAsync(Context context, Handler handler, String sourcePath, String outputDir, boolean withMovies, File filesDir, Listener listener) {
        new Thread(() -> {
            File tempExtractionDir = null;
            try {
                // Step 1: Ensure Python runtime and cooker scripts are unpacked
                CookerEngine.prepareAssets(context, new CookerEngine.CookerCallback() {
                    @Override public void onLog(String line) { listener.onLog(line); }
                    @Override public void onProgress(int percent, String message) {}
                    @Override public void onFinished(boolean success, String message) {}
                });

                String pythonHome = new File(filesDir, "pythonhome").getAbsolutePath();
                String scriptsDir = new File(filesDir, "scripts").getAbsolutePath();

                String cookInputPath = sourcePath;
                File sourceFile = new File(sourcePath);

                // Step 2: Handle Wii Disc Image (.rvz / .wbfs / .iso / .wia / .ciso) extraction
                String lower = sourcePath.toLowerCase();
                boolean isDisc = sourceFile.isFile() && (
                    lower.endsWith(".rvz") || lower.endsWith(".wbfs") || lower.endsWith(".iso") ||
                    lower.endsWith(".wia") || lower.endsWith(".ciso") || CookerEngine.isWiiDisc(sourcePath)
                );

                if (isDisc) {
                    listener.onLog("[Stage 1/2] Native Wii Disc detected (" + sourceFile.getName() + ").");
                    listener.onLog("[Stage 1/2] Decrypting and extracting game data on-device...");
                    listener.onProgress(-1, "Extracting Wii disc image...");

                    tempExtractionDir = new File(filesDir, "extracted_cache");
                    File fstCheck = new File(tempExtractionDir, "sys/fst.bin");
                    if (!fstCheck.exists()) {
                        fstCheck = new File(tempExtractionDir, "DATA/sys/fst.bin");
                    }
                    boolean extractSuccess = true;
                    if (fstCheck.exists() && fstCheck.length() > 0) {
                        listener.onLog("[Stage 1/2] Valid disc cache found from previous extraction. Skipping re-extract.");
                    } else {
                        CookerEngine.deleteRecursive(tempExtractionDir);
                        tempExtractionDir.mkdirs();

                        extractSuccess = CookerEngine.extractWithNodtool(
                            context,
                            sourcePath,
                            tempExtractionDir.getAbsolutePath(),
                            withMovies,
                            new CookerEngine.CookerCallback() {
                                @Override public void onLog(String line) { listener.onLog(line); }
                                @Override
                                public void onProgress(int percent, String message) {
                                    listener.onProgress(percent, message);
                                }
                                @Override public void onFinished(boolean success, String message) {}
                            }
                        );

                        if (!extractSuccess && (lower.endsWith(".wbfs") || lower.endsWith(".iso"))) {
                            listener.onLog("[Fallback] Retrying with secondary native C++ decryptor...");
                            int extractRet = CookerEngine.extractDisc(
                                sourcePath,
                                tempExtractionDir.getAbsolutePath(),
                                withMovies,
                                new CookerEngine.CookerCallback() {
                                    @Override public void onLog(String line) { listener.onLog(line); }
                                    @Override
                                    public void onProgress(int percent, String message) {
                                        listener.onProgress(percent, message);
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

                    listener.onLog("[Stage 1/2] Disc extraction complete.");
                    cookInputPath = tempExtractionDir.getAbsolutePath();
                } else if (sourceFile.isFile() && sourcePath.toLowerCase().endsWith(".zip")) {
                    listener.onLog("[Stage 1/2] ZIP Archive detected. Unpacking on-device...");
                    listener.onProgress(-1, "Unpacking ZIP archive...");

                    tempExtractionDir = new File(filesDir, "extracted_cache");
                    CookerEngine.deleteRecursive(tempExtractionDir);
                    tempExtractionDir.mkdirs();

                    boolean unzipped = CookerEngine.extractZip(sourceFile, tempExtractionDir, new CookerEngine.CookerCallback() {
                        @Override public void onLog(String line) { listener.onLog(line); }
                        @Override public void onProgress(int percent, String message) {
                            if (message != null) listener.onProgress(percent, message);
                        }
                        @Override public void onFinished(boolean success, String message) {}
                    });

                    if (!unzipped) {
                        throw new RuntimeException("Failed to unpack ZIP archive.");
                    }
                    cookInputPath = tempExtractionDir.getAbsolutePath();
                }

                // Step 3: Run the asset converter (cook.py)
                listener.onLog("[Stage 2/2] Running asset cooker engine (textures, models, audio)...");
                listener.onProgress(-1, "Cooking game assets...");

                File outFolder = new File(outputDir);
                if (!outFolder.exists()) {
                    outFolder.mkdirs();
                }

                int ret = CookerEngine.runCooker(
                    pythonHome, scriptsDir, cookInputPath, outputDir, withMovies,
                    new CookerEngine.CookerCallback() {
                        @Override public void onLog(String line) { listener.onLog(line); }
                        @Override
                        public void onProgress(int percent, String message) {
                            listener.onProgress(percent, message);
                        }
                        @Override public void onFinished(boolean success, String message) {
                            listener.onFinished(success, message);
                        }
                    }
                );

                if (ret != 0) {
                    listener.onFinished(false, "Cooker returned error code: " + ret);
                } else {
                    if (tempExtractionDir != null && tempExtractionDir.exists()) {
                        listener.onLog("[Clean] Freeing temporary cache storage...");
                        CookerEngine.deleteRecursive(tempExtractionDir);
                    }
                    listener.onFinished(true, "All game assets cooked successfully!");
                }

            } catch (Exception e) {
                Log.e(TAG, "Cooking failed with exception", e);
                listener.onLog("[ERROR] " + e.getMessage());
                listener.onFinished(false, e.getMessage());
            }
        }).start();
    }
}
