#include "SettingsMenuDefs.h"

namespace mobile_ui {

static const MenuRowDef kControlsRows[] = {
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "TOUCH CONTROLS", "Virtual buttons and joystick overlay", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SHOW_CONTROLS, ROW_SWITCH, 50.0f, "On-Screen Controls", "Display virtual buttons on touchscreen", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_LAYOUT_BUTTONS, ROW_DUAL_BTN, 50.0f, "Reposition Layout", "Reset Default", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_CONTROLS_OPACITY, ROW_SLIDER, 54.0f, "Controls Opacity", "HUD transparency level", 0, {}, 0.10f, 1.00f, 0.05f, "%.0f%%"},
    {ACT_BUTTON_SIZE, ROW_SLIDER, 54.0f, "Button Scale", "Size of touch action buttons", 0, {}, 0.70f, 1.40f, 0.05f, "%.0f%%"},
    {ACT_JOYSTICK_MODE, ROW_DROPDOWN, 50.0f, "Virtual Joystick", "Stick anchor behavior", 2, {"Dynamic (Follows Thumb)", "Fixed Position"}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_STICK_SENSITIVITY, ROW_SLIDER, 54.0f, "Movement Sensitivity", "Joystick responsiveness & speed", 0, {}, 0.50f, 2.00f, 0.05f, "%.0f%%"},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "CAMERA & STAR POINTER", "Aiming, sensor tilting & camera rotation", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_TOUCH_CAMERA, ROW_SWITCH, 50.0f, "360 Touch Camera Swipe", "Drag right side of screen to freely orbit 360 camera", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_RIGHT_STICK_MODE, ROW_DROPDOWN, 50.0f, "Right Analog Stick Mode", "Gamepad / Controller right stick function", 2, {"360 Camera Orbit", "Star Pointer Steering"}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_STAR_STICK, ROW_SWITCH, 50.0f, "2nd Joystick (Star Pointer)", "Dedicated virtual stick to steer Star Pointer", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_STAR_STICK_SENSITIVITY, ROW_SLIDER, 54.0f, "Star Stick Sensitivity", "Steering speed for pointer stick", 0, {}, 0.50f, 2.50f, 0.05f, "%.0f%%"},
    {ACT_REPOSITION_STAR_STICK, ROW_DUAL_BTN, 50.0f, "Reposition Star Stick", "Reset Above Buttons", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SHOW_DPAD, ROW_SWITCH, 50.0f, "Camera D-Pad", "Directional disc for camera rotation & recenter", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_DPAD_SIDE, ROW_DROPDOWN, 50.0f, "D-Pad Position", "Screen anchor side for camera controls", 2, {"Left Side", "Right Side"}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_GYRO_AIMING, ROW_SWITCH, 50.0f, "Gyro Star Pointer Aiming", "Tilt device to steer the Star Pointer", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_GYRO_INVERT_BUTTONS, ROW_DUAL_BTN, 48.0f, "Invert X", "Invert Y", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_RELATIVE_POINTER, ROW_SWITCH, 50.0f, "Relative Pointer (Trackpad)", "Drag anywhere to steer pointer without obstructing view", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_FULL_TOUCH_MODE, ROW_SWITCH, 50.0f, "Full Touch Navigation", "Tap to jump/dialogue, flick to spin", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_HAPTICS, ROW_SWITCH, 50.0f, "Vibration Haptics", "Tactile feedback pulses on button presses", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
};

static const MenuRowDef kGraphicsRows[] = {
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "GRAPHICS ENGINE", "Hardware renderer, resolution & frame pacing", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_RENDER_SCALE, ROW_DROPDOWN, 50.0f, "Internal Resolution", "Geometric render buffer scaling", 4, {"480p Native (Fastest)", "720p HD (Balanced)", "1080p FHD (Sharp)", "1440p 2K (Ultra)"}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_TARGET_FPS, ROW_SLIDER, 54.0f, "Target Framerate", "Engine frame limiter & target cap", 0, {}, 30.0f, 144.0f, 1.0f, "%.0f FPS"},
    {ACT_DISPLAY_REFRESH, ROW_DROPDOWN, 50.0f, "Display Refresh Rate", "Panel hardware sync frequency", 4, {"60 Hz", "90 Hz", "120 Hz", "144 Hz"}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_BATTERY_SAVER, ROW_SWITCH, 50.0f, "Battery Saver / Cool Mode", "Cap at 30 FPS to minimize heat and battery drain", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "POST-PROCESSING & SHADERS", "Vulkan cinematic presentation & anti-aliasing", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_VULKAN_POSTPROCESS, ROW_SWITCH, 50.0f, "Vulkan FXAA & Cosmic Bloom", "Single-pass antialiasing, bloom and vibrance", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "DISPLAY & SCREEN", "Ultrawide presentation and orientation", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_FULLSCREEN, ROW_SWITCH, 50.0f, "Ultrawide Fullscreen", "Native Hor+ FOV expansion (no black bars)", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_ORIENTATION, ROW_DROPDOWN, 50.0f, "Screen Orientation", "Lock or allow sensor rotation", 3, {"Auto Sensor (180°)", "Landscape", "Reverse Landscape"}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_PERF_HUD, ROW_DROPDOWN, 50.0f, "Performance Monitor HUD", "On-screen real-time statistics overlay", 3, {"Off", "FPS Only", "Full Hardware Stats"}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "HARDWARE TELEMETRY", "Driver specifications and active device", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_DEVICE_INFO_CARD, ROW_INFO_CARD, 76.0f, "Hardware Telemetry", "", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
};

