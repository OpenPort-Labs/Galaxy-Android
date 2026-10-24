#include "Settings.h"

#include <android/log.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MobileUI", __VA_ARGS__)

namespace mobile_ui {

void Settings::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        float val = 0.0f;
        if (sscanf(line, "%63[^=]=%f", key, &val) == 2) {
            if (!strcmp(key, "show_controls")) showControls = (val > 0.5f);
            else if (!strcmp(key, "fullscreen")) fullscreen = (val > 0.5f);
            else if (!strcmp(key, "opacity")) opacity = std::max(0.1f, std::min(1.0f, val));
            else if (!strcmp(key, "scale")) scale = std::max(0.6f, std::min(1.6f, val));
            else if (!strcmp(key, "dynamic_stick")) dynamicStick = (val > 0.5f);
            else if (!strcmp(key, "stick_offset_x")) stickOffsetX = val;
            else if (!strcmp(key, "stick_offset_y")) stickOffsetY = val;
            else if (!strcmp(key, "button_offset_x")) buttonOffsetX = val;
            else if (!strcmp(key, "button_offset_y")) buttonOffsetY = val;
            else if (!strcmp(key, "btn_a_offset_x")) btnAOffsetX = val;
            else if (!strcmp(key, "btn_a_offset_y")) btnAOffsetY = val;
            else if (!strcmp(key, "btn_b_offset_x")) btnBOffsetX = val;
            else if (!strcmp(key, "btn_b_offset_y")) btnBOffsetY = val;
            else if (!strcmp(key, "btn_z_offset_x")) btnZOffsetX = val;
            else if (!strcmp(key, "btn_z_offset_y")) btnZOffsetY = val;
            else if (!strcmp(key, "btn_spin_offset_x")) btnSpinOffsetX = val;
            else if (!strcmp(key, "btn_spin_offset_y")) btnSpinOffsetY = val;
            else if (!strcmp(key, "star_stick_offset_x")) starStickOffsetX = val;
            else if (!strcmp(key, "star_stick_offset_y")) starStickOffsetY = val;
            else if (!strcmp(key, "show_dpad")) showDpad = (val > 0.5f);
            else if (!strcmp(key, "dpad_on_right")) dpadOnRight = (val > 0.5f);
            else if (!strcmp(key, "dpad_offset_x")) dpadOffsetX = val;
            else if (!strcmp(key, "dpad_offset_y")) dpadOffsetY = val;
            else if (!strcmp(key, "mod_shield_textures")) modShieldTextures = (val > 0.5f);
            else if (!strcmp(key, "mod_shield_cutscenes")) modShieldCutscenes = (val > 0.5f);
            else if (!strcmp(key, "mod_shield_controls")) modShieldControls = (val > 0.5f);
            else if (!strcmp(key, "mod_classic_gamepad_ui")) modClassicGamepadUI = (val > 0.5f);
            else if (!strcmp(key, "mod_riivolution")) modRiivolution = (val > 0.5f);
            else if (!strcmp(key, "mod_fly")) modFly = (val > 0.5f);
            else if (!strcmp(key, "mod_skip_cutscenes")) modSkipCutscenes = (val > 0.5f);
            else if (!strcmp(key, "mod_infinite_health")) modInfiniteHealth = (val > 0.5f);
            else if (!strcmp(key, "mod_infinite_lives")) modInfiniteLives = (val > 0.5f);
            else if (!strcmp(key, "mod_infinite_starbits")) modInfiniteStarBits = (val > 0.5f);
            else if (!strcmp(key, "game_speed")) gameSpeed = std::max(1, std::min(5, (int)val));
            else if (!strcmp(key, "language_choice")) languageChoice = std::max(0, std::min(5, (int)val));
            else if (!strcmp(key, "render_scale")) renderScale = std::max(0, std::min(3, (int)val));
            else if (!strcmp(key, "target_fps")) {
                int v = (int)val;
                if (v >= 144) targetFps = 144;
                else if (v >= 120) targetFps = 120;
                else if (v >= 90) targetFps = 90;
                else if (v >= 60) targetFps = 60;
                else targetFps = 30;
            }
            else if (!strcmp(key, "display_refresh_rate")) preferredRefreshRate = (int)val;
            else if (!strcmp(key, "adaptive_fps")) adaptiveFps = (val > 0.5f);
            else if (!strcmp(key, "battery_saver")) batterySaver = (val > 0.5f);
            else if (!strcmp(key, "gyro_aiming")) gyroAiming = (val > 0.5f);
            else if (!strcmp(key, "gyro_invert_x")) gyroInvertX = (val > 0.5f);
            else if (!strcmp(key, "gyro_invert_y")) gyroInvertY = (val > 0.5f);
            else if (!strcmp(key, "star_stick")) starStick = (val > 0.5f);
            else if (!strcmp(key, "stick_sensitivity")) stickSensitivity = std::max(0.25f, std::min(3.0f, val));
            else if (!strcmp(key, "star_stick_sensitivity")) starStickSensitivity = std::max(0.25f, std::min(3.0f, val));
            else if (!strcmp(key, "right_stick_mode")) rightStickMode = std::max(0, std::min(1, (int)val));
            else if (!strcmp(key, "touch_camera")) touchCamera = (val > 0.5f);
            else if (!strcmp(key, "graphics_backend")) graphicsBackend = (int)val;
            else if (!strcmp(key, "relative_pointer")) relativePointer = (val > 0.5f);
            else if (!strcmp(key, "perf_hud")) perfHudMode = std::max(0, std::min(2, (int)val));
            else if (!strcmp(key, "full_touch_mode")) fullTouchMode = (val > 0.5f);
            else if (!strcmp(key, "screen_orientation")) screenOrientationChoice = std::max(0, std::min(2, (int)val));
            else if (!strcmp(key, "speedrun_hud")) speedrunHud = (val > 0.5f);
        }
    }
    fclose(f);
    LOGI("Loaded settings from %s", path.c_str());
}

