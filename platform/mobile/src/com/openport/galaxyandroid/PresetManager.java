package com.openport.galaxyandroid;

import android.content.Context;
import android.content.SharedPreferences;
import android.widget.Button;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.nio.charset.StandardCharsets;

public final class PresetManager {
    private PresetManager() {}

    public static int getCurrentResolutionPreset(SharedPreferences prefs) {
        return prefs.getInt("render_scale", 1);
    }

    public static String setResolutionPreset(Context context, SharedPreferences prefs, int scale) {
        prefs.edit().putInt("render_scale", scale).apply();

        File nandDir = new File(context.getFilesDir(), "nand");
        nandDir.mkdirs();
        File iniFile = new File(nandDir, "touch_settings.ini");
        updateIniRenderScale(iniFile, scale);

        return (scale == 0) ? "480p (Native Wii / Max Performance)" :
               (scale == 1) ? "720p (Balanced HD)" : "1080p (Full HD)";
    }

    public static void refreshResolutionButtons(Context context, SharedPreferences prefs,
                                                Button btnRes480, Button btnRes720, Button btnRes1080) {
        int current = getCurrentResolutionPreset(prefs);
        int activeTextColor = 0xFFFFFFFF;
        int inactiveTextColor = context.getColor(R.color.text_secondary);

        if (btnRes480 != null) {
            btnRes480.setBackgroundResource(current == 0 ? R.drawable.btn_primary : R.drawable.btn_secondary);
            btnRes480.setTextColor(current == 0 ? activeTextColor : inactiveTextColor);
        }
        if (btnRes720 != null) {
            btnRes720.setBackgroundResource(current == 1 ? R.drawable.btn_primary : R.drawable.btn_secondary);
            btnRes720.setTextColor(current == 1 ? activeTextColor : inactiveTextColor);
        }
        if (btnRes1080 != null) {
            btnRes1080.setBackgroundResource(current == 2 ? R.drawable.btn_primary : R.drawable.btn_secondary);
            btnRes1080.setTextColor(current == 2 ? activeTextColor : inactiveTextColor);
        }
    }

    public static void updateIniRenderScale(File iniFile, int scale) {
        try {
            StringBuilder sb = new StringBuilder();
            boolean found = false;
            if (iniFile.exists()) {
                try (BufferedReader reader = new BufferedReader(new FileReader(iniFile))) {
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
}
