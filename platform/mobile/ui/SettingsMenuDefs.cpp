#include "SettingsMenuDefs.h"

namespace mobile_ui {

static const MenuRowDef kControlsRows[] = {
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "TOUCH CONTROLS", "Virtual buttons and joystick overlay", 0, {}},
    {ACT_SHOW_CONTROLS, ROW_SWITCH, 50.0f, "On-Screen Controls", "Display virtual buttons on touchscreen", 0, {}},
    {ACT_LAYOUT_BUTTONS, ROW_DUAL_BTN, 50.0f, "Reposition Layout", "Reset Default", 0, {}},
    {ACT_CONTROLS_OPACITY, ROW_SEGMENTED, 56.0f, "Controls Opacity", "HUD transparency level", 4, {"25%", "50%", "75%", "100%"}},
    {ACT_BUTTON_SIZE, ROW_SEGMENTED, 56.0f, "Button Size", "Scale of touch action buttons", 3, {"Small", "Medium", "Large"}},
    {ACT_JOYSTICK_MODE, ROW_SEGMENTED, 56.0f, "Virtual Joystick", "Stick anchor behavior", 2, {"Dynamic", "Fixed"}},
    {ACT_STICK_SENSITIVITY, ROW_SEGMENTED, 56.0f, "Movement Sensitivity", "Joystick responsiveness & speed", 4, {"75%", "100%", "125%", "150%"}},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "CAMERA & STAR POINTER", "Aiming, sensor tilting & camera rotation", 0, {}},
    {ACT_TOUCH_CAMERA, ROW_SWITCH, 50.0f, "360 Touch Camera Swipe", "Drag right side of screen to freely orbit 360 camera", 0, {}},
    {ACT_STAR_STICK, ROW_SWITCH, 50.0f, "2nd Joystick (Star Pointer)", "Dedicated virtual stick to steer Star Pointer", 0, {}},
    {ACT_STAR_STICK_SENSITIVITY, ROW_SEGMENTED, 56.0f, "Star Stick Sensitivity", "Steering speed for 2nd joystick & right stick", 5, {"50%", "75%", "100%", "150%", "200%"}},
    {ACT_REPOSITION_STAR_STICK, ROW_DUAL_BTN, 50.0f, "Reposition Star Stick", "Reset Above Buttons", 0, {}},
    {ACT_RIGHT_STICK_MODE, ROW_SEGMENTED, 56.0f, "Right Analog Stick Mode", "Gamepad / Controller right stick function", 2, {"360 Camera", "Star Pointer"}},
    {ACT_SHOW_DPAD, ROW_SWITCH, 50.0f, "Camera D-Pad", "Directional disc for camera rotation & recenter", 0, {}},
    {ACT_DPAD_SIDE, ROW_SEGMENTED, 56.0f, "D-Pad Position", "Anchor side for camera controls", 2, {"Left Side", "Right Side"}},
    {ACT_GYRO_AIMING, ROW_SWITCH, 50.0f, "Gyro Star Pointer Aiming", "Tilt device to steer the Star Pointer", 0, {}},
    {ACT_GYRO_INVERT_BUTTONS, ROW_DUAL_BTN, 48.0f, "Invert X", "Invert Y", 0, {}},
    {ACT_RELATIVE_POINTER, ROW_SWITCH, 50.0f, "Relative Pointer (Trackpad)", "Drag anywhere to steer pointer without obstructing view", 0, {}},
    {ACT_FULL_TOUCH_MODE, ROW_SWITCH, 50.0f, "Full Touch Navigation", "Tap to jump/dialogue, flick to spin", 0, {}},
    {ACT_HAPTICS, ROW_SWITCH, 50.0f, "Vibration Haptics", "Tactile feedback pulses on button presses", 0, {}},
};

