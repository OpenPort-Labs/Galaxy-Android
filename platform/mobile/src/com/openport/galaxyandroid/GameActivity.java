package com.openport.galaxyandroid;

import android.app.NativeActivity;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.content.Context;
import android.content.Intent;
import android.os.VibrationEffect;
import android.os.Vibrator;

public class GameActivity extends NativeActivity {
    private static GameActivity sInstance;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sInstance = this;
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        applyFullscreen();
        setPreferredRefreshRate(144.0f);

        if (!MainActivity.isGameDataReady()) {
            returnToLauncher();
            return;
        }
    }

    public static void setPreferredRefreshRate(float maxFps) {
        if (sInstance != null) {
            sInstance.runOnUiThread(() -> {
                try {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                        WindowManager.LayoutParams lp = sInstance.getWindow().getAttributes();
                        android.view.Display display = null;
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                            display = sInstance.getDisplay();
                        } else {
                            WindowManager wm = (WindowManager) sInstance.getSystemService(Context.WINDOW_SERVICE);
                            if (wm != null) display = wm.getDefaultDisplay();
                        }
                        if (display != null) {
                            android.view.Display.Mode[] modes = display.getSupportedModes();
                            android.view.Display.Mode currentMode = display.getMode();
                            android.view.Display.Mode bestMode = null;
                            for (android.view.Display.Mode m : modes) {
                                if (currentMode != null &&
                                    (m.getPhysicalWidth() != currentMode.getPhysicalWidth() ||
                                     m.getPhysicalHeight() != currentMode.getPhysicalHeight())) {
                                    continue;
                                }
                                if (m.getRefreshRate() <= maxFps + 1.0f) {
                                    if (bestMode == null || m.getRefreshRate() > bestMode.getRefreshRate()) {
                                        bestMode = m;
                                    }
                                }
                            }
                            if (bestMode != null) {
                                lp.preferredDisplayModeId = bestMode.getModeId();
                            }
                        }
                        lp.preferredRefreshRate = (maxFps < 60.0f) ? 60.0f : maxFps;
                        sInstance.getWindow().setAttributes(lp);
                    }
                } catch (Throwable ignored) {}
            });
        }
    }

    @Override
    protected void onDestroy() {
        if (sInstance == this) sInstance = null;
        super.onDestroy();
    }

    public static void triggerHaptic(int ms) {
        if (sInstance != null) {
            sInstance.runOnUiThread(() -> {
                try {
                    Vibrator v = (Vibrator) sInstance.getSystemService(Context.VIBRATOR_SERVICE);
                    if (v != null && v.hasVibrator()) {
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                            v.vibrate(VibrationEffect.createOneShot(ms, VibrationEffect.DEFAULT_AMPLITUDE));
                        } else {
                            v.vibrate(ms);
                        }
                    }
                } catch (Throwable ignored) {}
            });
        }
    }

    public static void returnToLauncher() {
        if (sInstance != null) {
            sInstance.runOnUiThread(() -> {
                try {
                    Intent intent = new Intent(sInstance, MainActivity.class);
                    intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_NEW_TASK);
                    sInstance.startActivity(intent);
                    sInstance.finish();
                } catch (Exception ignored) {}
            });
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyFullscreen();
        }
    }

    private void applyFullscreen() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        );
    }
}