static const MenuRowDef kModsRows[] = {
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "SPEEDRUNNING & PRACTICE TOOLS", "Instant 0ms memory states and telemetry HUD", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SAVESTATE_ACTIONS, ROW_DUAL_BTN, 48.0f, "Save State (0ms)", "Load State (0ms)", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SPEEDRUN_HUD, ROW_SWITCH, 50.0f, "Speedrun Practice HUD", "Live position, horizontal/vertical speed, yaw, HP", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "NVIDIA SHIELD REMASTER PACK", "Official authentic Shield TV port enhancements", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SHIELD_TEXTURES, ROW_SWITCH, 50.0f, "Shield HD Textures", "1080p AI Remastered UI, characters & galaxy world", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SHIELD_CUTSCENES, ROW_SWITCH, 50.0f, "Shield 60FPS Cutscenes", "H.264 MP4 hardware video stream playback", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SHIELD_CONTROLS, ROW_SWITCH, 50.0f, "Shield Calibrated Curves", "NVIDIA Shield controller ray surfing & stick curves", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_CLASSIC_GAMEPAD, ROW_SWITCH, 50.0f, "Modern Gamepad ABXY Prompts", "Replace Wii Remote icons with Xbox/Shield buttons", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "GAMEPLAY SANDBOX & CHEATS", "Speed modifiers and player capabilities", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_GAME_SPEED, ROW_SLIDER, 54.0f, "Game Speed (Fast-Forward)", "Real-time physics and animation multiplier", 0, {}, 1.0f, 4.0f, 0.5f, "%.1fx"},
    {ACT_FLY_MODE, ROW_SWITCH, 50.0f, "Fly Mode", "Hold A to ascend smoothly, hold Z to descend", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SKIP_CUTSCENES, ROW_SWITCH, 50.0f, "Cutscene Prevention", "Instantly bypass non-interactive cutscenes", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_INFINITE_HEALTH, ROW_SWITCH, 50.0f, "Infinite Health (God Mode)", "Invincible against enemies, fire & black holes", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_INFINITE_LIVES, ROW_SWITCH, 50.0f, "Infinite Lives (99)", "Lock Mario life pouch to 99 lives", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_INFINITE_STARBITS, ROW_SWITCH, 50.0f, "Infinite Star Bits (999)", "Max out Star Bits pouch to 999", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "RIIVOLUTION MODDING", "SD card patches and game replacements", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_RIIVOLUTION, ROW_SWITCH, 50.0f, "Enable Riivolution Mods", "Load XML patch files from /sdcard/GalaxyData/mods", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_RELOAD_MODS, ROW_SINGLE_BTN, 48.0f, "Scan & Reload Mod Directory", "Scans /sdcard/GalaxyData/mods", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
};

static const MenuRowDef kSaveRows[] = {
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "INSTANT QUICK STATE", "Direct memory snapshot with zero latency", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_QUICK_CARDS, ROW_QUICK_CARDS, 56.0f, "QUICK SAVE NOW", "QUICK LOAD NOW", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "MULTI-SLOT SAVE ANYWHERE", "Isolated NAND saves with rollback protection", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SELECT_SLOT, ROW_DROPDOWN, 50.0f, "Selected Save Slot", "Choose active save destination", 4, {"Slot 1 (Standard)", "Slot 2 (Standard)", "Slot 3 (Standard)", "Quick Save Slot"}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SLOT_ACTIONS, ROW_DUAL_BTN, 48.0f, "Save to Slot", "Reload Slot", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "SAVE DATA MIGRATION", "External backup and Dolphin compatibility", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SD_BACKUP_ACTIONS, ROW_DUAL_BTN, 48.0f, "Export to SD", "Import from SD", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_STORAGE_INFO_CARD, ROW_INFO_CARD, 104.0f, "NAND Save Information", "", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "LANGUAGE & SYSTEM", "Localization, console logs and launcher", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_LANGUAGE, ROW_DROPDOWN, 50.0f, "Game Language", "Select text and audio localization", 6, {"System Auto", "English (US/UK)", "Español", "Français", "Deutsch", "Italiano"}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_VIEW_LOGS, ROW_SINGLE_BTN, 48.0f, "View System Logs Console", "Open full-screen in-game diagnostic log viewer", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
    {ACT_EXIT_LAUNCHER, ROW_SINGLE_BTN, 48.0f, "Exit to Setup Launcher", "Return to main game launcher screen", 0, {}, 0.0f, 0.0f, 0.0f, nullptr},
};

const MenuRowDef* getTabRows(int tab, size_t& outCount) {
    switch (tab) {
        case 0: outCount = sizeof(kControlsRows) / sizeof(kControlsRows[0]); return kControlsRows;
        case 1: outCount = sizeof(kGraphicsRows) / sizeof(kGraphicsRows[0]); return kGraphicsRows;
        case 2: outCount = sizeof(kModsRows) / sizeof(kModsRows[0]); return kModsRows;
        default: outCount = sizeof(kSaveRows) / sizeof(kSaveRows[0]); return kSaveRows;
    }
}

float getTabContentHeight(int tab, float uiScale) {
    size_t count = 0;
    const MenuRowDef* rows = getTabRows(tab, count);
    float total = 16.0f * uiScale;
    for (size_t i = 0; i < count; i++) {
        total += rows[i].heightDp * uiScale + 6.0f * uiScale;
    }
    return total + 24.0f * uiScale;
}

}  // namespace mobile_ui