static const MenuRowDef kGraphicsRows[] = {
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "GRAPHICS ENGINE", "Hardware renderer, resolution & frame pacing", 0, {}},
    {ACT_GRAPHICS_BACKEND, ROW_SEGMENTED, 56.0f, "Graphics Backend", "Select high-performance rendering pipeline", 2, {"OpenGL ES 3.2", "Vulkan 1.0"}},
    {ACT_RENDER_SCALE, ROW_SEGMENTED, 56.0f, "Internal Resolution", "Geometric render buffer scaling", 4, {"480p", "720p HD", "1080p FHD", "1440p 2K"}},
    {ACT_TARGET_FPS, ROW_SEGMENTED, 56.0f, "Target Framerate", "Engine frame limiter & target cap", 5, {"30", "60", "90", "120", "144"}},
    {ACT_DISPLAY_REFRESH, ROW_SEGMENTED, 56.0f, "Display Refresh Rate", "Panel hardware sync frequency", 4, {"60 Hz", "90 Hz", "120 Hz", "144 Hz"}},
    {ACT_BATTERY_SAVER, ROW_SWITCH, 50.0f, "Battery Saver / Cool Mode", "Cap at 30 FPS to minimize heat and battery drain", 0, {}},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "POST-PROCESSING & SHADERS", "Vulkan cinematic presentation & anti-aliasing", 0, {}},
    {ACT_VULKAN_POSTPROCESS, ROW_SWITCH, 50.0f, "Vulkan FXAA & Cosmic Bloom", "Single-pass antialiasing, bloom and vibrance", 0, {}},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "DISPLAY & SCREEN", "Ultrawide presentation and orientation", 0, {}},
    {ACT_FULLSCREEN, ROW_SWITCH, 50.0f, "Ultrawide Fullscreen", "Native Hor+ FOV expansion (no black bars)", 0, {}},
    {ACT_ORIENTATION, ROW_SEGMENTED, 56.0f, "Screen Orientation", "Lock or allow sensor rotation", 3, {"Auto Sensor", "Landscape", "Reverse"}},
    {ACT_PERF_HUD, ROW_SEGMENTED, 56.0f, "Performance Monitor HUD", "On-screen real-time statistics overlay", 3, {"Off", "FPS Only", "Full Stats"}},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "HARDWARE TELEMETRY", "Driver specifications and active device", 0, {}},
    {ACT_DEVICE_INFO_CARD, ROW_INFO_CARD, 76.0f, "Hardware Telemetry", "", 0, {}},
};

