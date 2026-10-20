#include "SettingsMenuActions.h"
#include "../system/SystemManager.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>

extern "C" void port_set_display_refresh_rate(int hz);
extern "C" void port_language_set(const char* name);
extern "C" int port_language_count(void);
extern "C" const char* port_language_name(int index);
extern "C" void port_set_game_speed(float speed);

namespace mobile_ui {

namespace {

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

void SettingsMenuActions::applySliderValue(MenuActionId action, float value, Settings& settings, std::string& outToastMsg) {
    char msg[64];
    switch (action) {
    case ACT_CONTROLS_OPACITY:
        settings.opacity = std::max(0.10f, std::min(1.0f, value));
        snprintf(msg, sizeof(msg), "Controls Opacity: %.0f%%", settings.opacity * 100.0f);
        outToastMsg = msg;
        break;
    case ACT_BUTTON_SIZE:
        settings.scale = std::max(0.70f, std::min(1.40f, value));
        snprintf(msg, sizeof(msg), "Button Scale: %.0f%%", settings.scale * 100.0f);
        outToastMsg = msg;
        break;
    case ACT_STICK_SENSITIVITY:
        settings.stickSensitivity = std::max(0.50f, std::min(2.00f, value));
        snprintf(msg, sizeof(msg), "Movement Sensitivity: %.0f%%", settings.stickSensitivity * 100.0f);
        outToastMsg = msg;
        break;
    case ACT_STAR_STICK_SENSITIVITY:
        settings.starStickSensitivity = std::max(0.50f, std::min(2.50f, value));
        snprintf(msg, sizeof(msg), "Star Stick Sensitivity: %.0f%%", settings.starStickSensitivity * 100.0f);
        outToastMsg = msg;
        break;
    case ACT_TARGET_FPS: {
        int fps = (value < 45.0f) ? 30 : (value < 75.0f) ? 60 : (value < 105.0f) ? 90 : (value < 132.0f) ? 120 : 144;
        settings.targetFps = fps;
        settings.batterySaver = (fps <= 30);
        snprintf(msg, sizeof(msg), "Target Framerate: %d FPS", fps);
        outToastMsg = msg;
        break;
    }
    case ACT_GAME_SPEED: {
        float sp = 1.0f;
        if (value < 1.25f) sp = 1.0f;
        else if (value < 1.75f) sp = 1.5f;
        else if (value < 2.50f) sp = 2.0f;
        else if (value < 3.50f) sp = 3.0f;
        else sp = 4.0f;
        port_set_game_speed(sp);
        settings.gameSpeed = (sp == 1.0f) ? 1 : (sp == 1.5f) ? 2 : (sp == 2.0f) ? 3 : (sp == 3.0f) ? 4 : 5;
        snprintf(msg, sizeof(msg), "Game Speed: %.1fx", sp);
        outToastMsg = msg;
        break;
    }
    default:
        break;
    }
}

void SettingsMenuActions::applyDropdownSelection(MenuActionId action, int selectedIdx, Settings& settings,
                                                 SaveManager& saveMgr, std::string& outToastMsg) {
    switch (action) {
    case ACT_JOYSTICK_MODE:
        settings.dynamicStick = (selectedIdx == 0);
        outToastMsg = settings.dynamicStick ? "Joystick: Dynamic Anchor" : "Joystick: Fixed Anchor";
        break;
    case ACT_RIGHT_STICK_MODE:
        settings.rightStickMode = (selectedIdx == 1) ? 1 : 0;
        outToastMsg = (settings.rightStickMode == 1) ? "Right Stick: Star Pointer" : "Right Stick: 360 Camera Orbit";
        break;
    case ACT_DPAD_SIDE:
        settings.dpadOnRight = (selectedIdx == 1);
        settings.dpadOffsetX = 0.0f;
        settings.dpadOffsetY = 0.0f;
        outToastMsg = settings.dpadOnRight ? "Camera D-Pad: Right Side" : "Camera D-Pad: Left Side";
        break;
    case ACT_RENDER_SCALE: {
        settings.renderScale = std::max(0, std::min(3, selectedIdx));
        const char* rStr[] = {"Resolution: 480p Native (Fastest)", "Resolution: 720p HD (Balanced)",
                              "Resolution: 1080p FHD (Sharp)", "Resolution: 1440p 2K (Ultra HD)"};
        outToastMsg = rStr[settings.renderScale];
        break;
    }
    case ACT_DISPLAY_REFRESH: {
        int hz = (selectedIdx == 0) ? 60 : (selectedIdx == 1) ? 90 : (selectedIdx == 2) ? 120 : 144;
        settings.preferredRefreshRate = hz;
        port_set_display_refresh_rate(hz);
        mobile_system::SystemManager::setPreferredRefreshRate(nullptr, (float)hz);
        outToastMsg = "Display Refresh Rate: " + std::to_string(hz) + " Hz";
        break;
    }
    case ACT_ORIENTATION: {
        settings.screenOrientationChoice = std::max(0, std::min(2, selectedIdx));
        outToastMsg = (selectedIdx == 0) ? "Orientation: Auto Sensor (180°)" :
                      (selectedIdx == 1) ? "Orientation: Landscape" : "Orientation: Reverse Landscape";
        break;
    }
    case ACT_PERF_HUD: {
        settings.perfHudMode = std::max(0, std::min(2, selectedIdx));
        outToastMsg = (selectedIdx == 0) ? "Performance HUD: OFF" :
                      (selectedIdx == 1) ? "Performance HUD: FPS Only" : "Performance HUD: Full Hardware Stats";
        break;
    }
    case ACT_SELECT_SLOT: {
        int slot = selectedIdx + 1;
        saveMgr.setCurrentSlot(slot);
        outToastMsg = (slot == 4) ? "Active Slot: Quick State" : "Active Slot: Slot " + std::to_string(slot);
        break;
    }
    case ACT_LANGUAGE: {
        settings.languageChoice = std::max(0, std::min(5, selectedIdx));
        applyLanguage(settings.languageChoice);
        const char* lStr[] = {"Language: System Auto", "Language: English", "Language: Español",
                              "Language: Français", "Language: Deutsch", "Language: Italiano"};
        outToastMsg = lStr[settings.languageChoice];
        break;
    }
    default:
        break;
    }
}

}  // namespace mobile_ui
