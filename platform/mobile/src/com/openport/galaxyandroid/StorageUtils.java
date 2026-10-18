package com.openport.galaxyandroid;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.provider.Settings;

import java.io.File;

public final class StorageUtils {
    private StorageUtils() {}

    public static void checkStoragePermissions(Activity activity) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + activity.getPackageName()));
                try {
                    activity.startActivity(intent);
                } catch (Exception e) {
                    Intent generic = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    activity.startActivity(generic);
                }
            }
        } else {
            if (activity.checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
                    != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                activity.requestPermissions(new String[]{
                    android.Manifest.permission.READ_EXTERNAL_STORAGE,
                    android.Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, 2001);
            }
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

    public static String getDisplayName(Context context, Uri uri) {
        String displayName = null;
        if ("content".equalsIgnoreCase(uri.getScheme())) {
            try (Cursor cursor = context.getContentResolver().query(uri, null, null, null, null)) {
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

    public static String getPathFromUri(Context context, Uri uri) {
        if ("file".equalsIgnoreCase(uri.getScheme())) {
            return uri.getPath();
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
            try {
                if (DocumentsContract.isDocumentUri(context, uri)) {
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
}
