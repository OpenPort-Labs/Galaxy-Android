package com.openport.galaxyandroid;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class CookerAssetsHelper {
    private static final String TAG = "CookerAssetsHelper";

    public static void prepareAssets(Context context, CookerEngine.CookerCallback callback) throws Exception {
        File filesDir = context.getFilesDir();
        File pythonHomeDir = new File(filesDir, "pythonhome");
        File scriptsDir = new File(filesDir, "scripts");

        if (!pythonHomeDir.exists()) pythonHomeDir.mkdirs();
        if (!scriptsDir.exists()) scriptsDir.mkdirs();

        // Unpack pythonhome.zip if not already extracted
        File stdlibMarker = new File(pythonHomeDir, ".extracted_arm64_v1");
        if (!stdlibMarker.exists()) {
            if (callback != null) callback.onLog("[Setup] Preparing embedded converter environment (ARM64)...");
            CookerEngine.deleteRecursive(pythonHomeDir);
            pythonHomeDir.mkdirs();
            File pythonHomeCanonical = pythonHomeDir.getCanonicalFile();
            String pythonHomePrefix = pythonHomeCanonical.getPath() + File.separator;
            try (InputStream is = context.getAssets().open("pythonhome.zip");
                 ZipInputStream zis = new ZipInputStream(is)) {
                ZipEntry entry;
                byte[] buffer = new byte[8192];
                while ((entry = zis.getNextEntry()) != null) {
                    File outFile = new File(pythonHomeCanonical, entry.getName()).getCanonicalFile();
                    if (!outFile.getPath().startsWith(pythonHomePrefix) && !outFile.equals(pythonHomeCanonical)) {
                        throw new java.io.IOException("ZIP entry escapes target directory: " + entry.getName());
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
            if (callback != null) callback.onLog("[Setup] Converter environment ready.");
        }

        // Copy Python cooking scripts from assets/scripts/ into files/scripts/
        String[] scriptAssets = context.getAssets().list("scripts");
        if (scriptAssets != null) {
            byte[] buffer = new byte[8192];
            for (String file : scriptAssets) {
                if (file == null || file.equals("__pycache__") || file.endsWith(".pyc")) {
                    continue;
                }
                try {
                    String[] sub = context.getAssets().list("scripts/" + file);
                    if (sub != null && sub.length > 0) {
                        continue;
                    }
                } catch (Exception ignored) {}

                File dest = new File(scriptsDir, file);
                try (InputStream in = context.getAssets().open("scripts/" + file);
                     OutputStream out = new FileOutputStream(dest)) {
                    int len;
                    while ((len = in.read(buffer)) > 0) {
                        out.write(buffer, 0, len);
                    }
                } catch (java.io.FileNotFoundException fnf) {
                    Log.w(TAG, "Skipping directory or unopenable asset: scripts/" + file);
                }
            }
        }

        extractShaderManifestIfPresent(context);
    }

    public static void extractShaderManifestIfPresent(Context context) {
        if (context == null) return;
        File filesDir = context.getFilesDir();
        if (filesDir == null) return;
        File manifestDest = new File(filesDir, "galaxy_shader_manifest.bin");

        try (InputStream in = context.getAssets().open("galaxy_shader_manifest.bin")) {
            // Read incoming APK asset completely and compute SHA-256
            java.security.MessageDigest assetDigest = java.security.MessageDigest.getInstance("SHA-256");
            java.io.ByteArrayOutputStream assetBytes = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int r;
            while ((r = in.read(buf)) > 0) {
                assetDigest.update(buf, 0, r);
                assetBytes.write(buf, 0, r);
            }
            byte[] assetHash = assetDigest.digest();
            byte[] rawAsset = assetBytes.toByteArray();

            // Check if existing file already matches the exact SHA-256 hash
            boolean needsExtract = true;
            if (manifestDest.exists() && manifestDest.length() == rawAsset.length) {
                try (InputStream destIn = new java.io.FileInputStream(manifestDest)) {
                    java.security.MessageDigest destDigest = java.security.MessageDigest.getInstance("SHA-256");
                    while ((r = destIn.read(buf)) > 0) {
                        destDigest.update(buf, 0, r);
                    }
                    if (java.security.MessageDigest.isEqual(assetHash, destDigest.digest())) {
                        needsExtract = false; // Identical content, no re-extraction needed
                    }
                } catch (Exception ignored) {}
            }

            if (needsExtract) {
                if (rawAsset.length < 20) {
                    throw new java.io.IOException("Manifest asset too small: " + rawAsset.length + " bytes");
                }
                int magic = (rawAsset[0] & 0xFF) | ((rawAsset[1] & 0xFF) << 8) | ((rawAsset[2] & 0xFF) << 16) | ((rawAsset[3] & 0xFF) << 24);
                if (magic != 0x55535450) { // "PTSU"
                    throw new java.io.IOException("Manifest asset magic mismatch: 0x" + Integer.toHexString(magic));
                }

                Log.i(TAG, "Extracting updated galaxy_shader_manifest.bin (" + rawAsset.length + " bytes)...");
                File tmp = new File(filesDir, "galaxy_shader_manifest.bin.tmp");
                try (OutputStream out = new FileOutputStream(tmp)) {
                    out.write(rawAsset);
                    out.flush();
                }

                try {
                    java.nio.file.Files.move(tmp.toPath(), manifestDest.toPath(),
                        java.nio.file.StandardCopyOption.REPLACE_EXISTING,
                        java.nio.file.StandardCopyOption.ATOMIC_MOVE);
                } catch (Exception moveEx) {
                    if (manifestDest.exists()) manifestDest.delete();
                    if (!tmp.renameTo(manifestDest)) {
                        throw new java.io.IOException("Failed renaming " + tmp + " to " + manifestDest);
                    }
                }
                Log.i(TAG, "Bundled shader manifest atomically installed (" + manifestDest.length() + " bytes)");
            }
        } catch (java.io.FileNotFoundException ignored) {
            // Not bundled in assets
        } catch (Exception e) {
            Log.w(TAG, "Failed extracting bundled shader manifest", e);
        }
    }
}