static const MenuRowDef kModsRows[] = {
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "HERO SELECT & ODYSSEY MOVESET", "Modern movement, characters & active assist", 0, {}},
    {ACT_CHARACTER_SELECT, ROW_SEGMENTED, 56.0f, "Playable Character", "Unique movement, float & physics profiles", 4, {"Mario", "Luigi", "Peach", "Rosalina"}},
    {ACT_ODYSSEY_MOVESET, ROW_SWITCH, 50.0f, "Super Mario Odyssey Moveset", "Air Dive (ZL+Y), Ground Roll, Ground-Pound High Jump", 0, {}},
    {ACT_COOP_MODE, ROW_SWITCH, 50.0f, "2-Player Co-Op Active Assist", "Controller 2 triggers Co-op Jump & Spin anywhere", 0, {}},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "SPEEDRUNNING & PRACTICE TOOLS", "Instant 0ms memory states and telemetry HUD", 0, {}},
    {ACT_SAVESTATE_ACTIONS, ROW_DUAL_BTN, 48.0f, "Save State (0ms)", "Load State (0ms)", 0, {}},
    {ACT_SPEEDRUN_HUD, ROW_SWITCH, 50.0f, "Speedrun Practice HUD", "Live position, horizontal/vertical speed, yaw, HP", 0, {}},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "NVIDIA SHIELD REMASTER PACK", "Official authentic Shield TV port enhancements", 0, {}},
    {ACT_SHIELD_TEXTURES, ROW_SWITCH, 50.0f, "Shield HD Textures", "1080p AI Remastered UI, characters & galaxy world", 0, {}},
    {ACT_SHIELD_CUTSCENES, ROW_SWITCH, 50.0f, "Shield 60FPS Cutscenes", "H.264 MP4 hardware video stream playback", 0, {}},
    {ACT_SHIELD_CONTROLS, ROW_SWITCH, 50.0f, "Shield Calibrated Curves", "NVIDIA Shield controller ray surfing & stick curves", 0, {}},
    {ACT_CLASSIC_GAMEPAD, ROW_SWITCH, 50.0f, "Modern Gamepad ABXY Prompts", "Replace Wii Remote icons with Xbox/Shield buttons", 0, {}},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "GAMEPLAY SANDBOX & CHEATS", "Speed modifiers and player capabilities", 0, {}},
    {ACT_GAME_SPEED, ROW_SEGMENTED, 56.0f, "Game Speed (Fast-Forward)", "Real-time physics and animation multiplier", 5, {"1.0x", "1.5x", "2.0x", "3.0x", "4.0x"}},
    {ACT_FLY_MODE, ROW_SWITCH, 50.0f, "Fly Mode", "Hold A to ascend smoothly, hold Z to descend", 0, {}},
    {ACT_SKIP_CUTSCENES, ROW_SWITCH, 50.0f, "Cutscene Prevention", "Instantly bypass non-interactive cutscenes", 0, {}},
    {ACT_INFINITE_HEALTH, ROW_SWITCH, 50.0f, "Infinite Health (God Mode)", "Invincible against enemies, fire & black holes", 0, {}},
    {ACT_INFINITE_LIVES, ROW_SWITCH, 50.0f, "Infinite Lives (99)", "Lock Mario life pouch to 99 lives", 0, {}},
    {ACT_INFINITE_STARBITS, ROW_SWITCH, 50.0f, "Infinite Star Bits (999)", "Max out Star Bits pouch to 999", 0, {}},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "RIIVOLUTION MODDING", "SD card patches and game replacements", 0, {}},
    {ACT_RIIVOLUTION, ROW_SWITCH, 50.0f, "Enable Riivolution Mods", "Load XML patch files from /sdcard/GalaxyData/mods", 0, {}},
    {ACT_RELOAD_MODS, ROW_SINGLE_BTN, 48.0f, "Scan & Reload Mod Directory", "Scans /sdcard/GalaxyData/mods", 0, {}},
};

static const MenuRowDef kSaveRows[] = {
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "INSTANT QUICK STATE", "Direct memory snapshot with zero latency", 0, {}},
    {ACT_QUICK_CARDS, ROW_QUICK_CARDS, 56.0f, "QUICK SAVE NOW", "QUICK LOAD NOW", 0, {}},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "MULTI-SLOT SAVE ANYWHERE", "Isolated NAND saves with rollback protection", 0, {}},
    {ACT_SELECT_SLOT, ROW_SEGMENTED, 56.0f, "Selected Save Slot", "Choose active save destination", 4, {"Slot 1", "Slot 2", "Slot 3", "Quick"}},
    {ACT_SLOT_ACTIONS, ROW_DUAL_BTN, 48.0f, "Save to Slot", "Reload Slot", 0, {}},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "SAVE DATA MIGRATION", "External backup and Dolphin compatibility", 0, {}},
    {ACT_SD_BACKUP_ACTIONS, ROW_DUAL_BTN, 48.0f, "Export to SD", "Import from SD", 0, {}},
    {ACT_STORAGE_INFO_CARD, ROW_INFO_CARD, 104.0f, "NAND Save Information", "", 0, {}},
    {ACT_SECTION_HEADER, ROW_SECTION, 36.0f, "LANGUAGE & SYSTEM", "Localization, console logs and launcher", 0, {}},
    {ACT_LANGUAGE, ROW_SEGMENTED, 56.0f, "Game Language", "Select text and audio localization", 6, {"Auto", "EN", "ES", "FR", "DE", "IT"}},
    {ACT_VIEW_LOGS, ROW_SINGLE_BTN, 48.0f, "View System Logs Console", "Open full-screen in-game diagnostic log viewer", 0, {}},
    {ACT_EXIT_LAUNCHER, ROW_SINGLE_BTN, 48.0f, "Exit to Setup Launcher", "Return to main game launcher screen", 0, {}},
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
