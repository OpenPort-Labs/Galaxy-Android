#pragma once

#include <string>

namespace mobile_ui {

struct Settings {
    bool showControls = true;
    bool fullscreen = true;
    float opacity = 0.65f;
    float scale = 1.0f;
    bool dynamicStick = true;
    float stickOffsetX = 0.0f;
    float stickOffsetY = 0.0f;
    float buttonOffsetX = 0.0f;
    float buttonOffsetY = 0.0f;
    float btnAOffsetX = 0.0f;
    float btnAOffsetY = 0.0f;
    float btnBOffsetX = 0.0f;
    float btnBOffsetY = 0.0f;
    float btnZOffsetX = 0.0f;
    float btnZOffsetY = 0.0f;
    float btnSpinOffsetX = 0.0f;
    float btnSpinOffsetY = 0.0f;
    float starStickOffsetX = 0.0f;
    float starStickOffsetY = 0.0f;
    bool showDpad = true;
    bool dpadOnRight = false;
    float dpadOffsetX = 0.0f;
    float dpadOffsetY = 0.0f;

    // NVIDIA Shield and Remaster Mods
    bool modShieldTextures = true;
    bool modShieldCutscenes = true;
    bool modShieldControls = true;
    bool modClassicGamepadUI = true;
    bool modRiivolution = true;

    // Advanced Gameplay Mods
    bool modFly = false;
    bool modSkipCutscenes = false;
    bool modInfiniteHealth = false;
    bool modInfiniteLives = false;
    bool modInfiniteStarBits = false;
    bool odysseyMoveset = true;
    int characterChoice = 0;
    bool speedrunHud = false;
    bool coopMode = false;

    // System Settings
    int gameSpeed = 1;
    int languageChoice = 0;
    int renderScale = 0;
    bool haptics = false;
    int targetFps = 60;
    int preferredRefreshRate = 60;
    bool adaptiveFps = true;
    bool batterySaver = false;
    bool gyroAiming = false;
    bool gyroInvertX = false;
    bool gyroInvertY = false;

    bool starStick = false;
    float stickSensitivity = 1.0f;
    float starStickSensitivity = 1.0f;
    int rightStickMode = 0;
    bool touchCamera = true;

    bool fullTouchMode = true;
    int perfHudMode = 0;
    int graphicsBackend = 1;
    bool relativePointer = false;
    int screenOrientationChoice = 0;

    void load(const std::string& path);
    void save(const std::string& path) const;
};

}  // namespace mobile_ui