void Settings::save(const std::string& path) const {
    // Write beside the destination and rename, so a kill mid-save cannot leave
    // a truncated settings file behind.
    const std::string tmpPath = path + ".tmp";
    FILE* f = fopen(tmpPath.c_str(), "w");
    if (!f) return;
    fprintf(f, "show_controls=%d\n", showControls ? 1 : 0);
    fprintf(f, "fullscreen=%d\n", fullscreen ? 1 : 0);
    fprintf(f, "full_touch_mode=%d\n", fullTouchMode ? 1 : 0);
    fprintf(f, "perf_hud=%d\n", perfHudMode);
    fprintf(f, "graphics_backend=%d\n", graphicsBackend);
    fprintf(f, "relative_pointer=%d\n", relativePointer ? 1 : 0);
    fprintf(f, "screen_orientation=%d\n", screenOrientationChoice);
    fprintf(f, "target_fps=%d\n", targetFps);
    fprintf(f, "display_refresh_rate=%d\n", preferredRefreshRate);
    fprintf(f, "battery_saver=%d\n", batterySaver ? 1 : 0);
    fprintf(f, "gyro_aiming=%d\n", gyroAiming ? 1 : 0);
    fprintf(f, "gyro_invert_x=%d\n", gyroInvertX ? 1 : 0);
    fprintf(f, "gyro_invert_y=%d\n", gyroInvertY ? 1 : 0);
    fprintf(f, "star_stick=%d\n", starStick ? 1 : 0);
    fprintf(f, "stick_sensitivity=%.2f\n", stickSensitivity);
    fprintf(f, "star_stick_sensitivity=%.2f\n", starStickSensitivity);
    fprintf(f, "right_stick_mode=%d\n", rightStickMode);
    fprintf(f, "touch_camera=%d\n", touchCamera ? 1 : 0);
    fprintf(f, "opacity=%.2f\n", opacity);
    fprintf(f, "scale=%.2f\n", scale);
    fprintf(f, "dynamic_stick=%d\n", dynamicStick ? 1 : 0);
    fprintf(f, "stick_offset_x=%.1f\n", stickOffsetX);
    fprintf(f, "stick_offset_y=%.1f\n", stickOffsetY);
    fprintf(f, "button_offset_x=%.1f\n", buttonOffsetX);
    fprintf(f, "button_offset_y=%.1f\n", buttonOffsetY);
    fprintf(f, "btn_a_offset_x=%.1f\n", btnAOffsetX);
    fprintf(f, "btn_a_offset_y=%.1f\n", btnAOffsetY);
    fprintf(f, "btn_b_offset_x=%.1f\n", btnBOffsetX);
    fprintf(f, "btn_b_offset_y=%.1f\n", btnBOffsetY);
    fprintf(f, "btn_z_offset_x=%.1f\n", btnZOffsetX);
    fprintf(f, "btn_z_offset_y=%.1f\n", btnZOffsetY);
    fprintf(f, "btn_spin_offset_x=%.1f\n", btnSpinOffsetX);
    fprintf(f, "btn_spin_offset_y=%.1f\n", btnSpinOffsetY);
    fprintf(f, "star_stick_offset_x=%.1f\n", starStickOffsetX);
    fprintf(f, "star_stick_offset_y=%.1f\n", starStickOffsetY);
    fprintf(f, "show_dpad=%d\n", showDpad ? 1 : 0);
    fprintf(f, "dpad_on_right=%d\n", dpadOnRight ? 1 : 0);
    fprintf(f, "dpad_offset_x=%.1f\n", dpadOffsetX);
    fprintf(f, "dpad_offset_y=%.1f\n", dpadOffsetY);
    fprintf(f, "mod_shield_textures=%d\n", modShieldTextures ? 1 : 0);
    fprintf(f, "mod_shield_cutscenes=%d\n", modShieldCutscenes ? 1 : 0);
    fprintf(f, "mod_shield_controls=%d\n", modShieldControls ? 1 : 0);
    fprintf(f, "mod_classic_gamepad_ui=%d\n", modClassicGamepadUI ? 1 : 0);
    fprintf(f, "mod_riivolution=%d\n", modRiivolution ? 1 : 0);
    fprintf(f, "mod_fly=%d\n", modFly ? 1 : 0);
    fprintf(f, "mod_skip_cutscenes=%d\n", modSkipCutscenes ? 1 : 0);
    fprintf(f, "mod_infinite_health=%d\n", modInfiniteHealth ? 1 : 0);
    fprintf(f, "mod_infinite_lives=%d\n", modInfiniteLives ? 1 : 0);
    fprintf(f, "mod_infinite_starbits=%d\n", modInfiniteStarBits ? 1 : 0);
    fprintf(f, "game_speed=%d\n", gameSpeed);
    fprintf(f, "language_choice=%d\n", languageChoice);
    fprintf(f, "render_scale=%d\n", renderScale);
    fprintf(f, "haptics=%d\n", haptics ? 1 : 0);
    fprintf(f, "target_fps=%d\n", targetFps);
    fprintf(f, "adaptive_fps=%d\n", adaptiveFps ? 1 : 0);
    fprintf(f, "speedrun_hud=%d\n", speedrunHud ? 1 : 0);
    bool ok = !ferror(f) && fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmpPath.c_str(), path.c_str()) != 0) {
        remove(tmpPath.c_str());
        LOGI("Failed to save settings atomically to %s", path.c_str());
        return;
    }
    LOGI("Saved settings to %s", path.c_str());
}

}  // namespace mobile_ui
