#include "SettingsMenuActions.h"
#include "SettingsMenuModActions.h"
#include "../system/SystemManager.h"

#include <android/log.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MobileUI", __VA_ARGS__)

extern "C" void port_set_display_refresh_rate(int hz);
extern "C" void port_language_set(const char* name);
extern "C" int port_language_count(void);
extern "C" const char* port_language_name(int index);
extern "C" void port_android_exit_to_launcher(void);

namespace mobile_ui {

namespace {

static void applyTargetFps(int fps) {
    if (fps < 30) fps = 30;
    if (fps > 144) fps = 144;
    port_set_display_refresh_rate(fps);
    mobile_system::SystemManager::setPreferredRefreshRate(nullptr, (float)fps);
}


static void applyLanguage(int langIdx) {
    if (langIdx <= 0) {
        port_language_set("auto");
        return;
    }
    const char* wanted = (langIdx == 1) ? "english" :
                         (langIdx == 2) ? "spanish" :
                         (langIdx == 3) ? "french" :
                         (langIdx == 4) ? "german" : "italian";
    int count = port_language_count();
    if (count > 0) {
        bool found = false;
        for (int i = 0; i < count; i++) {
            const char* n = port_language_name(i);
            if (n && strcasecmp(n, wanted) == 0) {
                found = true;
                break;
            }
        }
        port_language_set(found ? wanted : (port_language_name(0) ? port_language_name(0) : "english"));
    } else {
        port_language_set(wanted);
    }
}

}  // namespace

bool SettingsMenuActions::dispatch(MenuActionId action, float px, float midX, float relX, float contentW,
                                   Settings& settings, SaveManager& saveMgr,
                                   std::string& outToastMsg, bool& outEditMode, bool& outResetLayout,
                                   bool& outCloseSettings, const std::string& savePath) {
    (void)savePath;
    switch (action) {
    case ACT_SHOW_CONTROLS:
        settings.showControls = !settings.showControls;
        outToastMsg = settings.showControls ? "On-Screen Controls: ON" : "On-Screen Controls: HIDDEN";
        break;
    case ACT_LAYOUT_BUTTONS:
        if (px < midX) {
            outCloseSettings = true;
            outEditMode = true;
        } else {
            outResetLayout = true;
            outToastMsg = "Controls layout reset to defaults";
        }
        break;
    case ACT_CONTROLS_OPACITY: {
        int idx = std::max(0, std::min(3, (int)(relX / (contentW / 4.0f))));
        settings.opacity = (idx == 0) ? 0.25f : (idx == 1) ? 0.50f : (idx == 2) ? 0.75f : 1.00f;
        break;
    }
    case ACT_BUTTON_SIZE: {
        int idx = std::max(0, std::min(2, (int)(relX / (contentW / 3.0f))));
        settings.scale = (idx == 0) ? 0.85f : (idx == 1) ? 1.00f : 1.25f;
        break;
    }
    case ACT_JOYSTICK_MODE:
        settings.dynamicStick = (px < midX);
        outToastMsg = settings.dynamicStick ? "Joystick: Dynamic Anchor" : "Joystick: Fixed Anchor";
        break;
    case ACT_STICK_SENSITIVITY: {
        int idx = std::max(0, std::min(3, (int)(relX / (contentW / 4.0f))));
        settings.stickSensitivity = (idx == 0) ? 0.75f : (idx == 1) ? 1.00f : (idx == 2) ? 1.25f : 1.50f;
        char msg[64];
        snprintf(msg, sizeof(msg), "Movement Sensitivity: %.0f%%", settings.stickSensitivity * 100.0f);
        outToastMsg = msg;
        break;
    }
    case ACT_TOUCH_CAMERA:
        settings.touchCamera = !settings.touchCamera;
        outToastMsg = settings.touchCamera ? "360 Touch Camera: ON (Swipe right screen)" : "360 Touch Camera: OFF";
        break;
    case ACT_STAR_STICK:
        settings.starStick = !settings.starStick;
        outToastMsg = settings.starStick ? "2nd Joystick (Star Pointer): ON" : "2nd Joystick (Star Pointer): OFF";
        break;
    case ACT_STAR_STICK_SENSITIVITY: {
        int idx = std::max(0, std::min(4, (int)(relX / (contentW / 5.0f))));
        settings.starStickSensitivity = (idx == 0) ? 0.50f : (idx == 1) ? 0.75f : (idx == 2) ? 1.00f : (idx == 3) ? 1.50f : 2.00f;
        char msg[64];
        snprintf(msg, sizeof(msg), "Star Stick Sensitivity: %.0f%%", settings.starStickSensitivity * 100.0f);
        outToastMsg = msg;
        break;
    }
    case ACT_REPOSITION_STAR_STICK:
        if (px < midX) {
            outCloseSettings = true;
            outEditMode = true;
            outToastMsg = "Drag Star Joystick to any position, then tap SAVE";
        } else {
            settings.starStickOffsetX = 0.0f;
            settings.starStickOffsetY = 0.0f;
            outToastMsg = "Star Joystick reset to default position above buttons";
        }
        break;
    case ACT_RIGHT_STICK_MODE:
        settings.rightStickMode = (px >= midX) ? 1 : 0;
        outToastMsg = (settings.rightStickMode == 1) ? "Right Stick: Star Pointer" : "Right Stick: 360 Camera Orbit";
        break;
    case ACT_SHOW_DPAD:
        settings.showDpad = !settings.showDpad;
        outToastMsg = settings.showDpad ? "Camera D-Pad: ON" : "Camera D-Pad: OFF";
        break;
    case ACT_DPAD_SIDE:
        settings.dpadOnRight = (px >= midX);
        settings.dpadOffsetX = 0.0f;
        settings.dpadOffsetY = 0.0f;
        outToastMsg = settings.dpadOnRight ? "Camera D-Pad: Right Side" : "Camera D-Pad: Left Side";
        break;
    case ACT_GYRO_AIMING:
        settings.gyroAiming = !settings.gyroAiming;
        outToastMsg = settings.gyroAiming ? "Gyro Aiming: ON (Tilt device to aim)" : "Gyro Aiming: OFF";
        break;
    case ACT_GYRO_INVERT_BUTTONS:
        if (px < midX) {
            settings.gyroInvertX = !settings.gyroInvertX;
            outToastMsg = settings.gyroInvertX ? "Gyro Invert Horizontal (X): ON" : "Gyro Invert Horizontal (X): OFF";
        } else {
            settings.gyroInvertY = !settings.gyroInvertY;
            outToastMsg = settings.gyroInvertY ? "Gyro Invert Vertical (Y): ON" : "Gyro Invert Vertical (Y): OFF";
        }
        break;
    case ACT_RELATIVE_POINTER:
        settings.relativePointer = !settings.relativePointer;
        outToastMsg = settings.relativePointer ? "Relative Pointer: ON (Trackpad mode)" : "Relative Pointer: OFF";
        break;
    case ACT_FULL_TOUCH_MODE:
        settings.fullTouchMode = !settings.fullTouchMode;
        outToastMsg = settings.fullTouchMode ? "Full Touch Mode: ON" : "Full Touch Mode: OFF";
        break;
    case ACT_HAPTICS:
        settings.haptics = !settings.haptics;
        outToastMsg = settings.haptics ? "Vibration Haptics: ON" : "Vibration Haptics: OFF";
        break;
    case ACT_GRAPHICS_BACKEND:
        settings.graphicsBackend = (px < midX) ? 0 : 1;
        outToastMsg = (settings.graphicsBackend == 0) ? "Backend: OpenGL ES 3.2" : "Backend: Vulkan 1.0";
        break;
    case ACT_RENDER_SCALE: {
        int idx = std::max(0, std::min(3, (int)(relX / (contentW / 4.0f))));
        settings.renderScale = idx;
        outToastMsg = (idx == 0) ? "Resolution: 480p Native (Fastest)" :
                      (idx == 1) ? "Resolution: 720p HD (Balanced)" :
                      (idx == 2) ? "Resolution: 1080p FHD (Sharp)" : "Resolution: 1440p 2K (Ultra HD)";
        break;
    }
    case ACT_TARGET_FPS: {
        int idx = std::max(0, std::min(4, (int)(relX / (contentW / 5.0f))));
        int fps = (idx == 0) ? 30 : (idx == 1) ? 60 : (idx == 2) ? 90 : (idx == 3) ? 120 : 144;
        settings.targetFps = fps;
        settings.batterySaver = (fps <= 30);
        applyTargetFps(fps);
        outToastMsg = "Target Framerate: " + std::to_string(fps) + " FPS";
        break;
    }
    case ACT_DISPLAY_REFRESH: {
        int idx = std::max(0, std::min(3, (int)(relX / (contentW / 4.0f))));
        int fps = (idx == 0) ? 60 : (idx == 1) ? 90 : (idx == 2) ? 120 : 144;
        settings.targetFps = fps;
        settings.batterySaver = false;
        applyTargetFps(fps);
        outToastMsg = "Display Refresh Rate: " + std::to_string(fps) + " Hz";
        break;
    }
    case ACT_BATTERY_SAVER:
        settings.batterySaver = !settings.batterySaver;
        settings.targetFps = settings.batterySaver ? 30 : 60;
        applyTargetFps(settings.targetFps);
        outToastMsg = settings.batterySaver ? "Battery Saver: ON (30 FPS Lock)" : "Battery Saver: OFF";
        break;
    case ACT_FULLSCREEN:
        settings.fullscreen = !settings.fullscreen;
        outToastMsg = settings.fullscreen ? "Fullscreen Ultrawide: ON" : "Fullscreen: OFF (16:9)";
        break;
    case ACT_ORIENTATION: {
        int idx = std::max(0, std::min(2, (int)(relX / (contentW / 3.0f))));
        settings.screenOrientationChoice = idx;
        outToastMsg = (idx == 0) ? "Orientation: Auto Sensor (180°)" :
                      (idx == 1) ? "Orientation: Landscape" : "Orientation: Reverse Landscape";
        break;
    }
    case ACT_PERF_HUD: {
        int idx = std::max(0, std::min(2, (int)(relX / (contentW / 3.0f))));
        settings.perfHudMode = idx;
        outToastMsg = (idx == 0) ? "Performance HUD: OFF" : (idx == 1) ? "Performance HUD: FPS Only" : "Performance HUD: Full Hardware Stats";
        break;
    }
    case ACT_LANGUAGE: {
        int idx = std::max(0, std::min(5, (int)(relX / (contentW / 6.0f))));
        settings.languageChoice = idx;
        applyLanguage(settings.languageChoice);
        outToastMsg = (idx == 0) ? "Language: System Auto" : (idx == 1) ? "Language: English" :
                      (idx == 2) ? "Language: Español" : (idx == 3) ? "Language: Français" :
                      (idx == 4) ? "Language: Deutsch" : "Language: Italiano";
        break;
    }
    case ACT_EXIT_LAUNCHER:
        outCloseSettings = true;
        port_android_exit_to_launcher();
        return true;
    default:
        if (SettingsMenuModActions::dispatch(action, px, midX, relX, contentW,
                                             settings, saveMgr, outToastMsg, outCloseSettings)) {
            return true;
        }
        break;
    }
    return false;
}

}  // namespace mobile_ui
