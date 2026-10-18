package com.openport.galaxyandroid;

import android.content.Context;
import android.os.Environment;

import java.io.File;
import java.io.FileInputStream;
import java.util.ArrayList;
import java.util.List;

public final class GameDataLocator {
    private GameDataLocator() {}

    public static boolean checkDataDir(File dataDir) {
        if (dataDir == null || !dataDir.exists()) return false;
        File sentinel = new File(dataDir, ".installed_ok");
        File fst = new File(dataDir, "sys/fst.bin");
        File files = new File(dataDir, "files");
        File coin = new File(dataDir, "files/ObjectData/Coin.arc");
        if (!fst.exists() || !fst.isFile() || !files.exists() || !files.isDirectory() || !coin.exists()) {
            return false;
        }
        try (FileInputStream fis = new FileInputStream(coin)) {
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

    public static File findGameDataFolder(Context context) {
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

    public static boolean isGameDataReady(Context context) {
        return findGameDataFolder(context) != null;
    }

    public static File autoDetectDiscImage() {
        File galaxyDataDir = new File(Environment.getExternalStorageDirectory(), "GalaxyData");
        if (galaxyDataDir.exists()) {
            File[] gFiles = galaxyDataDir.listFiles();
            if (gFiles != null) {
                for (File f : gFiles) {
                    String name = f.getName().toLowerCase();
                    if (name.endsWith(".rvz") || name.endsWith(".wbfs") || name.endsWith(".iso")) {
                        return f;
                    }
                }
            }
        }
        File downloadDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
        if (downloadDir != null && downloadDir.exists()) {
            File[] files = downloadDir.listFiles();
            if (files != null) {
                for (File f : files) {
                    String name = f.getName().toLowerCase();
                    if (name.endsWith(".rvz") || name.endsWith(".wbfs") || name.endsWith(".iso")) {
                        return f;
                    }
                }
            }
        }
        return null;
    }
}
