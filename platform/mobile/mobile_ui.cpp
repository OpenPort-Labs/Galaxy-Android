#include "mobile_ui.h"

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#include "port/port.h"
#include "xr/ui_font.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MobileUI", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MobileUI", __VA_ARGS__)

namespace mobile_ui {

extern "C" void port_android_exit_to_launcher();
extern "C" void port_set_game_speed(float speed);
extern "C" float port_get_game_speed(void);
extern "C" void port_language_set(const char* name);
extern "C" void port_set_display_refresh_rate(int hz);
extern "C" int port_get_display_refresh_rate(void);

inline void applyTargetFps(int fps) {
    if (fps < 30) fps = 30;
    if (fps > 144) fps = 144;
    port_set_display_refresh_rate(fps);
}

inline void applyGameSpeed(int speedIdx) {
    float speed = 1.0f;
    switch (speedIdx) {
        case 1: speed = 1.0f; break;
        case 2: speed = 1.5f; break;
        case 3: speed = 2.0f; break;
        case 4: speed = 3.0f; break;
        case 5: speed = 4.0f; break;
        default: speed = 1.0f; break;
    }
    port_set_game_speed(speed);
}

extern "C" int port_language_count(void);
extern "C" const char* port_language_name(int index);

inline void applyLanguage(int langIdx) {
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
        if (found) {
            port_language_set(wanted);
        } else {
            const char* prim = port_language_name(0);
            port_language_set(prim ? prim : "english");
        }
    } else {
        port_language_set(wanted);
    }
}

namespace {

inline int64_t currentTimeMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}



struct Color {
    float r, g, b, a;
};

inline Color rgba(float r, float g, float b, float a) {
    return {r, g, b, a};
}

inline Color hexColor(uint32_t hex, float a) {
    float r = ((hex >> 16) & 0xFF) / 255.0f;
    float g = ((hex >> 8) & 0xFF) / 255.0f;
    float b = (hex & 0xFF) / 255.0f;
    return {r, g, b, a};
}

int base64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

enum MenuActionId {
    ACT_NONE = 0,
    ACT_SECTION_HEADER,
    // Controls Tab
    ACT_SHOW_CONTROLS,
    ACT_LAYOUT_BUTTONS,
    ACT_CONTROLS_OPACITY,
    ACT_BUTTON_SIZE,
    ACT_JOYSTICK_MODE,
    ACT_STICK_SENSITIVITY,
    ACT_SHOW_DPAD,
    ACT_DPAD_SIDE,
    ACT_GYRO_AIMING,
    ACT_GYRO_INVERT_BUTTONS,
    ACT_RELATIVE_POINTER,
    ACT_FULL_TOUCH_MODE,
    ACT_HAPTICS,
    ACT_TOUCH_CAMERA,
    ACT_STAR_STICK,
    ACT_STAR_STICK_SENSITIVITY,
    ACT_REPOSITION_STAR_STICK,
    ACT_RIGHT_STICK_MODE,
    // Graphics & Display Tab
    ACT_GRAPHICS_BACKEND,
    ACT_RENDER_SCALE,
    ACT_TARGET_FPS,
    ACT_DISPLAY_REFRESH,
    ACT_BATTERY_SAVER,
    ACT_FULLSCREEN,
    ACT_ORIENTATION,
    ACT_PERF_HUD,
    ACT_VULKAN_POSTPROCESS,
    ACT_DEVICE_INFO_CARD,
    // Mods & Enhancements Tab
    ACT_SHIELD_TEXTURES,
    ACT_SHIELD_CUTSCENES,
    ACT_SHIELD_CONTROLS,
    ACT_CLASSIC_GAMEPAD,
    ACT_CHARACTER_SELECT,
    ACT_ODYSSEY_MOVESET,
    ACT_COOP_MODE,
    ACT_SAVESTATE_ACTIONS,
    ACT_SPEEDRUN_HUD,
    ACT_GAME_SPEED,
    ACT_FLY_MODE,
    ACT_SKIP_CUTSCENES,
    ACT_INFINITE_HEALTH,
    ACT_INFINITE_LIVES,
    ACT_INFINITE_STARBITS,
    ACT_RIIVOLUTION,
    ACT_RELOAD_MODS,
    // Save & System Tab
    ACT_QUICK_CARDS,
    ACT_SELECT_SLOT,
    ACT_SLOT_ACTIONS,
    ACT_SD_BACKUP_ACTIONS,
    ACT_STORAGE_INFO_CARD,
    ACT_LANGUAGE,
    ACT_VIEW_LOGS,
    ACT_EXIT_LAUNCHER
};

enum RowType {
    ROW_SECTION,
    ROW_SWITCH,
    ROW_SEGMENTED,
    ROW_DUAL_BTN,
    ROW_SINGLE_BTN,
    ROW_INFO_CARD,
    ROW_QUICK_CARDS
};

struct MenuRowDef {
    MenuActionId action;
    RowType type;
    float heightDp;
    const char* title;
    const char* subtitle;
    int optCount;
    const char* optLabels[6];
};

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

inline const MenuRowDef* getTabRows(int tab, size_t& outCount) {
    switch (tab) {
        case 0: outCount = sizeof(kControlsRows) / sizeof(kControlsRows[0]); return kControlsRows;
        case 1: outCount = sizeof(kGraphicsRows) / sizeof(kGraphicsRows[0]); return kGraphicsRows;
        case 2: outCount = sizeof(kModsRows) / sizeof(kModsRows[0]); return kModsRows;
        default: outCount = sizeof(kSaveRows) / sizeof(kSaveRows[0]); return kSaveRows;
    }
}

inline float getTabContentHeight(int tab, float uiScale) {
    size_t count = 0;
    const MenuRowDef* rows = getTabRows(tab, count);
    float total = 16.0f * uiScale;
    for (size_t i = 0; i < count; i++) {
        total += rows[i].heightDp * uiScale + 6.0f * uiScale;
    }
    return total + 24.0f * uiScale;
}

const char* kVS = R"(#version 300 es
precision mediump float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aParams;

uniform mat4 uProj;

out vec2 vUv;
out vec4 vColor;
out vec4 vParams;

void main() {
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
    vUv = aUv;
    vColor = aColor;
    vParams = aParams;
}
)";

const char* kFS = R"(#version 300 es
precision mediump float;

in vec2 vUv;
in vec4 vColor;
in vec4 vParams;

uniform sampler2D uFontTex;

out vec4 FragColor;

void main() {
    float mode = vParams.x;
    float param = vParams.y;
    float alpha = 1.0;

    if (mode > 0.5 && mode < 1.5) {
        // Mode 1: Circle or Ring (vUv in -1..1)
        float d = length(vUv);
        float fw = fwidth(d);
        if (fw < 0.001) fw = 0.005;
        float outer = smoothstep(1.0, 1.0 - fw, d);
        if (param > 0.0) {
            float inner = smoothstep(param - fw, param, d);
            alpha = outer * inner;
        } else {
            alpha = outer;
        }
    } else if (mode > 1.5 && mode < 2.5) {
        // Mode 2: Font glyph texture: sample red channel
        alpha = texture(uFontTex, vUv).r;
    } else if (mode > 2.5 && mode < 3.5) {
        // Mode 3: Iconic Material 3 Settings Cogwheel
        float d = length(vUv);
        float fw = fwidth(d);
        if (fw < 0.001) fw = 0.005;
        float angle = atan(vUv.y, vUv.x) + param;
        float teeth = cos(8.0 * angle);
        float rMax = 0.74 + 0.22 * smoothstep(-0.25, 0.25, teeth);
        float outer = smoothstep(rMax, rMax - fw * 1.5, d);
        float inner = smoothstep(0.33 - fw * 1.5, 0.33, d);
        alpha = outer * inner;
    } else if (mode > 3.5 && mode < 4.5) {
        // Mode 4: Material 3 Close 'X' icon
        vec2 p = abs(vUv);
        float lineDist = abs(p.x - p.y) * 0.7071;
        float capDist = max(p.x, p.y);
        float fw = fwidth(lineDist);
        if (fw < 0.001) fw = 0.005;
        float line = smoothstep(0.16, 0.16 - fw, lineDist);
        float box = smoothstep(0.65, 0.65 - fw, capDist);
        alpha = line * box;
    } else if (mode > 4.5 && mode < 5.5) {
        // Mode 5: Spin Cyclone / Galaxy Swirl Glyph
        float d = length(vUv);
        float a = atan(vUv.y, vUv.x) + param;
        float aFold = mod(a, 3.14159265);
        float normAngle = clamp((aFold - 0.25) / 2.35, 0.0, 1.0);
        float arcR = mix(0.42, 0.78, normAngle);
        float dArc = abs(d - arcR);
        float thick = mix(0.06, 0.16, normAngle);
        float fw = fwidth(dArc);
        if (fw < 0.001) fw = 0.005;
        float arcAlpha = smoothstep(thick, thick - fw * 1.5, dArc);
        float angleMask = smoothstep(0.12, 0.30, aFold) * smoothstep(2.80, 2.60, aFold);
        alpha = arcAlpha * angleMask;
    } else if (mode > 5.5 && mode < 6.5) {
        // Mode 6: Modern Precision Chevron Arrow (rot in param)
        float ca = cos(param);
        float sa = sin(param);
        vec2 p = vec2(ca * vUv.x - sa * vUv.y, sa * vUv.x + ca * vUv.y);
        vec2 tip = vec2(0.30, 0.0);
        vec2 wing = vec2(-0.30, 0.52);
        vec2 seg = wing - tip;
        vec2 pt = vec2(p.x, abs(p.y)) - tip;
        float t = clamp(dot(pt, seg) / dot(seg, seg), 0.0, 1.0);
        float dLine = length(pt - seg * t);
        float fw = fwidth(dLine);
        if (fw < 0.001) fw = 0.005;
        float strokeW = 0.16;
        alpha = smoothstep(strokeW, strokeW - fw * 1.5, dLine);
    } else if (mode > 6.5 && mode < 7.5) {
        // Mode 7: Modern Minimalist Camera Glyph
        vec2 p = vUv;
        vec2 bBody = vec2(0.48, 0.32);
        vec2 pBody = p - vec2(0.0, 0.06);
        vec2 dBody = abs(pBody) - bBody;
        float distBody = length(max(dBody, 0.0)) + min(max(dBody.x, dBody.y), 0.0) - 0.10;

        vec2 bTop = vec2(0.18, 0.08);
        vec2 pTop = p - vec2(-0.16, -0.32);
        vec2 dTop = abs(pTop) - bTop;
        float distTop = length(max(dTop, 0.0)) + min(max(dTop.x, dTop.y), 0.0) - 0.05;

        float bodyUnion = min(distBody, distTop);
        float fw = fwidth(length(p));
        if (fw < 0.001) fw = 0.005;
        float fillBody = smoothstep(0.0, -fw * 1.5, bodyUnion);

        float dLens = length(p - vec2(0.0, 0.08));
        float lensHole = smoothstep(0.24 - fw, 0.24, dLens);
        float lensRing = smoothstep(0.24, 0.24 - fw, dLens) * smoothstep(0.15 - fw, 0.15, dLens);
        float lensDot = smoothstep(0.07, 0.07 - fw, dLens);

        float dFlash = length(p - vec2(0.34, -0.12));
        float flashDot = smoothstep(0.05, 0.05 - fw, dFlash);

        alpha = max(fillBody * lensHole * (1.0 - flashDot), max(lensRing, lensDot));
    } else {
        // Mode 0: Rounded rectangle with exact aspect ratio
        if (param > 0.0) {
            vec2 p = abs(vUv);
            float aspect = vParams.z;
            vec2 b = (vParams.w > 0.5) ? vec2(1.0 - param, aspect - param) : vec2(aspect - param, 1.0 - param);
            vec2 d = p - b;
            float dist = length(max(d, 0.0)) - param;
            float fw = fwidth(dist);
            if (fw < 0.001) fw = 0.005;
            alpha = smoothstep(0.0, -fw, dist);
        }
    }

    if (alpha <= 0.002) discard;
    FragColor = vec4(vColor.rgb, vColor.a * alpha);
}
)";

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("Shader compile error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

}  // namespace

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
            else if (!strcmp(key, "graphics_backend")) {
                graphicsBackend = (int)val;
            }
            else if (!strcmp(key, "relative_pointer")) relativePointer = (val > 0.5f);
            else if (!strcmp(key, "perf_hud")) perfHudMode = std::max(0, std::min(2, (int)val));
            else if (!strcmp(key, "full_touch_mode")) fullTouchMode = (val > 0.5f);
            else if (!strcmp(key, "screen_orientation")) screenOrientationChoice = std::max(0, std::min(2, (int)val));
            else if (!strcmp(key, "character_choice")) characterChoice = std::max(0, std::min(3, (int)val));
            else if (!strcmp(key, "odyssey_moveset")) odysseyMoveset = (val > 0.5f);
            else if (!strcmp(key, "coop_mode")) coopMode = (val > 0.5f);
            else if (!strcmp(key, "speedrun_hud")) speedrunHud = (val > 0.5f);
        }
    }
    fclose(f);
    LOGI("Loaded settings: show=%d, full=%d, touch=%d, hud=%d, backend=%d, relPtr=%d, orient=%d, saver=%d, gyro=%d, invX=%d, invY=%d, starStick=%d, stickSens=%.2f, starSens=%.2f, rightStickMode=%d, touchCam=%d, op=%.2f, scale=%.2f, dpad=%d, fly=%d, skip=%d, infHp=%d, infLives=%d, infBits=%d, spd=%d, lang=%d, res=%d, hap=%d, hero=%d, odyssey=%d, coop=%d, speedrunHud=%d",
         showControls, fullscreen, fullTouchMode, perfHudMode, graphicsBackend, relativePointer ? 1 : 0, screenOrientationChoice, batterySaver ? 1 : 0, gyroAiming ? 1 : 0, gyroInvertX ? 1 : 0, gyroInvertY ? 1 : 0, starStick ? 1 : 0, stickSensitivity, starStickSensitivity, rightStickMode, touchCamera ? 1 : 0, opacity, scale, showDpad, modFly, modSkipCutscenes, modInfiniteHealth, modInfiniteLives, modInfiniteStarBits, gameSpeed, languageChoice, renderScale, haptics ? 1 : 0, characterChoice, odysseyMoveset ? 1 : 0, coopMode ? 1 : 0, speedrunHud ? 1 : 0);
}

void Settings::save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    fprintf(f, "show_controls=%d\n", showControls ? 1 : 0);
    fprintf(f, "fullscreen=%d\n", fullscreen ? 1 : 0);
    fprintf(f, "full_touch_mode=%d\n", fullTouchMode ? 1 : 0);
    fprintf(f, "perf_hud=%d\n", perfHudMode);
    fprintf(f, "graphics_backend=%d\n", graphicsBackend);
    fprintf(f, "relative_pointer=%d\n", relativePointer ? 1 : 0);
    fprintf(f, "screen_orientation=%d\n", screenOrientationChoice);
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
    fprintf(f, "character_choice=%d\n", characterChoice);
    fprintf(f, "odyssey_moveset=%d\n", odysseyMoveset ? 1 : 0);
    fprintf(f, "coop_mode=%d\n", coopMode ? 1 : 0);
    fprintf(f, "speedrun_hud=%d\n", speedrunHud ? 1 : 0);
    fclose(f);
    LOGI("Saved settings to %s", path.c_str());
}


UIOverlay::UIOverlay() {
    mPointerX = 0.0f; // Screen center
    mPointerY = 0.0f; // Screen center
}

UIOverlay& UIOverlay::get() {
    static UIOverlay sInstance;
    return sInstance;
}

void UIOverlay::setSavePath(const std::string& path) {
    mSavePath = path;
    mSettings.load(path);
    applyGameSpeed(mSettings.gameSpeed);
    applyLanguage(mSettings.languageChoice);
    applyTargetFps(mSettings.batterySaver ? 30 : mSettings.targetFps);
    port_set_character_choice(mSettings.characterChoice);
    port_set_odyssey_moveset(mSettings.odysseyMoveset ? 1 : 0);
    port_set_coop_mode(mSettings.coopMode ? 1 : 0);
    port_set_speedrun_hud_enabled(mSettings.speedrunHud ? 1 : 0);
}

void UIOverlay::reportFramePresented() {
    mFpsFrameCounter++;
}

bool UIOverlay::needsRedraw() {
    float target = mSettingsOpen ? 1.0f : 0.0f;
    if (fabsf(mDrawerAnim - target) > 0.001f) {
        return true;
    }
    bool d = mDirty;
    mDirty = false;
    return d;
}

void UIOverlay::closeSettings() {
    mSettingsOpen = false;
    mDirty = true;
    if (!mSavePath.empty()) {
        mSettings.save(mSavePath);
    }
}

static bool copyFileVerified(const std::string& src, const std::string& dst) {
    struct stat st;
    if (stat(src.c_str(), &st) != 0 || st.st_size <= 0) return false;

    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;

    std::string tmpDst = dst + ".tmp";
    FILE* out = fopen(tmpDst.c_str(), "wb");
    if (!out) {
        fclose(in);
        return false;
    }

    char buf[16384];
    size_t n;
    size_t total = 0;
    bool writeErr = false;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            writeErr = true;
            break;
        }
        total += n;
    }
    fflush(out);
    int fd = fileno(out);
    if (fd >= 0) fsync(fd);
    fclose(out);
    fclose(in);

    if (writeErr || total != (size_t)st.st_size) {
        unlink(tmpDst.c_str());
        return false;
    }

    if (rename(tmpDst.c_str(), dst.c_str()) != 0) {
        unlink(tmpDst.c_str());
        return false;
    }
    return true;
}

static bool copyDirVerified(const std::string& src, const std::string& dst) {
    mkdir(dst.c_str(), 0770);
    DIR* dir = opendir(src.c_str());
    if (!dir) return false;
    struct dirent* entry;
    bool success = true;
    while ((entry = readdir(dir)) != nullptr) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (strstr(entry->d_name, ".tmp") || strstr(entry->d_name, "_rollback") || strstr(entry->d_name, "_staging")) continue;

        std::string sPath = src + "/" + entry->d_name;
        std::string dPath = dst + "/" + entry->d_name;
        struct stat st;
        if (stat(sPath.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (!copyDirVerified(sPath, dPath)) {
                    success = false;
                    break;
                }
            } else if (S_ISREG(st.st_mode)) {
                if (!copyFileVerified(sPath, dPath)) {
                    success = false;
                    break;
                }
            }
        }
    }
    closedir(dir);
    return success;
}

static bool validateSaveDir(const std::string& dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;

    // Check directly or recursively for valid GameData.bin
    std::string directGameData = dir + "/GameData.bin";
    if (stat(directGameData.c_str(), &st) == 0 && st.st_size >= 512) return true;

    DIR* d = opendir(dir.c_str());
    if (!d) return false;
    struct dirent* entry;
    bool found = false;
    while ((entry = readdir(d)) != nullptr) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        std::string sub = dir + "/" + entry->d_name;
        if (stat(sub.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (validateSaveDir(sub)) {
                    found = true;
                    break;
                }
            } else if (strstr(entry->d_name, "GameData.bin") && st.st_size >= 512) {
                found = true;
                break;
            }
        }
    }
    closedir(d);
    return found;
}

void UIOverlay::triggerSaveAnywhere() {
    port_trigger_quick_save();
    mPendingSaveSlot = 0; // 0 = general quick save anywhere
    mPendingSaveStartGen = port_get_save_generation();
    mPendingSaveWaitFrames = 0;
    if (mSettings.haptics) port_android_vibrate(50);
    showToast("Saving game progress anywhere...");
    LOGI("Save anywhere requested; waiting for game sequence to commit");
}

void UIOverlay::triggerSaveSlot(int slot) {
    port_trigger_quick_save();
    mPendingSaveSlot = slot;
    mPendingSaveStartGen = port_get_save_generation();
    mPendingSaveWaitFrames = 0;
    if (mSettings.haptics) port_android_vibrate(50);
    char msg[64];
    snprintf(msg, sizeof(msg), "Saving to Slot %d...", slot);
    showToast(msg);
    LOGI("Save to slot %d requested; waiting for game sequence to commit", slot);
}

void UIOverlay::triggerLoadSlot(int slot) {
    if (mSavePath.empty()) return;
    size_t lastSlash = mSavePath.find_last_of('/');
    if (lastSlash == std::string::npos) return;
    std::string nandDir = mSavePath.substr(0, lastSlash);
    std::string slotSuffix = (slot == 4) ? "_quick" : ("_slot" + std::to_string(slot));
    std::string slotDir = nandDir + slotSuffix;

    if (!validateSaveDir(slotDir)) {
        std::string pubSlot = "/sdcard/GalaxyData/saves/" + ((slot == 4) ? std::string("quick") : ("slot" + std::to_string(slot)));
        if (validateSaveDir(pubSlot)) {
            slotDir = pubSlot;
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), (slot == 4) ? "Quick Save slot is empty!" : "Slot %d is empty or corrupted!", slot);
            showToast(msg);
            return;
        }
    }

    // Create rollback buffer of live NAND
    std::string rollbackDir = nandDir + "_rollback";
    copyDirVerified(nandDir, rollbackDir);

    // Verified restore
    if (copyDirVerified(slotDir, nandDir) && validateSaveDir(nandDir)) {
        port_trigger_quick_load(slot);
        if (mSettings.haptics) port_android_vibrate(75);
        char msg[64];
        snprintf(msg, sizeof(msg), (slot == 4) ? "Quick Save Loaded & Stage Reloaded!" : "Slot %d Loaded & Stage Reloaded!", slot);
        showToast(msg);
        LOGI("NAND verified restored from slot %d: %s", slot, slotDir.c_str());
    } else {
        // Rollback immediately
        copyDirVerified(rollbackDir, nandDir);
        showToast("Slot restore failed! Preserved current save.");
        LOGE("Failed to restore slot %d; rolled back to previous state", slot);
    }
}

void UIOverlay::showToast(const std::string& msg) {
    mToastMessage = msg;
    mSaveToastTimer = 2.8f;
    mDirty = true;
}

bool UIOverlay::init() {
    if (mInitialized) return true;

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVS);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) return false;

    mProg = glCreateProgram();
    glAttachShader(mProg, vs);
    glAttachShader(mProg, fs);
    glLinkProgram(mProg);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(mProg, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(mProg, sizeof(log), nullptr, log);
        LOGE("UI program link error: %s", log);
        return false;
    }

    glGenVertexArrays(1, &mVao);
    glGenBuffers(1, &mVbo);

    glBindVertexArray(mVao);
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);

    // layout 0: pos (vec2)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, x));

    // layout 1: uv (vec2)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, u));

    // layout 2: color (vec4)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, r));

    // layout 3: params (vec4)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, mode));

    glBindVertexArray(0);

    // Decode font atlas into GL_R8 texture
    std::vector<uint8_t> coverage;
    getFontAtlas(mAtlasW, mAtlasH, coverage);

    glGenTextures(1, &mFontTex);
    glBindTexture(GL_TEXTURE_2D, mFontTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, mAtlasW, mAtlasH, 0, GL_RED, GL_UNSIGNED_BYTE, coverage.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const char* rend = (const char*)glGetString(GL_RENDERER);
    if (rend && rend[0]) {
        mGpuRendererName = rend;
    }

    port_set_character_choice(mSettings.characterChoice);
    port_set_odyssey_moveset(mSettings.odysseyMoveset ? 1 : 0);
    port_set_coop_mode(mSettings.coopMode ? 1 : 0);
    port_set_speedrun_hud_enabled(mSettings.speedrunHud ? 1 : 0);

    mInitialized = true;
    LOGI("Mobile UI initialized successfully (Atlas %dx%d, GPU: %s)", mAtlasW, mAtlasH, mGpuRendererName.c_str());
    return true;
}

void UIOverlay::getFontAtlas(int& width, int& height, std::vector<uint8_t>& coverage) {
    const ui_font::Face& face = ui_font::kFaces[0]; // size 40 Inter Bold
    width = face.width;
    height = face.height;

    std::vector<uint8_t> packed;
    uint32_t bits = 0;
    int nbits = 0;
    for (const char* p = face.atlas; *p; p++) {
        int v = base64Val(*p);
        if (v < 0) continue;
        bits = (bits << 6) | (uint32_t)v;
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            packed.push_back((uint8_t)(bits >> nbits));
        }
    }

    coverage.assign((size_t)face.width * face.height, 0);
    for (size_t i = 0; i < coverage.size(); i++) {
        uint8_t b = (i / 2 < packed.size()) ? packed[i / 2] : 0;
        coverage[i] = (uint8_t)(((i & 1) ? (b & 15) : (b >> 4)) * 17);
    }
}

uint32_t UIOverlay::getTouchButtons() const {
    uint32_t b = 0;
    if (mFingerDownOnA || mTapPendingFrames > 0) b |= 0x0800;
    if (mFingerDownOnB || (mTapPendingFrames > 0 && port_is_title_screen())) b |= 0x0400;
    if (mFingerDownOnZ) b |= 0x2000;
    if (mFingerDownOnPlus) b |= 0x0010;
    if (mBtnDpadLeft) b |= 0x0001;
    if (mBtnDpadRight) b |= 0x0002;
    if (mBtnDpadDown) b |= 0x0004;
    if (mBtnDpadUp) b |= 0x0008;
    return b;
}

void UIOverlay::update(PortPadState& pad, int& spinFrames) {
    // Diagnostic hook: open the layout editor straight away so it can be driven
    // from `adb shell setprop debug.petari.editlayout 1` without walking the menu.
    if (!mDebugEditChecked) {
        mDebugEditChecked = true;
        char value[PROP_VALUE_MAX] = {0};
        if (__system_property_get("debug.petari.editlayout", value) > 0 && value[0] == '1') {
            mEditLayoutMode = true;
            mSettingsOpen = false;
            mDirty = true;
            LOGI("layout edit: opened by debug.petari.editlayout");
        }
    }

    // Handle pending transactional save completion
    if (mPendingSaveSlot >= 0) {
        mPendingSaveWaitFrames++;
        bool committed = (port_get_save_generation() > mPendingSaveStartGen) ||
                         (mPendingSaveWaitFrames > 6 && !port_is_save_in_progress());

        if (committed || mPendingSaveWaitFrames > 120) {
            int slot = mPendingSaveSlot;
            mPendingSaveSlot = -1;

            if (!mSavePath.empty()) {
                size_t lastSlash = mSavePath.find_last_of('/');
                if (lastSlash != std::string::npos) {
                    std::string nandDir = mSavePath.substr(0, lastSlash);
                    mkdir("/sdcard/GalaxyData", 0775);
                    mkdir("/sdcard/GalaxyData/saves", 0775);

                    if (slot == 0) {
                        copyDirVerified(nandDir, "/sdcard/GalaxyData/saves/current");
                        showToast("Progress Saved Anywhere! (Verified)");
                    } else {
                        std::string slotSuffix = (slot == 4) ? "_quick" : ("_slot" + std::to_string(slot));
                        std::string slotDir = nandDir + slotSuffix;
                        std::string slotStaging = slotDir + "_staging";

                        if (copyDirVerified(nandDir, slotStaging) && validateSaveDir(slotStaging)) {
                            std::string slotOld = slotDir + "_old";
                            rename(slotDir.c_str(), slotOld.c_str());
                            rename(slotStaging.c_str(), slotDir.c_str());

                            std::string pubSlot = "/sdcard/GalaxyData/saves/" + ((slot == 4) ? std::string("quick") : ("slot" + std::to_string(slot)));
                            copyDirVerified(slotDir, pubSlot);
                            copyDirVerified(slotDir, "/sdcard/GalaxyData/saves/current");

                            char msg[96];
                            snprintf(msg, sizeof(msg), (slot == 4) ? "Quick Saved Anywhere! (Verified)" : "Saved to Slot %d! (Verified)", slot);
                            showToast(msg);
                            LOGI("NAND verified saved to slot %d: %s", slot, slotDir.c_str());
                        } else {
                            showToast("Save snapshot failed verification!");
                            LOGE("Save snapshot verification failed for slot %d", slot);
                        }
                    }
                }
            }
        }
    }

    // 1. Decay virtual tap pulse for full-touch controls (Title screen only)
    if (mTapDelayFrames > 0) {
        mTapDelayFrames--;
    } else if (mTapPendingFrames > 0) {
        mTapPendingFrames--;
    }

    // Direct button visual press states (without clearing physical controller bits)
    mBtnAPressed = mFingerDownOnA || (mTapPendingFrames > 0);
    mBtnBPressed = mFingerDownOnB || (mTapPendingFrames > 0 && port_is_title_screen());
    mBtnZPressed = mFingerDownOnZ;

    if (mFingerDownOnSpin) {
        spinFrames = 6;
        mBtnSpinPressed = true;
    } else if (spinFrames <= 0) {
        mBtnSpinPressed = false;
    }

    mBtnPlusPressed = mFingerDownOnPlus;

    // Always maintain valid pointer position
    pad.pointerValid = 1;
    pad.pointerX = mPointerX;
    pad.pointerY = mPointerY;
    pad.pointerDist = 2.0f;

    // Toast Timer Tick
    if (mSaveToastTimer > 0.0f) {
        mSaveToastTimer -= 0.016f;
        if (mSaveToastTimer < 0.0f) mSaveToastTimer = 0.0f;
        mDirty = true;
    }

    // 2. Hardware and Performance Monitoring Metrics (FPS, Frame Time, CPU, RAM)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t nowNs = (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
    if (mFpsLastTimeNs == 0) mFpsLastTimeNs = nowNs;
    int64_t diffNs = nowNs - mFpsLastTimeNs;
    if (diffNs >= 500000000ll) { // Sample every 500ms
        float sec = (float)diffNs / 1e9f;
        mCurrentFps = (float)mFpsFrameCounter / sec;
        int activeTargetFps = mSettings.batterySaver ? 30 : mSettings.targetFps;
        if (port_get_game_speed() <= 1.05f && mCurrentFps > (float)activeTargetFps) {
            mCurrentFps = (float)activeTargetFps;
        }
        mAvgFrameTimeMs = 1000.0f / (mCurrentFps > 1.0f ? mCurrentFps : 1.0f);
        mFpsFrameCounter = 0;
        mFpsLastTimeNs = nowNs;

        // Sample CPU usage:
        // 1. Try global /proc/stat (available on older Android or rooted/permissive ROMs)
        bool gotCpu = false;
        static unsigned long long prevUser = 0, prevNice = 0, prevSystem = 0, prevIdle = 0;
        FILE* fpStat = fopen("/proc/stat", "r");
        if (fpStat) {
            unsigned long long u = 0, n = 0, s = 0, idle = 0, iowait = 0, irq = 0, softirq = 0;
            if (fscanf(fpStat, "cpu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &idle, &iowait, &irq, &softirq) >= 4) {
                unsigned long long total = (u + n + s + idle + iowait + irq + softirq);
                unsigned long long prevTotal = (prevUser + prevNice + prevSystem + prevIdle);
                if (prevTotal > 0 && total > prevTotal) {
                    unsigned long long totalDiff = total - prevTotal;
                    unsigned long long idleDiff = (idle + iowait) - prevIdle;
                    if (totalDiff > 0 && idleDiff <= totalDiff) {
                        mCpuUsagePercent = (float)(totalDiff - idleDiff) * 100.0f / (float)totalDiff;
                        gotCpu = true;
                    }
                }
                prevUser = u + n;
                prevSystem = s + irq + softirq;
                prevIdle = idle + iowait;
            }
            fclose(fpStat);
        }

        // 2. SELinux on Android 8+ blocks app access to /proc/stat. Fall back to process CPU via /proc/self/stat
        if (!gotCpu) {
            static unsigned long long prevProcTicks = 0;
            FILE* fpSelf = fopen("/proc/self/stat", "r");
            if (fpSelf) {
                char statBuf[512];
                if (fgets(statBuf, sizeof(statBuf), fpSelf)) {
                    char* paren = strrchr(statBuf, ')');
                    if (paren) {
                        unsigned long long utime = 0, stime = 0;
                        // Fields after ')': state, ppid, pgrp, session, tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt, utime, stime
                        if (sscanf(paren + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &utime, &stime) == 2) {
                            unsigned long long totalTicks = utime + stime;
                            if (prevProcTicks > 0 && totalTicks >= prevProcTicks && sec > 0.001f) {
                                long clkTck = sysconf(_SC_CLK_TCK);
                                if (clkTck <= 0) clkTck = 100;
                                long numCores = sysconf(_SC_NPROCESSORS_ONLN);
                                if (numCores <= 0) numCores = 1;
                                float cpuSec = (float)(totalTicks - prevProcTicks) / (float)clkTck;
                                float usage = (cpuSec / sec) * 100.0f / (float)numCores;
                                if (usage < 0.0f) usage = 0.0f;
                                if (usage > 100.0f) usage = 100.0f;
                                mCpuUsagePercent = usage;
                            }
                            prevProcTicks = totalTicks;
                        }
                    }
                }
                fclose(fpSelf);
            }
        }

        // Sample RAM usage from /proc/self/statm
        FILE* fpStatm = fopen("/proc/self/statm", "r");
        if (fpStatm) {
            unsigned long sizePages = 0, residentPages = 0;
            if (fscanf(fpStatm, "%lu %lu", &sizePages, &residentPages) == 2) {
                // 4KB page size
                mRamUsageMB = (float)(residentPages * 4096) / (1024.0f * 1024.0f);
            }
            fclose(fpStatm);
        }

        // Sample GPU usage
        bool gotGpu = false;
        static const char* const gpuSysfsPaths[] = {
            "/sys/kernel/ged/hal/gpu_utilization",
            "/sys/class/misc/mali0/device/utilization",
            "/sys/devices/platform/13000000.mali/utilization",
            "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage"
        };
        for (const char* path : gpuSysfsPaths) {
            FILE* fpGpu = fopen(path, "r");
            if (fpGpu) {
                int val = -1;
                if (fscanf(fpGpu, "%d", &val) == 1 && val >= 0) {
                    mGpuUsagePercent = (float)(val > 100 ? 100 : val);
                    gotGpu = true;
                }
                fclose(fpGpu);
                if (gotGpu) break;
            }
        }
        if (!gotGpu) {
            // Render duty cycle fallback: measured active GPU render time vs window duration
            if (diffNs > 0 && mGpuRenderFrameCount > 0) {
                float gpuDuty = ((float)mGpuRenderTimeAccumNs / (float)diffNs) * 100.0f;
                if (gpuDuty < 0.0f) gpuDuty = 0.0f;
                if (gpuDuty > 100.0f) gpuDuty = 100.0f;
                mGpuUsagePercent = gpuDuty;
            } else if (mCurrentFps > 0.0f) {
                // Approximate fallback based on frame pacing
                mGpuUsagePercent = std::min(100.0f, (mAvgFrameTimeMs / 16.67f) * 40.0f);
            }
        }
        mGpuRenderTimeAccumNs = 0;
        mGpuRenderFrameCount = 0;

        mDirty = true;
    }
}

UIOverlay::ButtonLayout UIOverlay::buttonLayout(int screenW, int screenH) const {
    float baseDpi = (float)screenH / 650.0f;
    float scale = baseDpi * mSettings.scale;
    float w = (float)screenW;
    float h = (float)screenH;

    // Shared anchor of the action pad.  Dragging the pad in the layout editor
    // moves it; the per-button offsets below move a single button.
    float baseX = w - 150.0f * scale + mSettings.buttonOffsetX;
    float baseY = h - 150.0f * scale + mSettings.buttonOffsetY;

    ButtonLayout l{};

    // Nintendo Switch Diamond Geometry:
    // B (Bottom): Primary Jump button where thumb naturally rests
    l.bx = baseX - 36.0f * scale + mSettings.btnBOffsetX;
    l.by = baseY + 24.0f * scale + mSettings.btnBOffsetY;
    l.br = 52.0f * scale;

    // A (Right): Jump & Talk / Select
    l.ax = baseX + 42.0f * scale + mSettings.btnAOffsetX;
    l.ay = baseY - 42.0f * scale + mSettings.btnAOffsetY;
    l.ar = 50.0f * scale;

    // Y / SPIN (Left): Spin Attack (Nintendo Switch Spin on Y)
    l.spinX = baseX - 114.0f * scale + mSettings.btnSpinOffsetX;
    l.spinY = baseY - 42.0f * scale + mSettings.btnSpinOffsetY;
    l.spinR = 50.0f * scale;

    // ZL / Z (Upper-Left): Crouch / Ground Pound
    l.zx = baseX - 105.0f * scale + mSettings.btnZOffsetX;
    l.zy = baseY - 134.0f * scale + mSettings.btnZOffsetY;
    l.zr = 44.0f * scale;

    return l;
}

UIOverlay::StarStickLayout UIOverlay::starStickLayout(int screenW, int screenH) const {
    float baseDpi = (float)screenH / 650.0f;
    float scale = baseDpi * mSettings.scale;
    float w = (float)screenW;
    float h = (float)screenH;

    // Default position: centered cleanly directly on top of the action button diamond
    float baseStarX = w - 180.0f * scale;
    float baseStarY = h - 410.0f * scale;

    StarStickLayout l{};
    l.cx = baseStarX + mSettings.starStickOffsetX;
    l.cy = baseStarY + mSettings.starStickOffsetY;
    l.r = 70.0f * scale;
    return l;
}

bool UIOverlay::handleInput(const AInputEvent* event, PortPadState& pad, int& spinFrames,
                            int screenW, int screenH, int vpX, int vpY, int vpW, int vpH) {
    int32_t eventType = AInputEvent_getType(event);
    if (eventType != AINPUT_EVENT_TYPE_MOTION) {
        return false;
    }

    int32_t actionWithIndex = AMotionEvent_getAction(event);
    int32_t action = actionWithIndex & AMOTION_EVENT_ACTION_MASK;
    int32_t pointerIndex = (actionWithIndex & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    size_t pointerCount = AMotionEvent_getPointerCount(event);

    // Responsive scaling based on screen height
    float baseDpi = (float)screenH / 650.0f;
    float uiScale = baseDpi;
    float scale = baseDpi * mSettings.scale;
    float w = (float)screenW;
    float h = (float)screenH;
    // Material 3 Settings Tab Dock (Top-Left Edge)
    float tabW = 56.0f * uiScale;
    float tabH = 48.0f * uiScale;
    float tabY = 22.0f * uiScale;

    // Side Sheet Drawer Dimensions
    float drawerW = std::min(w * 0.44f, 390.0f * uiScale);
    float drawerX = (mDrawerAnim - 1.0f) * drawerW;

    // Check Log Viewer input (modal dialog)
    if (mLogViewerOpen) {
        if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            float px = AMotionEvent_getX(event, pointerIndex);
            float py = AMotionEvent_getY(event, pointerIndex);
            float modalPad = 24.0f * uiScale;
            float closeX = w - modalPad - 24.0f * uiScale;
            float closeY = modalPad + 22.0f * uiScale;
            float dx = px - closeX, dy = py - closeY;
            if (dx * dx + dy * dy <= 30.0f * uiScale * 30.0f * uiScale ||
                px < modalPad || px > w - modalPad || py < modalPad || py > h - modalPad) {
                mLogViewerOpen = false;
                mDirty = true;
            }
        }
        return true;
    }

    // Check Settings Tab tap (when drawer is closed or nearly closed)
    if (!mSettingsOpen && mDrawerAnim < 0.15f) {
        if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            float px = AMotionEvent_getX(event, pointerIndex);
            float py = AMotionEvent_getY(event, pointerIndex);
            if (px >= 0.0f && px <= tabW + 16.0f * uiScale && py >= tabY - 8.0f * uiScale && py <= tabY + tabH + 8.0f * uiScale) {
                mSettingsOpen = true;
                mBtnCfgPressed = true;
                mDirty = true;
                return true;
            }
            float qsX = tabW + 16.0f * uiScale;
            float qsW = 54.0f * uiScale;
            if (px >= qsX - 4.0f * uiScale && px <= qsX + qsW + 6.0f * uiScale && py >= tabY - 6.0f * uiScale && py <= tabY + tabH + 6.0f * uiScale) {
                triggerSaveAnywhere();
                return true;
            }
        }
    } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        mBtnCfgPressed = false;
    }

    // Handle Edit Layout Mode
    if (mEditLayoutMode) {
        if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            float px = AMotionEvent_getX(event, pointerIndex);
            float py = AMotionEvent_getY(event, pointerIndex);

            // Check Top buttons: Save or Reset
            float saveBtnX = w * 0.5f - 110.0f, saveBtnY = 50.0f;
            if (px >= saveBtnX && px <= saveBtnX + 100.0f && py >= saveBtnY && py <= saveBtnY + 50.0f) {
                mEditLayoutMode = false;
                if (!mSavePath.empty()) mSettings.save(mSavePath);
                mDirty = true;
                return true;
            }
            float rstBtnX = w * 0.5f + 10.0f, rstBtnY = 50.0f;
            if (px >= rstBtnX && px <= rstBtnX + 100.0f && py >= rstBtnY && py <= rstBtnY + 50.0f) {
                clearLayoutOffsets();
                if (!mSavePath.empty()) mSettings.save(mSavePath);
                mDirty = true;
                return true;
            }

            // A touch that starts on one action button drags just that button,
            // so Z, A, B and SPIN can be pulled apart.
            ButtonLayout bl = buttonLayout(screenW, screenH);
            struct BtnHit { int id; float cx, cy, r; } hits[4] = {
                {0, bl.ax, bl.ay, bl.ar},
                {1, bl.bx, bl.by, bl.br},
                {2, bl.zx, bl.zy, bl.zr},
                {3, bl.spinX, bl.spinY, bl.spinR},
            };
            for (const BtnHit& hit : hits) {
                float hitR = hit.r * 1.15f;
                float d2 = (px - hit.cx) * (px - hit.cx) + (py - hit.cy) * (py - hit.cy);
                if (d2 > hitR * hitR) continue;

                mDraggingButton = hit.id;
                LOGI("layout edit: dragging button %d from %.0f,%.0f", hit.id, px, py);
                mDragStartX = px;
                mDragStartY = py;
                switch (hit.id) {
                case 0: mInitialOwnOfsX = mSettings.btnAOffsetX; mInitialOwnOfsY = mSettings.btnAOffsetY; break;
                case 1: mInitialOwnOfsX = mSettings.btnBOffsetX; mInitialOwnOfsY = mSettings.btnBOffsetY; break;
                case 2: mInitialOwnOfsX = mSettings.btnZOffsetX; mInitialOwnOfsY = mSettings.btnZOffsetY; break;
                default: mInitialOwnOfsX = mSettings.btnSpinOffsetX; mInitialOwnOfsY = mSettings.btnSpinOffsetY; break;
                }
                mDirty = true;
                return true;
            }

            // Start dragging stick cluster, button cluster, or D-Pad cluster
            float baseDpadX = mSettings.dpadOnRight ? (w - 380.0f * scale) : (140.0f * scale);
            float baseDpadY = mSettings.dpadOnRight ? (h - 220.0f * scale) : (h - 390.0f * scale);
            float curDpadX = baseDpadX + mSettings.dpadOffsetX;
            float curDpadY = baseDpadY + mSettings.dpadOffsetY;

            float curStickX = 175.0f * scale + mSettings.stickOffsetX;
            float curStickY = h - 175.0f * scale + mSettings.stickOffsetY;

            float curBtnX = w - 160.0f * scale + mSettings.buttonOffsetX - 60.0f * scale;
            float curBtnY = h - 160.0f * scale + mSettings.buttonOffsetY - 60.0f * scale;

            float dDpad2 = (px - curDpadX) * (px - curDpadX) + (py - curDpadY) * (py - curDpadY);
            float dStick2 = (px - curStickX) * (px - curStickX) + (py - curStickY) * (py - curStickY);

            StarStickLayout starL = starStickLayout(screenW, screenH);
            float dStar2 = (px - starL.cx) * (px - starL.cx) + (py - starL.cy) * (py - starL.cy);
            float starHitR = starL.r * 1.30f;

            if (mSettings.showDpad && dDpad2 <= (95.0f * scale) * (95.0f * scale)) {
                mDraggingCluster = 3; // D-Pad
                mDragStartX = px;
                mDragStartY = py;
                mInitialDpadOfsX = mSettings.dpadOffsetX;
                mInitialDpadOfsY = mSettings.dpadOffsetY;
            } else if (mSettings.starStick && dStar2 <= starHitR * starHitR) {
                mDraggingCluster = 4; // Star Joystick (Mappable anywhere on screen)
                mDragStartX = px;
                mDragStartY = py;
                mInitialStarStickOfsX = mSettings.starStickOffsetX;
                mInitialStarStickOfsY = mSettings.starStickOffsetY;
            } else if (dStick2 <= (140.0f * scale) * (140.0f * scale) || px < w * 0.40f) {
                mDraggingCluster = 1; // stick
                mDragStartX = px;
                mDragStartY = py;
                mInitialStickOfsX = mSettings.stickOffsetX;
                mInitialStickOfsY = mSettings.stickOffsetY;
            } else {
                mDraggingCluster = 2; // button cluster
                mDragStartX = px;
                mDragStartY = py;
                mInitialBtnOfsX = mSettings.buttonOffsetX;
                mInitialBtnOfsY = mSettings.buttonOffsetY;
            }
            mDirty = true;
            return true;
        } else if (action == AMOTION_EVENT_ACTION_MOVE) {
            float px = AMotionEvent_getX(event, 0);
            float py = AMotionEvent_getY(event, 0);
            if (mDraggingButton >= 0) {
                float dx = px - mDragStartX;
                float dy = py - mDragStartY;
                switch (mDraggingButton) {
                case 0:
                    mSettings.btnAOffsetX = mInitialOwnOfsX + dx;
                    mSettings.btnAOffsetY = mInitialOwnOfsY + dy;
                    break;
                case 1:
                    mSettings.btnBOffsetX = mInitialOwnOfsX + dx;
                    mSettings.btnBOffsetY = mInitialOwnOfsY + dy;
                    break;
                case 2:
                    mSettings.btnZOffsetX = mInitialOwnOfsX + dx;
                    mSettings.btnZOffsetY = mInitialOwnOfsY + dy;
                    break;
                default:
                    mSettings.btnSpinOffsetX = mInitialOwnOfsX + dx;
                    mSettings.btnSpinOffsetY = mInitialOwnOfsY + dy;
                    break;
                }
            } else if (mDraggingCluster == 1) {
                mSettings.stickOffsetX = mInitialStickOfsX + (px - mDragStartX);
                mSettings.stickOffsetY = mInitialStickOfsY + (py - mDragStartY);
            } else if (mDraggingCluster == 2) {
                mSettings.buttonOffsetX = mInitialBtnOfsX + (px - mDragStartX);
                mSettings.buttonOffsetY = mInitialBtnOfsY + (py - mDragStartY);
            } else if (mDraggingCluster == 3) {
                mSettings.dpadOffsetX = mInitialDpadOfsX + (px - mDragStartX);
                mSettings.dpadOffsetY = mInitialDpadOfsY + (py - mDragStartY);
            } else if (mDraggingCluster == 4) {
                mSettings.starStickOffsetX = mInitialStarStickOfsX + (px - mDragStartX);
                mSettings.starStickOffsetY = mInitialStarStickOfsY + (py - mDragStartY);
            }
            mDirty = true;
            return true;
        } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
            if (mDraggingButton >= 0) {
                float ox = 0.0f, oy = 0.0f;
                switch (mDraggingButton) {
                case 0: ox = mSettings.btnAOffsetX; oy = mSettings.btnAOffsetY; break;
                case 1: ox = mSettings.btnBOffsetX; oy = mSettings.btnBOffsetY; break;
                case 2: ox = mSettings.btnZOffsetX; oy = mSettings.btnZOffsetY; break;
                default: ox = mSettings.btnSpinOffsetX; oy = mSettings.btnSpinOffsetY; break;
                }
                LOGI("layout edit: button %d now offset %.0f,%.0f", mDraggingButton, ox, oy);
            }
            mDraggingCluster = 0;
            mDraggingButton = -1;
            if (!mSavePath.empty()) mSettings.save(mSavePath);
            mDirty = true;
            return true;
        }
        return true;
    }

    // Handle Modern Side Sheet Drawer Touches (Tabbed & Scrollable)
    if (mSettingsOpen || mDrawerAnim > 0.05f) {
        float padX = 20.0f * uiScale;
        float contentW = drawerW - padX * 2.0f;
        float contentTop = 106.0f * uiScale;
        float contentBottom = h - 54.0f * uiScale;
        float totalContentH = getTabContentHeight(mSettingsTab, uiScale);
        float maxScroll = std::max(0.0f, totalContentH - (contentBottom - contentTop));

        // 1. Move Gesture for Smooth Scrolling
        if (action == AMOTION_EVENT_ACTION_MOVE && mDrawerScrollPointerId >= 0) {
            for (size_t i = 0; i < pointerCount; i++) {
                int32_t pid = AMotionEvent_getPointerId(event, i);
                if (pid == mDrawerScrollPointerId) {
                    float py = AMotionEvent_getY(event, i);
                    float deltaY = py - mDrawerTouchStartY;
                    if (fabsf(deltaY) > 6.0f * uiScale || mDrawerIsScrolling) {
                        mDrawerIsScrolling = true;
                        mDrawerScrollTarget = std::max(0.0f, std::min(mDrawerScrollStartOffset - deltaY, maxScroll));
                        mDirty = true;
                    }
                    return true;
                }
            }
        }

        // 2. Up / Pointer Up / Cancel: Finish Drag or Handle Row Tap
        if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
            int32_t liftedPid = AMotionEvent_getPointerId(event, pointerIndex);
            if (liftedPid == mDrawerScrollPointerId) {
                if (!mDrawerIsScrolling && action != AMOTION_EVENT_ACTION_CANCEL) {
                    float px = AMotionEvent_getX(event, pointerIndex);
                    float py = AMotionEvent_getY(event, pointerIndex);
                    float tapRelY = py - contentTop + mDrawerScrollY;

                    size_t count = 0;
                    const MenuRowDef* rows = getTabRows(mSettingsTab, count);
                    float curY = 8.0f * uiScale;
                    for (size_t i = 0; i < count; i++) {
                        const MenuRowDef& r = rows[i];
                        float rowH = r.heightDp * uiScale;
                        if (tapRelY >= curY && tapRelY < curY + rowH) {
                            // Hit row r!
                            float midX = drawerX + padX + contentW * 0.5f;
                            float relX = px - (drawerX + padX);

                            switch (r.action) {
                            case ACT_SHOW_CONTROLS:
                                mSettings.showControls = !mSettings.showControls;
                                showToast(mSettings.showControls ? "On-Screen Controls: ON" : "On-Screen Controls: HIDDEN");
                                break;
                            case ACT_LAYOUT_BUTTONS:
                                if (px < midX) {
                                    mSettingsOpen = false;
                                    mEditLayoutMode = true;
                                    LOGI("layout edit: opened from settings");
                                } else {
                                    clearLayoutOffsets();
                                    showToast("Controls layout reset to defaults");
                                }
                                break;
                            case ACT_CONTROLS_OPACITY: {
                                int idx = std::max(0, std::min(3, (int)(relX / (contentW / 4.0f))));
                                if (idx == 0) mSettings.opacity = 0.25f;
                                else if (idx == 1) mSettings.opacity = 0.50f;
                                else if (idx == 2) mSettings.opacity = 0.75f;
                                else mSettings.opacity = 1.00f;
                                break;
                            }
                            case ACT_BUTTON_SIZE: {
                                int idx = std::max(0, std::min(2, (int)(relX / (contentW / 3.0f))));
                                if (idx == 0) mSettings.scale = 0.85f;
                                else if (idx == 1) mSettings.scale = 1.00f;
                                else mSettings.scale = 1.25f;
                                break;
                            }
                            case ACT_JOYSTICK_MODE:
                                mSettings.dynamicStick = (px < midX);
                                showToast(mSettings.dynamicStick ? "Joystick: Dynamic Anchor" : "Joystick: Fixed Anchor");
                                break;
                            case ACT_STICK_SENSITIVITY: {
                                int idx = std::max(0, std::min(3, (int)(relX / (contentW / 4.0f))));
                                if (idx == 0) mSettings.stickSensitivity = 0.75f;
                                else if (idx == 1) mSettings.stickSensitivity = 1.00f;
                                else if (idx == 2) mSettings.stickSensitivity = 1.25f;
                                else mSettings.stickSensitivity = 1.50f;
                                char msg[64];
                                snprintf(msg, sizeof(msg), "Movement Sensitivity: %.0f%%", mSettings.stickSensitivity * 100.0f);
                                showToast(msg);
                                break;
                            }
                            case ACT_TOUCH_CAMERA:
                                mSettings.touchCamera = !mSettings.touchCamera;
                                showToast(mSettings.touchCamera ? "360 Touch Camera: ON (Swipe right screen)" : "360 Touch Camera: OFF");
                                break;
                            case ACT_STAR_STICK:
                                mSettings.starStick = !mSettings.starStick;
                                showToast(mSettings.starStick ? "2nd Joystick (Star Pointer): ON" : "2nd Joystick (Star Pointer): OFF");
                                break;
                            case ACT_STAR_STICK_SENSITIVITY: {
                                int idx = std::max(0, std::min(4, (int)(relX / (contentW / 5.0f))));
                                if (idx == 0) mSettings.starStickSensitivity = 0.50f;
                                else if (idx == 1) mSettings.starStickSensitivity = 0.75f;
                                else if (idx == 2) mSettings.starStickSensitivity = 1.00f;
                                else if (idx == 3) mSettings.starStickSensitivity = 1.50f;
                                else mSettings.starStickSensitivity = 2.00f;
                                char msg[64];
                                snprintf(msg, sizeof(msg), "Star Stick Sensitivity: %.0f%%", mSettings.starStickSensitivity * 100.0f);
                                showToast(msg);
                                break;
                            }
                            case ACT_REPOSITION_STAR_STICK:
                                if (px < midX) {
                                    mSettingsOpen = false;
                                    mEditLayoutMode = true;
                                    showToast("Drag Star Joystick to any position, then tap SAVE");
                                } else {
                                    mSettings.starStickOffsetX = 0.0f;
                                    mSettings.starStickOffsetY = 0.0f;
                                    if (!mSavePath.empty()) mSettings.save(mSavePath);
                                    showToast("Star Joystick reset to default position above buttons");
                                }
                                break;
                            case ACT_RIGHT_STICK_MODE:
                                mSettings.rightStickMode = (px >= midX) ? 1 : 0;
                                showToast(mSettings.rightStickMode == 1 ? "Right Stick: Star Pointer" : "Right Stick: 360 Camera Orbit");
                                break;
                            case ACT_SHOW_DPAD:
                                mSettings.showDpad = !mSettings.showDpad;
                                showToast(mSettings.showDpad ? "Camera D-Pad: ON" : "Camera D-Pad: OFF");
                                break;
                            case ACT_DPAD_SIDE:
                                mSettings.dpadOnRight = (px >= midX);
                                mSettings.dpadOffsetX = 0.0f;
                                mSettings.dpadOffsetY = 0.0f;
                                showToast(mSettings.dpadOnRight ? "Camera D-Pad: Right Side" : "Camera D-Pad: Left Side");
                                break;
                            case ACT_GYRO_AIMING:
                                mSettings.gyroAiming = !mSettings.gyroAiming;
                                showToast(mSettings.gyroAiming ? "Gyro Aiming: ON (Tilt device to aim)" : "Gyro Aiming: OFF");
                                break;
                            case ACT_GYRO_INVERT_BUTTONS:
                                if (px < midX) {
                                    mSettings.gyroInvertX = !mSettings.gyroInvertX;
                                    showToast(mSettings.gyroInvertX ? "Gyro Invert Horizontal (X): ON" : "Gyro Invert Horizontal (X): OFF");
                                } else {
                                    mSettings.gyroInvertY = !mSettings.gyroInvertY;
                                    showToast(mSettings.gyroInvertY ? "Gyro Invert Vertical (Y): ON" : "Gyro Invert Vertical (Y): OFF");
                                }
                                break;
                            case ACT_RELATIVE_POINTER:
                                mSettings.relativePointer = !mSettings.relativePointer;
                                showToast(mSettings.relativePointer ? "Relative Pointer: ON (Trackpad mode)" : "Relative Pointer: OFF (Direct mode)");
                                break;
                            case ACT_FULL_TOUCH_MODE:
                                mSettings.fullTouchMode = !mSettings.fullTouchMode;
                                showToast(mSettings.fullTouchMode ? "Full Touch Mode: ON" : "Full Touch Mode: OFF");
                                break;
                            case ACT_HAPTICS:
                                mSettings.haptics = !mSettings.haptics;
                                showToast(mSettings.haptics ? "Vibration Haptics: ON" : "Vibration Haptics: OFF");
                                break;
                            // Graphics Tab
                            case ACT_GRAPHICS_BACKEND:
                                if (px < midX) {
                                    mSettings.graphicsBackend = 0;
                                    showToast("Backend: OpenGL ES 3.2 (Active)");
                                } else {
                                    mSettings.graphicsBackend = 1;
                                    showToast("Backend: Vulkan 1.0 (Core Engine)");
                                }
                                break;
                            case ACT_RENDER_SCALE: {
                                int idx = std::max(0, std::min(3, (int)(relX / (contentW / 4.0f))));
                                mSettings.renderScale = idx;
                                showToast(idx == 0 ? "Resolution: 480p Native (Fastest)" :
                                          idx == 1 ? "Resolution: 720p HD (Balanced)" :
                                          idx == 2 ? "Resolution: 1080p FHD (Sharp)" : "Resolution: 1440p 2K (Ultra HD)");
                                break;
                            }
                            case ACT_TARGET_FPS: {
                                int idx = std::max(0, std::min(4, (int)(relX / (contentW / 5.0f))));
                                int fps = (idx == 0) ? 30 : (idx == 1) ? 60 : (idx == 2) ? 90 : (idx == 3) ? 120 : 144;
                                mSettings.targetFps = fps;
                                mSettings.batterySaver = (fps <= 30);
                                applyTargetFps(fps);
                                showToast(("Target Framerate: " + std::to_string(fps) + " FPS").c_str());
                                break;
                            }
                            case ACT_DISPLAY_REFRESH: {
                                int idx = std::max(0, std::min(3, (int)(relX / (contentW / 4.0f))));
                                int fps = (idx == 0) ? 60 : (idx == 1) ? 90 : (idx == 2) ? 120 : 144;
                                mSettings.targetFps = fps;
                                mSettings.batterySaver = false;
                                applyTargetFps(fps);
                                showToast(("Display Refresh Rate: " + std::to_string(fps) + " Hz").c_str());
                                break;
                            }
                            case ACT_BATTERY_SAVER:
                                mSettings.batterySaver = !mSettings.batterySaver;
                                if (mSettings.batterySaver) {
                                    mSettings.targetFps = 30;
                                    applyTargetFps(30);
                                } else {
                                    mSettings.targetFps = 60;
                                    applyTargetFps(60);
                                }
                                showToast(mSettings.batterySaver ? "Battery Saver: ON (30 FPS Lock)" : "Battery Saver: OFF (Performance Mode)");
                                break;
                            case ACT_FULLSCREEN:
                                mSettings.fullscreen = !mSettings.fullscreen;
                                showToast(mSettings.fullscreen ? "Fullscreen Ultrawide: ON (Hor+ FOV)" : "Fullscreen: OFF (Standard 16:9)");
                                break;
                            case ACT_ORIENTATION: {
                                int idx = std::max(0, std::min(2, (int)(relX / (contentW / 3.0f))));
                                mSettings.screenOrientationChoice = idx;
                                showToast(idx == 0 ? "Orientation: Auto Sensor (Flips 180°)" :
                                          idx == 1 ? "Orientation: Fixed Landscape" : "Orientation: Fixed Reverse Landscape");
                                break;
                            }
                            case ACT_PERF_HUD: {
                                int idx = std::max(0, std::min(2, (int)(relX / (contentW / 3.0f))));
                                mSettings.perfHudMode = idx;
                                showToast(idx == 0 ? "Performance HUD: OFF" : idx == 1 ? "Performance HUD: FPS Only" : "Performance HUD: Full Hardware Stats");
                                break;
                            }
                            case ACT_VULKAN_POSTPROCESS: {
                                int cur = port_is_post_processing_enabled();
                                port_set_post_processing_enabled(cur ? 0 : 1);
                                showToast(cur ? "Vulkan Post-Processing: OFF" : "Vulkan FXAA & Cosmic Bloom: ENABLED");
                                break;
                            }
                            // Mods Tab - Hero & Moveset
                            case ACT_CHARACTER_SELECT: {
                                int idx = std::max(0, std::min(3, (int)(relX / (contentW / 4.0f))));
                                mSettings.characterChoice = idx;
                                port_set_character_choice(idx);
                                const char* names[] = {"Mario (Standard)", "Luigi (High Jump, Slippery)", "Peach (Hover Float)", "Rosalina (Cosmic Pull)"};
                                showToast(std::string("Playable Hero: ") + names[idx]);
                                break;
                            }
                            case ACT_ODYSSEY_MOVESET: {
                                mSettings.odysseyMoveset = !mSettings.odysseyMoveset;
                                port_set_odyssey_moveset(mSettings.odysseyMoveset ? 1 : 0);
                                showToast(mSettings.odysseyMoveset ? "Odyssey Moveset: ON (Dive ZL+Y, Roll, Jump Boost)" : "Odyssey Moveset: OFF");
                                break;
                            }
                            case ACT_COOP_MODE: {
                                mSettings.coopMode = !mSettings.coopMode;
                                port_set_coop_mode(mSettings.coopMode ? 1 : 0);
                                showToast(mSettings.coopMode ? "2P Co-Op Active Assist: ON" : "2P Co-Op Active Assist: OFF");
                                break;
                            }
                            case ACT_SAVESTATE_ACTIONS: {
                                if (px < midX) {
                                    port_trigger_savestate_save();
                                    if (mSettings.haptics) port_android_vibrate(50);
                                    showToast("Instant 0ms Savestate Snapshot Saved!");
                                } else {
                                    if (port_savestate_has_data()) {
                                        port_trigger_savestate_load();
                                        if (mSettings.haptics) port_android_vibrate(75);
                                        showToast("Instant 0ms Savestate Restored!");
                                    } else {
                                        showToast("No savestate in memory! Tap Save State first.");
                                    }
                                }
                                break;
                            }
                            case ACT_SPEEDRUN_HUD: {
                                mSettings.speedrunHud = !mSettings.speedrunHud;
                                port_set_speedrun_hud_enabled(mSettings.speedrunHud ? 1 : 0);
                                showToast(mSettings.speedrunHud ? "Speedrun Practice HUD: ON" : "Speedrun Practice HUD: OFF");
                                break;
                            }
                            case ACT_SHIELD_TEXTURES:
                                mSettings.modShieldTextures = !mSettings.modShieldTextures;
                                showToast(mSettings.modShieldTextures ? "Shield HD Textures: ENABLED (1080p)" : "Shield HD Textures: DISABLED");
                                break;
                            case ACT_SHIELD_CUTSCENES:
                                mSettings.modShieldCutscenes = !mSettings.modShieldCutscenes;
                                showToast(mSettings.modShieldCutscenes ? "Shield 60FPS Cutscenes: ENABLED" : "Shield 60FPS Cutscenes: DISABLED");
                                break;
                            case ACT_SHIELD_CONTROLS:
                                mSettings.modShieldControls = !mSettings.modShieldControls;
                                showToast(mSettings.modShieldControls ? "Shield Calibrated Analog: ENABLED" : "Shield Calibrated Analog: OFF");
                                break;
                            case ACT_CLASSIC_GAMEPAD:
                                mSettings.modClassicGamepadUI = !mSettings.modClassicGamepadUI;
                                showToast(mSettings.modClassicGamepadUI ? "Modern Gamepad ABXY Prompts: ENABLED" : "Modern Gamepad Prompts: DISABLED");
                                break;
                            case ACT_GAME_SPEED: {
                                int idx = std::max(0, std::min(4, (int)(relX / (contentW / 5.0f))));
                                mSettings.gameSpeed = idx + 1;
                                applyGameSpeed(mSettings.gameSpeed);
                                showToast(mSettings.gameSpeed == 1 ? "Game Speed: 1.0x (Normal)" :
                                          mSettings.gameSpeed == 2 ? "Game Speed: 1.5x" :
                                          mSettings.gameSpeed == 3 ? "Game Speed: 2.0x (Fast)" :
                                          mSettings.gameSpeed == 4 ? "Game Speed: 3.0x" : "Game Speed: 4.0x (Turbo)");
                                break;
                            }
                            case ACT_FLY_MODE:
                                mSettings.modFly = !mSettings.modFly;
                                showToast(mSettings.modFly ? "Fly Mode: ON (Hold A to ascend, Z to descend)" : "Fly Mode: OFF");
                                break;
                            case ACT_SKIP_CUTSCENES:
                                mSettings.modSkipCutscenes = !mSettings.modSkipCutscenes;
                                showToast(mSettings.modSkipCutscenes ? "Skip Cutscenes: ON" : "Skip Cutscenes: OFF");
                                break;
                            case ACT_INFINITE_HEALTH:
                                mSettings.modInfiniteHealth = !mSettings.modInfiniteHealth;
                                showToast(mSettings.modInfiniteHealth ? "Infinite Health: ON (God Mode)" : "Infinite Health: OFF");
                                break;
                            case ACT_INFINITE_LIVES:
                                mSettings.modInfiniteLives = !mSettings.modInfiniteLives;
                                showToast(mSettings.modInfiniteLives ? "Infinite Lives (99): ON" : "Infinite Lives: OFF");
                                break;
                            case ACT_INFINITE_STARBITS:
                                mSettings.modInfiniteStarBits = !mSettings.modInfiniteStarBits;
                                showToast(mSettings.modInfiniteStarBits ? "Infinite Star Bits (999): ON" : "Infinite Star Bits: OFF");
                                break;
                            case ACT_RIIVOLUTION:
                                mSettings.modRiivolution = !mSettings.modRiivolution;
                                showToast(mSettings.modRiivolution ? "Riivolution Mods: ON (/sdcard/GalaxyData/mods)" : "Riivolution Mods: OFF");
                                break;
                            case ACT_RELOAD_MODS:
                                mkdir("/sdcard/GalaxyData", 0775);
                                mkdir("/sdcard/GalaxyData/mods", 0775);
                                showToast("Scanned /sdcard/GalaxyData/mods: Active!");
                                break;
                            // Save & System Tab
                            case ACT_QUICK_CARDS:
                                if (px < midX) triggerSaveSlot(4);
                                else triggerLoadSlot(4);
                                break;
                            case ACT_SELECT_SLOT: {
                                int idx = std::max(0, std::min(3, (int)(relX / (contentW / 4.0f))));
                                mCurrentSlot = idx + 1;
                                showToast(mCurrentSlot == 4 ? "Active Slot: Quick Slot" : ("Active Slot: Slot " + std::to_string(mCurrentSlot)).c_str());
                                break;
                            }
                            case ACT_SLOT_ACTIONS:
                                if (px < midX) triggerSaveSlot(mCurrentSlot);
                                else triggerLoadSlot(mCurrentSlot);
                                break;
                            case ACT_SD_BACKUP_ACTIONS:
                                if (px < midX) exportSave();
                                else importSave();
                                break;
                            case ACT_LANGUAGE: {
                                int idx = std::max(0, std::min(5, (int)(relX / (contentW / 6.0f))));
                                mSettings.languageChoice = idx;
                                applyLanguage(mSettings.languageChoice);
                                showToast(mSettings.languageChoice == 0 ? "Language: System Auto" :
                                          mSettings.languageChoice == 1 ? "Language: English" :
                                          mSettings.languageChoice == 2 ? "Language: Español" :
                                          mSettings.languageChoice == 3 ? "Language: Français" :
                                          mSettings.languageChoice == 4 ? "Language: Deutsch" : "Language: Italiano");
                                break;
                            }
                            case ACT_VIEW_LOGS:
                                mSettingsOpen = false;
                                mLogViewerOpen = true;
                                mDirty = true;
                                break;
                            case ACT_EXIT_LAUNCHER:
                                mSettingsOpen = false;
                                if (!mSavePath.empty()) mSettings.save(mSavePath);
                                port_android_exit_to_launcher();
                                return true;
                            default:
                                break;
                            }

                            if (!mSavePath.empty()) mSettings.save(mSavePath);
                            mDirty = true;
                            break;
                        }
                        curY += rowH + 6.0f * uiScale;
                    }
                }
                mDrawerScrollPointerId = -1;
                mDrawerIsScrolling = false;
                mDirty = true;
                return true;
            }
        }

        // 3. Down / Pointer Down: Fixed Buttons, Tabs, or Start Content Scroll
        if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            float px = AMotionEvent_getX(event, pointerIndex);
            float py = AMotionEvent_getY(event, pointerIndex);
            int32_t pid = AMotionEvent_getPointerId(event, pointerIndex);

            // Outside drawer tap (scrim) closes settings
            if (px > drawerX + drawerW) {
                mSettingsOpen = false;
                if (!mSavePath.empty()) mSettings.save(mSavePath);
                mDirty = true;
                return true;
            }

            // Close [X] button at top-right of drawer
            float closeBtnX = drawerX + drawerW - 32.0f * uiScale;
            float closeBtnY = 32.0f * uiScale;
            float dx = px - closeBtnX;
            float dy = py - closeBtnY;
            if (dx * dx + dy * dy <= 28.0f * uiScale * 28.0f * uiScale) {
                mSettingsOpen = false;
                if (!mSavePath.empty()) mSettings.save(mSavePath);
                mDirty = true;
                return true;
            }

            // Tab Bar Tap (Controls | Graphics | Mods | Save/Sys)
            if (py >= 52.0f * uiScale && py <= 98.0f * uiScale) {
                float segW = contentW / 4.0f;
                int newTab = (int)((px - (drawerX + padX)) / segW);
                newTab = std::max(0, std::min(3, newTab));
                if (newTab != mSettingsTab) {
                    mSettingsTab = newTab;
                    mDrawerScrollY = 0.0f;
                    mDrawerScrollTarget = 0.0f;
                }
                mDirty = true;
                return true;
            }

            // Fixed [ Close Menu ] button at bottom
            if (py >= contentBottom) {
                mSettingsOpen = false;
                if (!mSavePath.empty()) mSettings.save(mSavePath);
                mDirty = true;
                return true;
            }

            // Content Viewport Touch Acquisition
            if (py >= contentTop && py < contentBottom) {
                mDrawerScrollPointerId = pid;
                mDrawerTouchStartY = py;
                mDrawerTouchStartX = px;
                mDrawerScrollStartOffset = mDrawerScrollY;
                mDrawerIsScrolling = false;
                return true;
            }

            return true;
        }
        return true;
    }

    // -------------------------------------------------------------
    // Live Virtual Controls Handling
    // -------------------------------------------------------------
    pad.connected = 1;
    uint32_t touchButtons = 0;

    // Reset button press highlights
    mBtnAPressed = false;
    mBtnBPressed = false;
    mBtnZPressed = false;
    mBtnSpinPressed = false;
    mBtnPlusPressed = false;
    mBtnDpadLeft = false;
    mBtnDpadRight = false;
    mBtnDpadUp = false;
    mBtnDpadDown = false;

    // Button layout positions - shared with the renderer and the layout editor
    ButtonLayout bl = buttonLayout(screenW, screenH);
    float aX = bl.ax, aY = bl.ay, aR = bl.ar;
    float bX = bl.bx, bY = bl.by, bR = bl.br;
    float spinX = bl.spinX, spinY = bl.spinY, spinR = bl.spinR;
    float zX = bl.zx, zY = bl.zy, zR = bl.zr;
    // Plus (+) - Top right
    float plusX = w - 65.0f * scale, plusY = 65.0f * scale, plusR = 34.0f * scale;

    // Camera D-Pad position
    float baseDpadX = mSettings.dpadOnRight ? (w - 140.0f * scale) : (130.0f * scale);
    float baseDpadY = mSettings.dpadOnRight ? (170.0f * scale) : (160.0f * scale);
    float dpadX = baseDpadX + mSettings.dpadOffsetX;
    float dpadY = baseDpadY + mSettings.dpadOffsetY;
    float dpadTotalR = 58.0f * scale;

    // Stick default position - Bottom left thumb area
    float defaultStickX = 175.0f * scale + mSettings.stickOffsetX;
    float defaultStickY = h - 175.0f * scale + mSettings.stickOffsetY;
    float stickBaseR = 95.0f * scale;
    float stickMaxR = 100.0f * scale;

    // 1. Complete release: when all fingers are lifted or gesture canceled
    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        for (int i = 0; i < 10; i++) {
            mTouches[i] = {};
        }
        mStickPointerId = -1;
        mStickKnobX = mStickBaseX;
        mStickKnobY = mStickBaseY;
        mTouchStickX = 0.0f;
        mTouchStickY = 0.0f;
        pad.stickX = 0.0f;
        pad.stickY = 0.0f;
        touchButtons = 0;
        pad.buttons &= ~0x3C1F; // Only clear touch bits, preserve physical controller buttons
        mFingerDownOnA = false;
        mFingerDownOnB = false;
        mFingerDownOnZ = false;
        mFingerDownOnSpin = false;
        mFingerDownOnPlus = false;
        mBtnAPressed = mBtnBPressed = mBtnZPressed = mBtnSpinPressed = mBtnPlusPressed = false;
        mBtnDpadLeft = mBtnDpadRight = mBtnDpadUp = mBtnDpadDown = false;
        mDirty = true;

        if (mTouchPointerId >= 0) {
            int64_t now = currentTimeMs();
            int64_t dur = now - mTouchStartTime;
            float px = AMotionEvent_getX(event, pointerIndex);
            float py = AMotionEvent_getY(event, pointerIndex);
            float fdx = px - mTouchStartX;
            float fdy = py - mTouchStartY;
            bool isTitle = (port_is_title_screen() != 0);
            if (isTitle && dur < 350 && (fdx * fdx + fdy * fdy <= 45.0f * uiScale * 45.0f * uiScale)) {
                mTapDelayFrames = 0;
                mTapPendingFrames = 6;
            }
            mTouchPointerId = -1;
        }

        pad.pointerValid = 1;
        pad.pointerX = mPointerX;
        pad.pointerY = mPointerY;
        pad.pointerDist = 2.0f;
        return true;
    }

    // 2. Single finger release during multi-touch
    if (action == AMOTION_EVENT_ACTION_POINTER_UP) {
        int32_t liftedPid = AMotionEvent_getPointerId(event, pointerIndex);
        for (int i = 0; i < 10; i++) {
            if (mTouches[i].id == liftedPid) {
                if (mTouches[i].role == TouchRole::STICK) {
                    mStickPointerId = -1;
                    mStickKnobX = mStickBaseX;
                    mStickKnobY = mStickBaseY;
                    mTouchStickX = 0.0f;
                    mTouchStickY = 0.0f;
                    pad.stickX = 0.0f;
                    pad.stickY = 0.0f;
                } else if (mTouches[i].role == TouchRole::STAR_STICK) {
                    mStarStickPointerId = -1;
                    mStarStickKnobX = mStarStickBaseX;
                    mStarStickKnobY = mStarStickBaseY;
                    mStarStickX = 0.0f;
                    mStarStickY = 0.0f;
                } else if (mTouches[i].role == TouchRole::CAMERA_SWIPE) {
                    float px = AMotionEvent_getX(event, pointerIndex);
                    float py = AMotionEvent_getY(event, pointerIndex);
                    int64_t dur = currentTimeMs() - mTouches[i].startTime;
                    float fdx = px - mTouches[i].startX;
                    float fdy = py - mTouches[i].startY;
                    if (dur < 220 && (fdx * fdx + fdy * fdy <= 30.0f * scale * 30.0f * scale)) {
                        if (vpW > 0 && vpH > 0) {
                            mPointerX = ((px - vpX) / (float)vpW) * 2.0f - 1.0f;
                            mPointerY = ((py - vpY) / (float)vpH) * 2.0f - 1.0f;
                            mPointerX = std::max(-1.0f, std::min(1.0f, mPointerX));
                            mPointerY = std::max(-1.0f, std::min(1.0f, mPointerY));
                        }
                    }
                    pad.rightStickX = 0.0f;
                    pad.rightStickY = 0.0f;
                }
                mTouches[i] = {};
            }
        }
        if (liftedPid == mTouchPointerId) {
            bool isTitle = (port_is_title_screen() != 0);
            if (isTitle) {
                int64_t now = currentTimeMs();
                int64_t dur = now - mTouchStartTime;
                float px = AMotionEvent_getX(event, pointerIndex);
                float py = AMotionEvent_getY(event, pointerIndex);
                float fdx = px - mTouchStartX;
                float fdy = py - mTouchStartY;
                if (dur < 350 && (fdx * fdx + fdy * fdy <= 45.0f * uiScale * 45.0f * uiScale)) {
                    mTapPendingFrames = 6;
                }
            }
            mTouchPointerId = -1;
        }
        mDirty = true;
    }

    // 3. New Touch Down (Assign dedicated role to this finger)
    if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        float px = AMotionEvent_getX(event, pointerIndex);
        float py = AMotionEvent_getY(event, pointerIndex);
        int32_t pid = AMotionEvent_getPointerId(event, pointerIndex);

        int slot = -1;
        for (int i = 0; i < 10; i++) {
            if (mTouches[i].id == pid) { slot = i; break; }
            if (slot < 0 && mTouches[i].id < 0) { slot = i; }
        }
        if (slot >= 0) {
            mTouches[slot].id = pid;
            mTouches[slot].startX = px;
            mTouches[slot].startY = py;
            mTouches[slot].startTime = currentTimeMs();
            mTouches[slot].role = TouchRole::NONE;

            // Candidate check for Action Buttons (A, B, Z, SPIN, PLUS)
            int bestBtnIdx = -1;
            if (mSettings.showControls) {
                struct BtnCandidate {
                    TouchRole role;
                    float distSq;
                    float maxDistSq;
                };
                BtnCandidate candidates[5] = {
                    {TouchRole::BTN_A, (px - aX)*(px - aX) + (py - aY)*(py - aY), (aR * 1.15f) * (aR * 1.15f)},
                    {TouchRole::BTN_B, (px - bX)*(px - bX) + (py - bY)*(py - bY), (bR * 1.15f) * (bR * 1.15f)},
                    {TouchRole::BTN_Z, (px - zX)*(px - zX) + (py - zY)*(py - zY), (zR * 1.15f) * (zR * 1.15f)},
                    {TouchRole::BTN_SPIN, (px - spinX)*(px - spinX) + (py - spinY)*(py - spinY), (spinR * 1.15f) * (spinR * 1.15f)},
                    {TouchRole::BTN_PLUS, (px - plusX)*(px - plusX) + (py - plusY)*(py - plusY), (plusR * 1.25f) * (plusR * 1.25f)},
                };

                float bestNormDist = 1.0f;
                for (int b = 0; b < 5; b++) {
                    float normDist = candidates[b].distSq / candidates[b].maxDistSq;
                    if (normDist <= 1.0f && normDist < bestNormDist) {
                        bestNormDist = normDist;
                        bestBtnIdx = b;
                    }
                }
            }

            // Star Stick check (mappable anywhere on screen)
            StarStickLayout starL = starStickLayout(screenW, screenH);
            float d2Star = (px - starL.cx) * (px - starL.cx) + (py - starL.cy) * (py - starL.cy);
            float starHitR = starL.r * 1.45f;
            bool starEligible = (mSettings.starStick && mStarStickPointerId < 0 && d2Star <= starHitR * starHitR);

            // Left Movement Stick acquisition: bottom-left thumb zone
            bool stickEligible = false;
            if (mSettings.showControls && mStickPointerId < 0 && !starEligible) {
                if (mSettings.dynamicStick) {
                    stickEligible = (px <= w * 0.38f && py >= h * 0.45f);
                } else {
                    float d2 = (px - defaultStickX) * (px - defaultStickX) + (py - defaultStickY) * (py - defaultStickY);
                    stickEligible = (d2 <= (stickBaseR * 1.5f) * (stickBaseR * 1.5f));
                }
            }

            if (bestBtnIdx >= 0) {
                TouchRole btnRoles[5] = {TouchRole::BTN_A, TouchRole::BTN_B, TouchRole::BTN_Z, TouchRole::BTN_SPIN, TouchRole::BTN_PLUS};
                mTouches[slot].role = btnRoles[bestBtnIdx];
                mDirty = true;
            } else if (starEligible) {
                // 2nd Joystick (Star Pointer)
                mTouches[slot].role = TouchRole::STAR_STICK;
                mStarStickPointerId = pid;
                mStarStickBaseX = starL.cx;
                mStarStickBaseY = starL.cy;
                mStarStickKnobX = px;
                mStarStickKnobY = py;
                mDirty = true;
            } else if (stickEligible) {
                // Movement Joystick
                mTouches[slot].role = TouchRole::STICK;
                mStickPointerId = pid;
                if (mSettings.dynamicStick) {
                    mStickBaseX = px;
                    mStickBaseY = py;
                } else {
                    mStickBaseX = defaultStickX;
                    mStickBaseY = defaultStickY;
                }
                mStickKnobX = px;
                mStickKnobY = py;
                mDirty = true;
            } else if (mSettings.showControls && mSettings.showDpad &&
                     ((px - dpadX)*(px - dpadX) + (py - dpadY)*(py - dpadY) <= dpadTotalR * dpadTotalR)) {
                // Camera D-Pad
                mTouches[slot].role = TouchRole::DPAD;
                mDirty = true;
            } else if (mSettings.touchCamera && px >= w * 0.38f) {
                // 360 Touch Camera Swipe on right screen
                mTouches[slot].role = TouchRole::CAMERA_SWIPE;
                mTouches[slot].lastX = px;
                mTouches[slot].lastY = py;
                mDirty = true;
            } else {
                // Star Pointer tap / direct coordinate target
                mTouches[slot].role = TouchRole::STAR_POINTER;
            }

            if (mTouches[slot].role == TouchRole::STAR_POINTER) {
                if (mTouchPointerId < 0) {
                    mTouchPointerId = pid;
                    mTouchStartTime = currentTimeMs();
                    mTouchStartX = px;
                    mTouchStartY = py;
                }
                mTouches[slot].lastX = px;
                mTouches[slot].lastY = py;
                if (!mSettings.relativePointer) {
                    if (vpW > 0 && vpH > 0) {
                        mPointerX = ((px - vpX) / (float)vpW) * 2.0f - 1.0f;
                        mPointerY = ((py - vpY) / (float)vpH) * 2.0f - 1.0f;
                        mPointerX = std::max(-1.0f, std::min(1.0f, mPointerX));
                        mPointerY = std::max(-1.0f, std::min(1.0f, mPointerY));
                    }
                }
            } else if (mTouches[slot].role == TouchRole::CAMERA_SWIPE) {
                mTouches[slot].lastX = px;
                mTouches[slot].lastY = py;
            }
        }
    }

    // 4. Update state from active pointers
    mFingerDownOnA = false;
    mFingerDownOnB = false;
    mFingerDownOnZ = false;
    mFingerDownOnSpin = false;
    mFingerDownOnPlus = false;
    mBtnDpadLeft = false;
    mBtnDpadRight = false;
    mBtnDpadUp = false;
    mBtnDpadDown = false;
    bool stickFound = false;
    bool starStickFound = false;
    bool cameraSwipeFound = false;

    for (size_t i = 0; i < pointerCount; i++) {
        if (action == AMOTION_EVENT_ACTION_POINTER_UP && (int32_t)i == pointerIndex) {
            continue;
        }

        int32_t pid = AMotionEvent_getPointerId(event, i);
        float px = AMotionEvent_getX(event, i);
        float py = AMotionEvent_getY(event, i);

        TouchRole role = TouchRole::NONE;
        int touchSlot = -1;
        for (int s = 0; s < 10; s++) {
            if (mTouches[s].id == pid) {
                role = mTouches[s].role;
                touchSlot = s;
                break;
            }
        }

        if (role == TouchRole::STICK) {
            stickFound = true;
            float dx = px - mStickBaseX;
            float dy = py - mStickBaseY;
            float dist = sqrtf(dx * dx + dy * dy);
            float deadzone = 16.0f * scale;

            if (dist > stickMaxR && dist > 0.001f) {
                mStickKnobX = mStickBaseX + (dx / dist) * stickMaxR;
                mStickKnobY = mStickBaseY + (dy / dist) * stickMaxR;
                mTouchStickX = (dx / dist) * mSettings.stickSensitivity;
                mTouchStickY = (-dy / dist) * mSettings.stickSensitivity;
            } else if (dist > deadzone) {
                mStickKnobX = px;
                mStickKnobY = py;
                float norm = (dist - deadzone) / (stickMaxR - deadzone);
                mTouchStickX = ((dx / dist) * norm) * mSettings.stickSensitivity;
                mTouchStickY = (-(dy / dist) * norm) * mSettings.stickSensitivity;
            } else {
                mStickKnobX = px;
                mStickKnobY = py;
                mTouchStickX = 0.0f;
                mTouchStickY = 0.0f;
            }
            mTouchStickX = std::max(-1.0f, std::min(1.0f, mTouchStickX));
            mTouchStickY = std::max(-1.0f, std::min(1.0f, mTouchStickY));
            pad.stickX = mTouchStickX;
            pad.stickY = mTouchStickY;
        }
        else if (role == TouchRole::DPAD) {
            float dDpadX = px - dpadX;
            float dDpadY = py - dpadY;
            float distDpad = sqrtf(dDpadX * dDpadX + dDpadY * dDpadY);
            float deadzone = 8.0f * scale;
            if (distDpad > deadzone && distDpad <= dpadTotalR * 1.35f) {
                if (dDpadX < -deadzone && fabsf(dDpadX) > fabsf(dDpadY) * 0.40f) {
                    touchButtons |= 0x0001; // WPAD_BUTTON_LEFT
                    mBtnDpadLeft = true;
                } else if (dDpadX > deadzone && fabsf(dDpadX) > fabsf(dDpadY) * 0.40f) {
                    touchButtons |= 0x0002; // WPAD_BUTTON_RIGHT
                    mBtnDpadRight = true;
                }
                if (dDpadY < -deadzone && fabsf(dDpadY) > fabsf(dDpadX) * 0.40f) {
                    touchButtons |= 0x0008; // WPAD_BUTTON_UP
                    mBtnDpadUp = true;
                } else if (dDpadY > deadzone && fabsf(dDpadY) > fabsf(dDpadX) * 0.40f) {
                    touchButtons |= 0x0004; // WPAD_BUTTON_DOWN
                    mBtnDpadDown = true;
                }
            }
        }
        else if (role == TouchRole::BTN_A) {
            touchButtons |= 0x0800; // WPAD_BUTTON_A
            if (!mBtnAPressed && mSettings.haptics) port_android_vibrate(22);
            mBtnAPressed = true;
            mFingerDownOnA = true;
        }
        else if (role == TouchRole::BTN_B) {
            // Nintendo Switch: B is Jump (also handles cancel/shoot)
            touchButtons |= 0x0800; // WPAD_BUTTON_A (Jump)
            touchButtons |= 0x0400; // WPAD_BUTTON_B (Cancel / Shoot)
            if (!mBtnBPressed && mSettings.haptics) port_android_vibrate(18);
            mBtnBPressed = true;
            mFingerDownOnB = true;
        }
        else if (role == TouchRole::BTN_Z) {
            touchButtons |= 0x2000; // WPAD_BUTTON_Z
            if (!mBtnZPressed && mSettings.haptics) port_android_vibrate(20);
            mBtnZPressed = true;
            mFingerDownOnZ = true;
        }
        else if (role == TouchRole::BTN_SPIN) {
            spinFrames = 6;
            if (!mBtnSpinPressed && mSettings.haptics) port_android_vibrate(35);
            mBtnSpinPressed = true;
            mFingerDownOnSpin = true;
        }
        else if (role == TouchRole::BTN_PLUS) {
            touchButtons |= 0x0010; // WPAD_BUTTON_PLUS
            if (!mBtnPlusPressed && mSettings.haptics) port_android_vibrate(25);
            mBtnPlusPressed = true;
            mFingerDownOnPlus = true;
        }
        else if (role == TouchRole::STAR_STICK) {
            starStickFound = true;
            float dx = px - mStarStickBaseX;
            float dy = py - mStarStickBaseY;
            float dist = sqrtf(dx * dx + dy * dy);
            float deadzone = 10.0f * scale;

            if (dist > stickMaxR && dist > 0.001f) {
                mStarStickKnobX = mStarStickBaseX + (dx / dist) * stickMaxR;
                mStarStickKnobY = mStarStickBaseY + (dy / dist) * stickMaxR;
                mStarStickX = dx / dist;
                mStarStickY = -dy / dist;
            } else if (dist > deadzone) {
                mStarStickKnobX = px;
                mStarStickKnobY = py;
                float norm = (dist - deadzone) / (stickMaxR - deadzone);
                mStarStickX = (dx / dist) * norm;
                mStarStickY = -(dy / dist) * norm;
            } else {
                mStarStickKnobX = px;
                mStarStickKnobY = py;
                mStarStickX = 0.0f;
                mStarStickY = 0.0f;
            }
            mStarStickX = std::max(-1.0f, std::min(1.0f, mStarStickX));
            mStarStickY = std::max(-1.0f, std::min(1.0f, mStarStickY));

            // Smoothly steer star pointer across the screen
            float slewSpeed = 0.038f * mSettings.starStickSensitivity;
            mPointerX = std::max(-1.0f, std::min(1.0f, mPointerX + mStarStickX * slewSpeed));
            mPointerY = std::max(-1.0f, std::min(1.0f, mPointerY - mStarStickY * slewSpeed));
        }
        else if (role == TouchRole::CAMERA_SWIPE) {
            cameraSwipeFound = true;
            if (touchSlot >= 0) {
                float dx = px - mTouches[touchSlot].lastX;
                float dy = py - mTouches[touchSlot].lastY;
                mTouches[touchSlot].lastX = px;
                mTouches[touchSlot].lastY = py;

                // Scale swipe deltas into rightStick orbital rate (smooth 360 camera rotation)
                float sens = 0.055f * mSettings.starStickSensitivity;
                pad.rightStickX = std::max(-1.0f, std::min(1.0f, dx * sens));
                pad.rightStickY = std::max(-1.0f, std::min(1.0f, -dy * sens));
            }
        }
        else {
            // Star Pointer
            if (vpW > 0 && vpH > 0) {
                if (mSettings.relativePointer) {
                    if (touchSlot >= 0) {
                        float dx = ((px - mTouches[touchSlot].lastX) / (float)vpW) * 2.0f;
                        float dy = ((py - mTouches[touchSlot].lastY) / (float)vpH) * 2.0f;
                        mPointerX = std::max(-1.0f, std::min(1.0f, mPointerX + dx));
                        mPointerY = std::max(-1.0f, std::min(1.0f, mPointerY + dy));
                        mTouches[touchSlot].lastX = px;
                        mTouches[touchSlot].lastY = py;
                    }
                } else {
                    mPointerX = ((px - vpX) / (float)vpW) * 2.0f - 1.0f;
                    mPointerY = ((py - vpY) / (float)vpH) * 2.0f - 1.0f;
                    mPointerX = std::max(-1.0f, std::min(1.0f, mPointerX));
                    mPointerY = std::max(-1.0f, std::min(1.0f, mPointerY));
                    if (touchSlot >= 0) {
                        mTouches[touchSlot].lastX = px;
                        mTouches[touchSlot].lastY = py;
                    }
                }
            }
        }
    }

    if (!stickFound) {
        if (mStickPointerId >= 0) {
            mStickPointerId = -1;
            mStickKnobX = mStickBaseX;
            mStickKnobY = mStickBaseY;
            mDirty = true;
        }
        mTouchStickX = 0.0f;
        mTouchStickY = 0.0f;
        pad.stickX = 0.0f;
        pad.stickY = 0.0f;
    }

    if (!starStickFound) {
        if (mStarStickPointerId >= 0) {
            mStarStickPointerId = -1;
            mStarStickKnobX = mStarStickBaseX;
            mStarStickKnobY = mStarStickBaseY;
            mDirty = true;
        }
        mStarStickX = 0.0f;
        mStarStickY = 0.0f;
    }

    if (!cameraSwipeFound && pad.rightStickX != 0.0f) {
        pad.rightStickX = 0.0f;
        pad.rightStickY = 0.0f;
    }

    if (mTapPendingFrames > 0) {
        bool isTitle = (port_is_title_screen() != 0);
        if (isTitle) {
            touchButtons |= 0x0C00; // WPAD_BUTTON_A | WPAD_BUTTON_B
            mBtnBPressed = true;
            mBtnAPressed = true;
        }
    }

    pad.buttons = (pad.buttons & ~0x3C1F) | touchButtons;
    pad.pointerValid = 1;
    pad.pointerX = mPointerX;
    pad.pointerY = mPointerY;
    pad.pointerDist = 2.0f;

    return true;
}

void UIOverlay::generateVertices(int screenW, int screenH, std::vector<UIVertex>& verts) {
    verts.clear();
    verts.reserve(1024);

    float baseDpi = (float)screenH / 650.0f;
    float uiScale = baseDpi;
    float scale = baseDpi * mSettings.scale;
    float op = mSettings.opacity;
    float w = (float)screenW;
    float h = (float)screenH;

    // Animation progress calculation
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t now = (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
    if (mLastAnimTime == 0) mLastAnimTime = now;
    float dt = (float)(now - mLastAnimTime) / 1e9f;
    mLastAnimTime = now;
    if (dt < 0.0001f) dt = 0.016f;
    if (dt > 0.1f) dt = 0.1f;

    float target = mSettingsOpen ? 1.0f : 0.0f;
    if (fabsf(mDrawerAnim - target) > 0.0005f) {
        float speed = 14.0f; // Material 3 snappy deceleration
        mDrawerAnim += (target - mDrawerAnim) * (1.0f - expf(-speed * dt));
        if (fabsf(mDrawerAnim - target) < 0.001f) {
            mDrawerAnim = target;
        }
        mDirty = true;
    }

    if (mSaveToastTimer > 0.0f) {
        mSaveToastTimer -= dt;
        if (mSaveToastTimer < 0.0f) mSaveToastTimer = 0.0f;
        mDirty = true;
    }

    float drawerW = std::min(w * 0.44f, 390.0f * uiScale);
    float drawerX = (mDrawerAnim - 1.0f) * drawerW;
    float cogRot = mDrawerAnim * 1.5707963f; // Smooth 90-degree cogwheel turn

    auto addQuad = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                       const Color& c, float mode, float param, float aspect = 1.0f, float extra = 0.0f) {
        verts.push_back({x0, y0, u0, v0, c.r, c.g, c.b, c.a, mode, param, aspect, extra});
        verts.push_back({x1, y0, u1, v0, c.r, c.g, c.b, c.a, mode, param, aspect, extra});
        verts.push_back({x1, y1, u1, v1, c.r, c.g, c.b, c.a, mode, param, aspect, extra});

        verts.push_back({x0, y0, u0, v0, c.r, c.g, c.b, c.a, mode, param, aspect, extra});
        verts.push_back({x1, y1, u1, v1, c.r, c.g, c.b, c.a, mode, param, aspect, extra});
        verts.push_back({x0, y1, u0, v1, c.r, c.g, c.b, c.a, mode, param, aspect, extra});
    };

    auto drawCircle = [&](float cx, float cy, float r, const Color& c) {
        addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 1.0f, 0.0f);
    };

    auto drawRing = [&](float cx, float cy, float r, float innerRatio, const Color& c) {
        addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 1.0f, innerRatio);
    };

    auto drawRoundRect = [&](float x0, float y0, float x1, float y1, float radius, const Color& c) {
        float rw = x1 - x0;
        float rh = y1 - y0;
        if (rw <= 0.0f || rh <= 0.0f) return;
        if (rw >= rh) {
            float aspect = rw / rh;
            float rNorm = std::min(1.0f, radius / (rh * 0.5f));
            addQuad(x0, y0, x1, y1, -aspect, -1.0f, aspect, 1.0f, c, 0.0f, rNorm, aspect, 0.0f);
        } else {
            float aspect = rh / rw;
            float rNorm = std::min(1.0f, radius / (rw * 0.5f));
            addQuad(x0, y0, x1, y1, -1.0f, -aspect, 1.0f, aspect, c, 0.0f, rNorm, aspect, 1.0f);
        }
    };

    auto drawCogwheel = [&](float cx, float cy, float r, const Color& c, float rot = 0.0f) {
        addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 3.0f, rot);
    };

    auto drawCloseIcon = [&](float cx, float cy, float r, const Color& c) {
        addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 4.0f, 0.0f);
    };

    auto drawCyclone = [&](float cx, float cy, float r, const Color& c, float rot = 0.0f) {
        addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 5.0f, rot);
    };

    auto drawChevron = [&](float cx, float cy, float r, const Color& c, float rotAngle = 0.0f) {
        addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 6.0f, rotAngle);
    };

    auto drawCameraIcon = [&](float cx, float cy, float r, const Color& c) {
        addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 7.0f, 0.0f);
    };

    auto drawLine = [&](float x0, float y0, float x1, float y1, float thickness, const Color& c) {
        float dx = x1 - x0;
        float dy = y1 - y0;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.001f) return;
        float hThick = thickness * 0.5f;
        float nx = -dy / len * hThick;
        float ny = dx / len * hThick;

        verts.push_back({x0 + nx, y0 + ny, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
        verts.push_back({x1 + nx, y1 + ny, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
        verts.push_back({x1 - nx, y1 - ny, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});

        verts.push_back({x0 + nx, y0 + ny, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
        verts.push_back({x1 - nx, y1 - ny, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
        verts.push_back({x0 - nx, y0 - ny, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
    };

    auto drawTriangle = [&](float x0, float y0, float x1, float y1, float x2, float y2, const Color& c) {
        verts.push_back({x0, y0, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
        verts.push_back({x1, y1, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
        verts.push_back({x2, y2, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
    };

    const ui_font::Face& face = ui_font::kFaces[0];
    auto measureText = [&](const char* text, float sizeScale) -> float {
        float strW = 0.0f;
        for (const char* p = text; *p; p++) {
            char ch = *p;
            if (ch >= 32 && ch < 127) {
                strW += (face.glyphs[ch - 32].advance * 0.25f) * sizeScale;
            }
        }
        return strW;
    };

    auto drawText = [&](const char* text, float x, float baseline, float sizeScale, const Color& c, int align = 0) {
        float strW = measureText(text, sizeScale);
        float curX = x;
        if (align == 1) curX -= strW * 0.5f;      // Center
        else if (align == 2) curX -= strW;        // Right

        for (const char* p = text; *p; p++) {
            char ch = *p;
            if (ch < 32 || ch >= 127) continue;
            const ui_font::Glyph& g = face.glyphs[ch - 32];
            float gx = curX + g.left * sizeScale;
            float gy = baseline - g.top * sizeScale;
            float gw = g.w * sizeScale;
            float gh = g.h * sizeScale;
            float u0 = (float)g.x / (float)face.width;
            float v0 = (float)g.y / (float)face.height;
            float u1 = (float)(g.x + g.w) / (float)face.width;
            float v1 = (float)(g.y + g.h) / (float)face.height;

            addQuad(gx, gy, gx + gw, gy + gh, u0, v0, u1, v1, c, 2.0f, 0.0f);
            curX += (g.advance * 0.25f) * sizeScale;
        }
    };

    auto drawM3Switch = [&](float x, float y, float swW, float swH, bool on) {
        float r = swH * 0.5f;
        if (on) {
            // Track: Primary Container (#0B57D0)
            drawRoundRect(x, y, x + swW, y + swH, r, rgba(0.043f, 0.341f, 0.816f, 1.0f));
            // Thumb: High-contrast white disc at right
            float thumbR = r * 0.70f;
            drawCircle(x + swW - r, y + r, thumbR, rgba(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            // Track: Surface Container Highest (#303239) with subtle outline
            drawRoundRect(x, y, x + swW, y + swH, r, rgba(0.188f, 0.196f, 0.224f, 0.95f));
            drawRoundRect(x + 1.0f, y + 1.0f, x + swW - 1.0f, y + swH - 1.0f, r - 1.0f, rgba(0.267f, 0.278f, 0.310f, 0.60f));
            // Thumb: Outline disc at left
            float thumbR = r * 0.56f;
            drawCircle(x + r, y + r, thumbR, rgba(0.60f, 0.64f, 0.72f, 1.0f));
        }
    };

    auto drawSegmentedGroup = [&](float x, float y, float grpW, float grpH, const char* const* labels, int count, int activeIdx) {
        float r = 12.0f * uiScale;
        // Group container: Surface Container High (#26282E) with outline
        drawRoundRect(x, y, x + grpW, y + grpH, r, rgba(0.149f, 0.157f, 0.180f, 0.95f));
        drawRoundRect(x, y, x + grpW, y + grpH, r, rgba(0.267f, 0.278f, 0.310f, 0.50f));

        float segW = grpW / (float)count;
        for (int i = 0; i < count; i++) {
            float sx0 = x + i * segW;
            float sx1 = sx0 + segW;
            bool active = (i == activeIdx);
            if (active) {
                // Active pill: Primary Container (#0B57D0)
                drawRoundRect(sx0 + 2.0f, y + 2.0f, sx1 - 2.0f, y + grpH - 2.0f, r - 2.0f, rgba(0.043f, 0.341f, 0.816f, 1.0f));
                drawText(labels[i], (sx0 + sx1) * 0.5f, y + grpH * 0.64f, 0.36f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
            } else {
                drawText(labels[i], (sx0 + sx1) * 0.5f, y + grpH * 0.64f, 0.36f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.95f), 1);
                if (i > 0 && (i - 1) != activeIdx) {
                    drawRoundRect(sx0 - 0.5f, y + 6.0f, sx0 + 0.5f, y + grpH - 6.0f, 0.0f, rgba(0.267f, 0.278f, 0.310f, 0.40f));
                }
            }
        }
    };

    // -------------------------------------------------------------
    // Draw On-Screen Controls (HUD) - Modern Minimalist Frosted Theme
    // -------------------------------------------------------------
    if (mSettings.showControls && mDrawerAnim < 0.98f) {
        float hudOp = op * (1.0f - mDrawerAnim * 0.75f);

        // ---------------------------------------------------------
        // 1. Virtual Joystick - Precision Minimalist Reticle & Laser Tether
        // ---------------------------------------------------------
        float defaultStickX = 175.0f * scale + mSettings.stickOffsetX;
        float defaultStickY = h - 175.0f * scale + mSettings.stickOffsetY;
        float baseX = (mStickPointerId >= 0) ? mStickBaseX : defaultStickX;
        float baseY = (mStickPointerId >= 0) ? mStickBaseY : defaultStickY;
        float knobX = (mStickPointerId >= 0) ? mStickKnobX : baseX;
        float knobY = (mStickPointerId >= 0) ? mStickKnobY : baseY;
        float baseR = 95.0f * scale;
        float knobR = 38.0f * scale;
        bool stickActive = (mStickPointerId >= 0);

        // Base Plate: Translucent dark glass disc
        drawCircle(baseX, baseY, baseR, rgba(0.05f, 0.07f, 0.11f, hudOp * 0.28f));

        // Outer Reticle: Razor-thin anti-aliased ring
        drawRing(baseX, baseY, baseR, 0.96f, rgba(0.60f, 0.80f, 1.0f, hudOp * 0.45f));

        // Cardinal Precision Orientation Ticks (N, S, E, W)
        float tickIn = baseR * 0.84f;
        float tickOut = baseR * 0.95f;
        float tickW = 2.0f * scale;
        Color tickCol = rgba(0.75f, 0.88f, 1.0f, hudOp * 0.50f);
        drawLine(baseX, baseY - tickOut, baseX, baseY - tickIn, tickW, tickCol); // N
        drawLine(baseX, baseY + tickIn, baseX, baseY + tickOut, tickW, tickCol); // S
        drawLine(baseX - tickOut, baseY, baseX - tickIn, baseY, tickW, tickCol); // W
        drawLine(baseX + tickIn, baseY, baseX + tickOut, baseY, tickW, tickCol); // E

        // Deadzone Center Target: Minimalist ring & center micro-dot
        drawRing(baseX, baseY, 13.0f * scale, 0.86f, rgba(0.50f, 0.70f, 1.0f, hudOp * 0.35f));
        drawCircle(baseX, baseY, 2.5f * scale, rgba(1.0f, 1.0f, 1.0f, hudOp * 0.60f));

        // Dynamic Luminous Laser Tether (Center to Knob deflection vector)
        float sDx = knobX - baseX;
        float sDy = knobY - baseY;
        float sDist = sqrtf(sDx * sDx + sDy * sDy);
        if (sDist > 5.0f * scale) {
            // Soft cyan outer glow tether
            drawLine(baseX, baseY, knobX, knobY, 3.2f * scale, rgba(0.22f, 0.74f, 0.97f, hudOp * 0.65f));
            // Crisp white core laser beam
            drawLine(baseX, baseY, knobX, knobY, 1.4f * scale, rgba(1.0f, 1.0f, 1.0f, hudOp * 0.85f));
        }

        // Floating Frosted Thumb Puck
        float kpR = stickActive ? (knobR * 1.04f) : knobR;
        // Knob base disc
        drawCircle(knobX, knobY, kpR, rgba(0.09f, 0.13f, 0.22f, hudOp * (stickActive ? 0.85f : 0.65f)));
        // Outer glowing rim
        Color knobRim = stickActive ? rgba(0.22f, 0.74f, 0.97f, hudOp * 0.95f) : rgba(0.60f, 0.80f, 1.0f, hudOp * 0.65f);
        drawRing(knobX, knobY, kpR, 0.90f, knobRim);
        // Inner concentric thumb groove ring
        drawRing(knobX, knobY, kpR * 0.68f, 0.93f, rgba(1.0f, 1.0f, 1.0f, hudOp * 0.35f));
        // Center tactile micro-dot
        drawCircle(knobX, knobY, 3.0f * scale, stickActive ? rgba(0.22f, 0.74f, 0.97f, hudOp * 0.95f) : rgba(1.0f, 1.0f, 1.0f, hudOp * 0.50f));

        // ---------------------------------------------------------
        // 1b. 2nd Virtual Joystick (Star Pointer) - Golden Star Theme
        // ---------------------------------------------------------
        if (mSettings.starStick) {
            StarStickLayout starL = starStickLayout(screenW, screenH);
            float sBaseX = (mStarStickPointerId >= 0) ? mStarStickBaseX : starL.cx;
            float sBaseY = (mStarStickPointerId >= 0) ? mStarStickBaseY : starL.cy;
            float sKnobX = (mStarStickPointerId >= 0) ? mStarStickKnobX : sBaseX;
            float sKnobY = (mStarStickPointerId >= 0) ? mStarStickKnobY : sBaseY;
            float sBaseR = starL.r;
            float sKnobR = 30.0f * scale;
            bool sActive = (mStarStickPointerId >= 0);

            // Base plate: Translucent dark gold glass disc
            drawCircle(sBaseX, sBaseY, sBaseR, rgba(0.12f, 0.10f, 0.05f, hudOp * 0.35f));
            drawRing(sBaseX, sBaseY, sBaseR, 0.94f, rgba(1.0f, 0.85f, 0.20f, hudOp * (sActive ? 0.75f : 0.40f)));

            // Center star micro-dot
            drawCircle(sBaseX, sBaseY, 3.0f * scale, rgba(1.0f, 0.90f, 0.30f, hudOp * 0.70f));

            // Laser Tether to Knob
            float sDx = sKnobX - sBaseX;
            float sDy = sKnobY - sBaseY;
            float sDist = sqrtf(sDx * sDx + sDy * sDy);
            if (sDist > 4.0f * scale) {
                drawLine(sBaseX, sBaseY, sKnobX, sKnobY, 2.5f * scale, rgba(1.0f, 0.80f, 0.15f, hudOp * 0.70f));
            }

            // Knob disc
            float spR = sActive ? (sKnobR * 1.05f) : sKnobR;
            drawCircle(sKnobX, sKnobY, spR, rgba(0.20f, 0.16f, 0.06f, hudOp * (sActive ? 0.90f : 0.70f)));
            drawRing(sKnobX, sKnobY, spR, 0.88f, rgba(1.0f, 0.90f, 0.30f, hudOp * 0.95f));
            drawText("*", sKnobX, sKnobY + 6.0f * scale, 0.45f * scale, rgba(1.0f, 1.0f, 1.0f, hudOp * 0.95f), 1);
        }

        // ---------------------------------------------------------
        // 2. Action Buttons - Frosted Glass with Signature Micro-Accents
        // ---------------------------------------------------------
        float btnBaseX = w - 150.0f * scale + mSettings.buttonOffsetX;
        float btnBaseY = h - 150.0f * scale + mSettings.buttonOffsetY;
        ButtonLayout bl = buttonLayout(screenW, screenH);

        auto drawModernButton = [&](float cx, float cy, float r, const char* label, uint32_t accentHex, bool pressed, int specialGlyph = 0) {
            float pr = pressed ? (r * 0.93f) : r;
            float baseAlpha = pressed ? (hudOp * 0.88f) : (hudOp * 0.36f);
            float borderAlpha = pressed ? (hudOp * 0.98f) : (hudOp * 0.65f);
            float ringAlpha = pressed ? (hudOp * 0.85f) : (hudOp * 0.25f);

            // Frosted dark glass base plate
            Color baseBg = pressed ? hexColor(accentHex, baseAlpha) : rgba(0.06f, 0.08f, 0.13f, baseAlpha);
            drawCircle(cx, cy, pr, baseBg);

            // Thin luminous perimeter ring (1.5px)
            Color outerRing = pressed ? rgba(1.0f, 1.0f, 1.0f, borderAlpha) : hexColor(accentHex, borderAlpha);
            drawRing(cx, cy, pr, 0.92f, outerRing);

            // Inner concentric frosted micro-ring
            drawRing(cx, cy, pr * 0.80f, 0.95f, rgba(1.0f, 1.0f, 1.0f, ringAlpha));

            // High-contrast clean glyph / typography
            Color glyphCol = pressed ? rgba(1.0f, 1.0f, 1.0f, hudOp * 1.0f) : rgba(0.92f, 0.95f, 1.0f, hudOp * 0.92f);

            if (specialGlyph == 1) { // SPIN (Y): Procedural Cyclone glyph + "Y • SPIN"
                float iconR = pr * 0.40f;
                drawCyclone(cx, cy - 5.0f * scale, iconR, glyphCol, 0.0f);
                drawText("Y • SPIN", cx, cy + pr * 0.58f, 0.22f * scale, glyphCol, 1);
            } else if (specialGlyph == 2) { // PLUS (+): Modern precision cross
                float armLen = pr * 0.38f;
                float thick = 2.4f * scale;
                drawLine(cx - armLen, cy, cx + armLen, cy, thick, glyphCol);
                drawLine(cx, cy - armLen, cx, cy + armLen, thick, glyphCol);
            } else { // Standard typography (A, B, ZL)
                float textSize = (r > 48.0f * scale) ? (0.64f * scale) : (0.54f * scale);
                drawText(label, cx, cy + 11.0f * scale, textSize, glyphCol, 1);
            }
        };

        // Button B (Primary Jump) - Luminous Amber Gold (#F59E0B) - Nintendo Switch Jump
        drawModernButton(bl.bx, bl.by, bl.br, "B", 0xF59E0B, mBtnBPressed);

        // Button A (Jump / Interact) - Luminous Electric Cyan (#38BDF8)
        drawModernButton(bl.ax, bl.ay, bl.ar, "A", 0x38BDF8, mBtnAPressed);

        // Button Y (Spin Attack) - Luminous Emerald Mint (#34D399) with Cyclone Glyph
        drawModernButton(bl.spinX, bl.spinY, bl.spinR, "Y", 0x34D399, mBtnSpinPressed, 1);

        // Button ZL (Crouch / Ground Pound) - Luminous Lavender Violet (#A78BFA)
        drawModernButton(bl.zx, bl.zy, bl.zr, "ZL", 0xA78BFA, mBtnZPressed);

        // Button + (Pause) - Minimalist Pearl Slate (#E2E8F0) with Vector Cross
        drawModernButton(w - 65.0f * scale, 65.0f * scale, 34.0f * scale, "+", 0x94A3B8, mBtnPlusPressed, 2);

        // ---------------------------------------------------------
        // 3. Camera D-Pad - Directional Precision Disc with Chevrons & Camera Icon
        // ---------------------------------------------------------
        if (mSettings.showDpad) {
            float baseDpadX = mSettings.dpadOnRight ? (w - 140.0f * scale) : (130.0f * scale);
            float baseDpadY = mSettings.dpadOnRight ? (170.0f * scale) : (160.0f * scale);
            float dpadX = baseDpadX + mSettings.dpadOffsetX;
            float dpadY = baseDpadY + mSettings.dpadOffsetY;
            float dialR = 56.0f * scale;
            float armDist = 36.0f * scale;
            float chevronR = 14.0f * scale;

            // Frosted circular dial plate
            drawCircle(dpadX, dpadY, dialR, rgba(0.06f, 0.08f, 0.13f, hudOp * 0.32f));

            // Outer razor-thin precision ring
            drawRing(dpadX, dpadY, dialR, 0.95f, rgba(0.55f, 0.75f, 1.0f, hudOp * 0.42f));

            // Quadrant division hairlines (45 deg diagonals)
            float divInner = 20.0f * scale;
            float divOuter = dialR * 0.88f;
            float diag = 0.7071f;
            Color divCol = rgba(1.0f, 1.0f, 1.0f, hudOp * 0.16f);
            drawLine(dpadX + divInner * diag, dpadY + divInner * diag, dpadX + divOuter * diag, dpadY + divOuter * diag, 1.4f * scale, divCol);
            drawLine(dpadX - divInner * diag, dpadY + divInner * diag, dpadX - divOuter * diag, dpadY + divOuter * diag, 1.4f * scale, divCol);
            drawLine(dpadX + divInner * diag, dpadY - divInner * diag, dpadX + divOuter * diag, dpadY - divOuter * diag, 1.4f * scale, divCol);
            drawLine(dpadX - divInner * diag, dpadY - divInner * diag, dpadX - divOuter * diag, dpadY - divOuter * diag, 1.4f * scale, divCol);

            // 4 Directional Chevron Sectors (Left, Right, Up, Down)
            auto drawDpadSector = [&](float cx, float cy, float rotAngle, bool pressed) {
                if (pressed) {
                    drawCircle(cx, cy, 20.0f * scale, rgba(0.22f, 0.74f, 0.97f, hudOp * 0.80f));
                    drawRing(cx, cy, 20.0f * scale, 0.88f, rgba(1.0f, 1.0f, 1.0f, hudOp * 0.95f));
                }
                Color chevCol = pressed ? rgba(1.0f, 1.0f, 1.0f, hudOp * 1.0f) : rgba(0.85f, 0.92f, 1.0f, hudOp * 0.75f);
                float chevSize = pressed ? (chevronR * 1.15f) : chevronR;
                drawChevron(cx, cy, chevSize, chevCol, rotAngle);
            };

            // Left (rot = PI)
            drawDpadSector(dpadX - armDist, dpadY, 3.14159265f, mBtnDpadLeft);
            // Right (rot = 0)
            drawDpadSector(dpadX + armDist, dpadY, 0.0f, mBtnDpadRight);
            // Up (rot = -PI/2)
            drawDpadSector(dpadX, dpadY - armDist, -1.5707963f, mBtnDpadUp);
            // Down (rot = PI/2)
            drawDpadSector(dpadX, dpadY + armDist, 1.5707963f, mBtnDpadDown);

            // Center Concave Hub with Modern Camera Icon
            float hubR = 18.0f * scale;
            drawCircle(dpadX, dpadY, hubR, rgba(0.09f, 0.13f, 0.22f, hudOp * 0.80f));
            drawRing(dpadX, dpadY, hubR, 0.88f, rgba(0.55f, 0.75f, 1.0f, hudOp * 0.55f));
            drawCameraIcon(dpadX, dpadY, 11.0f * scale, rgba(0.85f, 0.93f, 1.0f, hudOp * 0.90f));
        }
    }

    // -------------------------------------------------------------
    // Realtime Hardware Monitor HUD (FPS, Frame Time, CPU, GPU, RAM)
    // -------------------------------------------------------------
    if (mSettings.perfHudMode > 0 && !mEditLayoutMode) {
        char hudBuf[144];
        if (mSettings.perfHudMode == 1) {
            snprintf(hudBuf, sizeof(hudBuf), "%.1f FPS  •  %.1f ms", mCurrentFps, mAvgFrameTimeMs);
        } else {
            snprintf(hudBuf, sizeof(hudBuf), "%.1f FPS  •  %.1f ms  •  CPU %.0f%%  •  GPU %.0f%%  •  RAM %.0f MB",
                     mCurrentFps, mAvgFrameTimeMs, mCpuUsagePercent, mGpuUsagePercent, mRamUsageMB);
        }

        float hudTextScale = 0.28f * uiScale;
        float textW = measureText(hudBuf, hudTextScale);
        float hudPadX = 14.0f * uiScale;
        float hudPadY = 6.0f * uiScale;
        float hudW = textW + hudPadX * 2.0f;
        float hudH = 26.0f * uiScale;
        float hudX = w * 0.5f - hudW * 0.5f; // Perfectly centered at top of screen
        float hudY = 14.0f * uiScale;

        // Frosted dark pill container
        drawRoundRect(hudX, hudY, hudX + hudW, hudY + hudH, 13.0f * uiScale, rgba(0.06f, 0.08f, 0.12f, 0.85f));
        drawRoundRect(hudX + 1.0f, hudY + 1.0f, hudX + hudW - 1.0f, hudY + hudH - 1.0f, 12.0f * uiScale, rgba(0.24f, 0.32f, 0.44f, 0.55f));

        // Pulsing status LED
        Color ledColor = (mCurrentFps >= 55.0f) ? rgba(0.30f, 0.90f, 0.50f, 1.0f) :
                         (mCurrentFps >= 30.0f) ? rgba(1.0f, 0.75f, 0.20f, 1.0f) :
                                                  rgba(0.95f, 0.30f, 0.25f, 1.0f);
        drawCircle(hudX + 10.0f * uiScale, hudY + hudH * 0.5f, 3.5f * uiScale, ledColor);

        // Render stats typography
        drawText(hudBuf, hudX + 18.0f * uiScale, hudY + hudH * 0.5f + 4.0f * uiScale, hudTextScale, rgba(0.92f, 0.95f, 1.0f, 0.95f), 0);
    }

    // -------------------------------------------------------------
    // Speedrunning & Practice Telemetry HUD
    // -------------------------------------------------------------
    if (mSettings.speedrunHud && !mEditLayoutMode) {
        PortSpeedrunTelemetry telem{};
        port_get_speedrun_telemetry(&telem);

        char line1[128];
        char line2[128];
        snprintf(line1, sizeof(line1), "POS: [%.1f, %.1f, %.1f]  •  YAW: %.0f°", telem.posX, telem.posY, telem.posZ, telem.angleYaw);
        uint32_t totalSec = telem.stageTimerMs / 1000;
        uint32_t min = totalSec / 60;
        uint32_t sec = totalSec % 60;
        uint32_t csec = (telem.stageTimerMs % 1000) / 10;
        snprintf(line2, sizeof(line2), "H-SPD: %.2f  •  V-SPD: %.2f  •  HP: %d/%d  •  %02u:%02u.%02u",
                 telem.velH, telem.velY, telem.health, telem.maxHealth, min, sec, csec);

        float sTextScale = 0.26f * uiScale;
        float tw1 = measureText(line1, sTextScale);
        float tw2 = measureText(line2, sTextScale);
        float sBoxW = std::max(tw1, tw2) + 24.0f * uiScale;
        float sBoxH = 40.0f * uiScale;
        float sBoxX = 16.0f * uiScale;
        float sBoxY = (mSettings.perfHudMode > 0) ? (46.0f * uiScale) : (14.0f * uiScale);

        // Glassmorphic dark container
        drawRoundRect(sBoxX, sBoxY, sBoxX + sBoxW, sBoxY + sBoxH, 10.0f * uiScale, rgba(0.04f, 0.08f, 0.14f, 0.88f));
        drawRoundRect(sBoxX + 1.0f, sBoxY + 1.0f, sBoxX + sBoxW - 1.0f, sBoxY + sBoxH - 1.0f, 9.0f * uiScale, rgba(0.20f, 0.75f, 0.95f, 0.50f));

        // Green indicator dot
        drawCircle(sBoxX + 9.0f * uiScale, sBoxY + 14.0f * uiScale, 3.0f * uiScale, rgba(0.20f, 0.95f, 0.65f, 1.0f));

        drawText(line1, sBoxX + 16.0f * uiScale, sBoxY + 16.0f * uiScale, sTextScale, rgba(0.85f, 0.95f, 1.0f, 0.95f), 0);
        drawText(line2, sBoxX + 16.0f * uiScale, sBoxY + 31.0f * uiScale, sTextScale, rgba(1.0f, 0.88f, 0.40f, 0.95f), 0);
    }

    // -------------------------------------------------------------
    // Material 3 Settings Tab Dock (Pinned to Top-Left Edge)
    // -------------------------------------------------------------
    if (mDrawerAnim < 0.95f) {
        float tabW = 56.0f * uiScale;
        float tabH = 48.0f * uiScale;
        float tabY = 22.0f * uiScale;
        float tabPr = mBtnCfgPressed ? 0.92f : 1.0f;

        // Docked pill container: rounded right edge
        drawRoundRect(-10.0f, tabY, tabW * tabPr, tabY + tabH, 24.0f * uiScale, rgba(0.149f, 0.157f, 0.180f, 0.94f));
        drawRoundRect(-10.0f, tabY, tabW * tabPr, tabY + tabH, 24.0f * uiScale, rgba(0.267f, 0.278f, 0.310f, 0.60f));

        // Rotating Material 3 Cogwheel icon in Google Accent Blue
        drawCogwheel(tabW * 0.50f, tabY + tabH * 0.5f, 15.0f * uiScale, rgba(0.659f, 0.780f, 0.980f, 1.0f), cogRot);

        // Direct Quick Save Pill Button
        float qsX = tabW + 16.0f * uiScale;
        float qsW = 58.0f * uiScale;
        drawRoundRect(qsX, tabY + 4.0f * uiScale, qsX + qsW, tabY + tabH - 4.0f * uiScale, 16.0f * uiScale, rgba(0.12f, 0.14f, 0.18f, 0.88f));
        drawRoundRect(qsX, tabY + 4.0f * uiScale, qsX + qsW, tabY + tabH - 4.0f * uiScale, 16.0f * uiScale, rgba(0.25f, 0.45f, 0.78f, 0.65f));
        drawText("SAVE", qsX + qsW * 0.5f, tabY + tabH * 0.5f + 4.0f * uiScale, 0.28f * uiScale, rgba(0.92f, 0.96f, 1.0f, 0.95f), 1);
    }

    // -------------------------------------------------------------
    // Draw Edit Layout Overlay
    // -------------------------------------------------------------
    if (mEditLayoutMode) {
        // Scrim
        drawRoundRect(0, 0, w, h, 0.0f, rgba(0.0f, 0.0f, 0.0f, 0.45f));

        // M3 Top bar card
        drawRoundRect(w * 0.5f - 240.0f, 30.0f, w * 0.5f + 240.0f, 95.0f, 20.0f, rgba(0.12f, 0.14f, 0.18f, 0.96f));
        drawText("DRAG ANY CONTROL TO MOVE IT", w * 0.5f, 55.0f, 0.46f, rgba(0.90f, 0.92f, 0.96f, 1.0f), 1);

        // Save button (M3 Primary Pill)
        float saveBtnX = w * 0.5f - 110.0f;
        drawRoundRect(saveBtnX, 65.0f, saveBtnX + 100.0f, 90.0f, 12.0f, rgba(0.043f, 0.341f, 0.816f, 0.95f));
        drawText("SAVE", saveBtnX + 50.0f, 83.0f, 0.42f, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

        // Reset button (M3 Outline Pill)
        float rstBtnX = w * 0.5f + 10.0f;
        drawRoundRect(rstBtnX, 65.0f, rstBtnX + 100.0f, 90.0f, 12.0f, rgba(0.35f, 0.18f, 0.20f, 0.95f));
        drawText("RESET", rstBtnX + 50.0f, 83.0f, 0.42f, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

        // Stick cluster bounding outline
        float defaultStickX = 175.0f * scale + mSettings.stickOffsetX;
        float defaultStickY = h - 175.0f * scale + mSettings.stickOffsetY;
        drawRing(defaultStickX, defaultStickY, 120.0f * scale, 0.96f, rgba(0.40f, 0.65f, 0.98f, 0.85f));
        drawText("JOYSTICK", defaultStickX, defaultStickY - 130.0f * scale, 0.45f, rgba(0.40f, 0.65f, 0.98f, 0.95f), 1);

        // Button cluster bounding outline
        float btnBaseX = w - 160.0f * scale + mSettings.buttonOffsetX;
        float btnBaseY = h - 160.0f * scale + mSettings.buttonOffsetY;
        drawRing(btnBaseX - 60.0f * scale, btnBaseY - 60.0f * scale, 160.0f * scale, 0.96f, rgba(0.40f, 0.65f, 0.98f, 0.85f));

        // Each action button gets its own grip ring so it can be dragged apart
        // from the others (e.g. Z away from A).
        ButtonLayout editBl = buttonLayout(screenW, screenH);
        struct EditBtn { float cx, cy, r; const char* label; } editBtns[4] = {
            {editBl.ax, editBl.ay, editBl.ar, "A"},
            {editBl.bx, editBl.by, editBl.br, "B"},
            {editBl.zx, editBl.zy, editBl.zr, "Z"},
            {editBl.spinX, editBl.spinY, editBl.spinR, "SPIN"},
        };
        for (const EditBtn& b : editBtns) {
            drawCircle(b.cx, b.cy, b.r, rgba(0.06f, 0.08f, 0.13f, 0.55f));
            drawRing(b.cx, b.cy, b.r, 0.92f, rgba(0.40f, 0.65f, 0.98f, 0.95f));
            drawText(b.label, b.cx, b.cy + 8.0f * scale, 0.42f * scale, rgba(0.92f, 0.95f, 1.0f, 0.95f), 1);
        }
        drawText("BUTTONS", btnBaseX - 20.0f * scale, btnBaseY - 240.0f * scale, 0.45f, rgba(0.40f, 0.65f, 0.98f, 0.95f), 1);

        // Camera D-Pad bounding outline
        if (mSettings.showDpad) {
            float baseDpadX = mSettings.dpadOnRight ? (w - 380.0f * scale) : (140.0f * scale);
            float baseDpadY = mSettings.dpadOnRight ? (h - 220.0f * scale) : (h - 390.0f * scale);
            float curDpadX = baseDpadX + mSettings.dpadOffsetX;
            float curDpadY = baseDpadY + mSettings.dpadOffsetY;
            drawRing(curDpadX, curDpadY, 78.0f * scale, 0.96f, rgba(0.40f, 0.65f, 0.98f, 0.85f));
            drawText("CAMERA D-PAD", curDpadX, curDpadY - 88.0f * scale, 0.42f, rgba(0.40f, 0.65f, 0.98f, 0.95f), 1);
        }

        // Star Joystick bounding outline (Mappable anywhere)
        if (mSettings.starStick) {
            StarStickLayout starL = starStickLayout(screenW, screenH);
            drawCircle(starL.cx, starL.cy, starL.r, rgba(0.20f, 0.16f, 0.06f, 0.50f));
            drawRing(starL.cx, starL.cy, starL.r, 0.94f, rgba(1.0f, 0.85f, 0.20f, 0.95f));
            drawCircle(starL.cx, starL.cy, 3.0f * scale, rgba(1.0f, 0.90f, 0.30f, 0.90f));
            drawText("*", starL.cx, starL.cy + 6.0f * scale, 0.45f * scale, rgba(1.0f, 1.0f, 1.0f, 0.95f), 1);
            drawText("STAR JOYSTICK", starL.cx, starL.cy - starL.r - 12.0f * scale, 0.42f, rgba(1.0f, 0.85f, 0.20f, 0.95f), 1);
        }
    }

    // -------------------------------------------------------------
    // Modern In-App Navigation Drawer (4-Tab Glassmorphic System)
    // -------------------------------------------------------------
    if (mDrawerAnim > 0.001f) {
        // Scrim backdrop (smoothly fades with animation)
        drawRoundRect(0, 0, w, h, 0.0f, rgba(0.0f, 0.0f, 0.0f, mDrawerAnim * 0.58f));

        // Side Sheet container: Surface Container (#12141A) with 26dp rounded right corners
        drawRoundRect(drawerX - 60.0f, 0.0f, drawerX + drawerW, h, 26.0f * uiScale, rgba(0.07f, 0.08f, 0.11f, 0.98f));
        // Right edge luminous hairline border
        drawRoundRect(drawerX + drawerW - 1.5f, 0.0f, drawerX + drawerW, h, 0.0f, rgba(0.25f, 0.30f, 0.42f, 0.60f));

        float padX = 20.0f * uiScale;
        float contentW = drawerW - padX * 2.0f;
        float contentTop = 106.0f * uiScale;
        float contentBottom = h - 54.0f * uiScale;
        float totalContentH = getTabContentHeight(mSettingsTab, uiScale);
        float maxScroll = std::max(0.0f, totalContentH - (contentBottom - contentTop));

        // Smooth scroll damping
        if (fabsf(mDrawerScrollY - mDrawerScrollTarget) > 0.5f) {
            mDrawerScrollY += (mDrawerScrollTarget - mDrawerScrollY) * (1.0f - expf(-18.0f * dt));
            mDirty = true;
        } else {
            mDrawerScrollY = mDrawerScrollTarget;
        }
        mDrawerScrollY = std::max(0.0f, std::min(mDrawerScrollY, maxScroll));
        mDrawerScrollTarget = std::max(0.0f, std::min(mDrawerScrollTarget, maxScroll));

        float swW = 48.0f * uiScale, swH = 26.0f * uiScale;

        // Active tab theme colors:
        // Tab 0: Electric Cyan
        // Tab 1: Lavender Violet
        // Tab 2: Emerald Mint
        // Tab 3: Amber Gold
        Color themeColor = (mSettingsTab == 0) ? rgba(0.20f, 0.85f, 1.0f, 1.0f) :
                           (mSettingsTab == 1) ? rgba(0.70f, 0.55f, 1.0f, 1.0f) :
                           (mSettingsTab == 2) ? rgba(0.25f, 0.90f, 0.65f, 1.0f) :
                                                 rgba(1.0f, 0.78f, 0.25f, 1.0f);

        // ---------------------------------------------------------
        // A. Scrollable Content Layer (Table-Driven)
        // ---------------------------------------------------------
        size_t rowCount = 0;
        const MenuRowDef* rows = getTabRows(mSettingsTab, rowCount);
        float curY = 8.0f * uiScale;

        for (size_t i = 0; i < rowCount; i++) {
            const MenuRowDef& r = rows[i];
            float rowH = r.heightDp * uiScale;
            float drawY = contentTop + curY - mDrawerScrollY;
            curY += rowH + 6.0f * uiScale;

            if (drawY + rowH < contentTop - 15.0f || drawY > contentBottom + 15.0f) {
                continue;
            }

            float rowX0 = drawerX + padX;
            float rowX1 = drawerX + drawerW - padX;

            if (r.type == ROW_SECTION) {
                // Section Header with Glowing Vertical Pill Accent
                drawRoundRect(rowX0, drawY + 8.0f * uiScale, rowX0 + 3.5f * uiScale, drawY + 26.0f * uiScale, 2.0f * uiScale, themeColor);
                drawText(r.title, rowX0 + 10.0f * uiScale, drawY + 19.0f * uiScale, 0.33f * uiScale, themeColor, 0);
                if (r.subtitle && r.subtitle[0]) {
                    drawText(r.subtitle, rowX0 + 10.0f * uiScale, drawY + 31.0f * uiScale, 0.24f * uiScale, rgba(0.60f, 0.66f, 0.76f, 0.80f), 0);
                }
            }
            else if (r.type == ROW_SWITCH) {
                // Frosted card backing
                drawRoundRect(rowX0, drawY + 2.0f, rowX1, drawY + rowH - 2.0f, 12.0f * uiScale, rgba(0.12f, 0.14f, 0.18f, 0.75f));
                drawRoundRect(rowX0, drawY + 2.0f, rowX1, drawY + rowH - 2.0f, 12.0f * uiScale, rgba(0.22f, 0.26f, 0.35f, 0.40f));

                // Title & Subtitle
                drawText(r.title, rowX0 + 12.0f * uiScale, drawY + 19.0f * uiScale, 0.36f * uiScale, rgba(0.92f, 0.93f, 0.96f, 1.0f), 0);
                if (r.subtitle && r.subtitle[0]) {
                    drawText(r.subtitle, rowX0 + 12.0f * uiScale, drawY + 33.0f * uiScale, 0.25f * uiScale, rgba(0.68f, 0.72f, 0.80f, 0.85f), 0);
                }

                // Switch state retrieval
                bool swOn = false;
                switch (r.action) {
                case ACT_SHOW_CONTROLS: swOn = mSettings.showControls; break;
                case ACT_TOUCH_CAMERA: swOn = mSettings.touchCamera; break;
                case ACT_STAR_STICK: swOn = mSettings.starStick; break;
                case ACT_SHOW_DPAD: swOn = mSettings.showDpad; break;
                case ACT_GYRO_AIMING: swOn = mSettings.gyroAiming; break;
                case ACT_RELATIVE_POINTER: swOn = mSettings.relativePointer; break;
                case ACT_FULL_TOUCH_MODE: swOn = mSettings.fullTouchMode; break;
                case ACT_HAPTICS: swOn = mSettings.haptics; break;
                case ACT_BATTERY_SAVER: swOn = mSettings.batterySaver; break;
                case ACT_VULKAN_POSTPROCESS: swOn = (port_is_post_processing_enabled() != 0); break;
                case ACT_FULLSCREEN: swOn = mSettings.fullscreen; break;
                case ACT_ODYSSEY_MOVESET: swOn = mSettings.odysseyMoveset; break;
                case ACT_COOP_MODE: swOn = mSettings.coopMode; break;
                case ACT_SPEEDRUN_HUD: swOn = mSettings.speedrunHud; break;
                case ACT_SHIELD_TEXTURES: swOn = mSettings.modShieldTextures; break;
                case ACT_SHIELD_CUTSCENES: swOn = mSettings.modShieldCutscenes; break;
                case ACT_SHIELD_CONTROLS: swOn = mSettings.modShieldControls; break;
                case ACT_CLASSIC_GAMEPAD: swOn = mSettings.modClassicGamepadUI; break;
                case ACT_FLY_MODE: swOn = mSettings.modFly; break;
                case ACT_SKIP_CUTSCENES: swOn = mSettings.modSkipCutscenes; break;
                case ACT_INFINITE_HEALTH: swOn = mSettings.modInfiniteHealth; break;
                case ACT_INFINITE_LIVES: swOn = mSettings.modInfiniteLives; break;
                case ACT_INFINITE_STARBITS: swOn = mSettings.modInfiniteStarBits; break;
                case ACT_RIIVOLUTION: swOn = mSettings.modRiivolution; break;
                default: break;
                }

                drawM3Switch(rowX1 - swW - 10.0f * uiScale, drawY + (rowH - swH) * 0.5f, swW, swH, swOn);
            }
            else if (r.type == ROW_SEGMENTED) {
                drawText(r.title, rowX0 + 2.0f * uiScale, drawY + 14.0f * uiScale, 0.35f * uiScale, rgba(0.88f, 0.90f, 0.95f, 1.0f), 0);

                int activeIdx = 0;
                switch (r.action) {
                case ACT_CHARACTER_SELECT:
                    activeIdx = std::max(0, std::min(3, mSettings.characterChoice));
                    break;
                case ACT_RIGHT_STICK_MODE:
                    activeIdx = (mSettings.rightStickMode == 1) ? 1 : 0;
                    break;
                case ACT_CONTROLS_OPACITY:
                    activeIdx = (fabsf(mSettings.opacity - 0.25f) < 0.12f) ? 0 : (fabsf(mSettings.opacity - 0.50f) < 0.12f) ? 1 : (fabsf(mSettings.opacity - 0.75f) < 0.12f) ? 2 : 3;
                    break;
                case ACT_BUTTON_SIZE:
                    activeIdx = (fabsf(mSettings.scale - 0.85f) < 0.08f) ? 0 : (fabsf(mSettings.scale - 1.25f) < 0.08f) ? 2 : 1;
                    break;
                case ACT_JOYSTICK_MODE:
                    activeIdx = mSettings.dynamicStick ? 0 : 1;
                    break;
                case ACT_STICK_SENSITIVITY:
                    activeIdx = (fabsf(mSettings.stickSensitivity - 0.75f) < 0.10f) ? 0 :
                                (fabsf(mSettings.stickSensitivity - 1.25f) < 0.10f) ? 2 :
                                (fabsf(mSettings.stickSensitivity - 1.50f) < 0.10f) ? 3 : 1;
                    break;
                case ACT_STAR_STICK_SENSITIVITY:
                    activeIdx = (fabsf(mSettings.starStickSensitivity - 0.50f) < 0.10f) ? 0 :
                                (fabsf(mSettings.starStickSensitivity - 0.75f) < 0.10f) ? 1 :
                                (fabsf(mSettings.starStickSensitivity - 1.50f) < 0.10f) ? 3 :
                                (fabsf(mSettings.starStickSensitivity - 2.00f) < 0.10f) ? 4 : 2;
                    break;
                case ACT_DPAD_SIDE:
                    activeIdx = mSettings.dpadOnRight ? 1 : 0;
                    break;
                case ACT_GRAPHICS_BACKEND:
                    activeIdx = (mSettings.graphicsBackend == 1) ? 1 : 0;
                    break;
                case ACT_RENDER_SCALE:
                    activeIdx = std::max(0, std::min(3, mSettings.renderScale));
                    break;
                case ACT_TARGET_FPS:
                    activeIdx = (mSettings.targetFps >= 144) ? 4 : (mSettings.targetFps >= 120) ? 3 : (mSettings.targetFps >= 90) ? 2 : (mSettings.targetFps >= 60) ? 1 : 0;
                    break;
                case ACT_DISPLAY_REFRESH:
                    activeIdx = (mSettings.targetFps >= 144) ? 3 : (mSettings.targetFps >= 120) ? 2 : (mSettings.targetFps >= 90) ? 1 : 0;
                    break;
                case ACT_ORIENTATION:
                    activeIdx = std::max(0, std::min(2, mSettings.screenOrientationChoice));
                    break;
                case ACT_PERF_HUD:
                    activeIdx = std::max(0, std::min(2, mSettings.perfHudMode));
                    break;
                case ACT_GAME_SPEED:
                    activeIdx = std::max(0, std::min(4, mSettings.gameSpeed - 1));
                    break;
                case ACT_SELECT_SLOT:
                    activeIdx = std::max(0, std::min(3, mCurrentSlot - 1));
                    break;
                case ACT_LANGUAGE:
                    activeIdx = std::max(0, std::min(5, mSettings.languageChoice));
                    break;
                default: break;
                }

                drawSegmentedGroup(rowX0, drawY + 22.0f * uiScale, contentW, 32.0f * uiScale, r.optLabels, r.optCount, activeIdx);
            }
            else if (r.type == ROW_DUAL_BTN) {
                float bW = (contentW - 8.0f * uiScale) * 0.5f;
                float btnY = drawY + 4.0f * uiScale;
                float btnH = rowH - 8.0f * uiScale;
                float b0_x0 = rowX0;
                float b0_x1 = b0_x0 + bW;
                float b1_x0 = b0_x1 + 8.0f * uiScale;
                float b1_x1 = rowX1;

                if (r.action == ACT_LAYOUT_BUTTONS) {
                    drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.14f, 0.24f, 0.40f, 0.95f));
                    drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.35f, 0.65f, 0.98f, 0.60f));
                    drawText("Reposition Layout", (b0_x0 + b0_x1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(0.85f, 0.94f, 1.0f, 1.0f), 1);

                    drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.35f, 0.15f, 0.18f, 0.95f));
                    drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.85f, 0.30f, 0.35f, 0.60f));
                    drawText("Reset Defaults", (b1_x0 + b1_x1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(1.0f, 0.85f, 0.85f, 1.0f), 1);
                }
                else if (r.action == ACT_REPOSITION_STAR_STICK) {
                    drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.24f, 0.18f, 0.06f, 0.95f));
                    drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(1.0f, 0.85f, 0.20f, 0.60f));
                    drawText("Reposition Star Stick", (b0_x0 + b0_x1) * 0.5f, btnY + btnH * 0.62f, 0.32f * uiScale, rgba(1.0f, 0.95f, 0.80f, 1.0f), 1);

                    drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.25f, 0.18f, 0.12f, 0.95f));
                    drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.85f, 0.65f, 0.30f, 0.50f));
                    drawText("Reset Above Buttons", (b1_x0 + b1_x1) * 0.5f, btnY + btnH * 0.62f, 0.32f * uiScale, rgba(0.95f, 0.90f, 0.85f, 1.0f), 1);
                }
                else if (r.action == ACT_GYRO_INVERT_BUTTONS) {
                    Color c0 = mSettings.gyroInvertX ? rgba(0.043f, 0.341f, 0.816f, 0.95f) : rgba(0.16f, 0.18f, 0.24f, 0.85f);
                    Color c1 = mSettings.gyroInvertY ? rgba(0.043f, 0.341f, 0.816f, 0.95f) : rgba(0.16f, 0.18f, 0.24f, 0.85f);
                    drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, c0);
                    drawText(mSettings.gyroInvertX ? "Invert X: ON" : "Invert X: OFF", (b0_x0 + b0_x1) * 0.5f, btnY + btnH * 0.62f, 0.32f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

                    drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, c1);
                    drawText(mSettings.gyroInvertY ? "Invert Y: ON" : "Invert Y: OFF", (b1_x0 + b1_x1) * 0.5f, btnY + btnH * 0.62f, 0.32f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
                }

                else if (r.action == ACT_SAVESTATE_ACTIONS) {
                    drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.08f, 0.32f, 0.45f, 0.95f));
                    drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.25f, 0.85f, 1.0f, 0.65f));
                    drawText("Save State (0ms)", (b0_x0 + b0_x1) * 0.5f, btnY + btnH * 0.62f, 0.32f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

                    drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.35f, 0.22f, 0.08f, 0.95f));
                    drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(1.0f, 0.80f, 0.25f, 0.65f));
                    drawText("Load State (0ms)", (b1_x0 + b1_x1) * 0.5f, btnY + btnH * 0.62f, 0.32f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
                }
                else if (r.action == ACT_SLOT_ACTIONS) {
                    drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.06f, 0.38f, 0.28f, 0.95f));
                    drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.25f, 0.85f, 0.65f, 0.65f));
                    char sTxt[32];
                    if (mCurrentSlot == 4) snprintf(sTxt, sizeof(sTxt), "Save Quick Slot");
                    else snprintf(sTxt, sizeof(sTxt), "Save to Slot %d", mCurrentSlot);
                    drawText(sTxt, (b0_x0 + b0_x1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

                    drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.12f, 0.28f, 0.55f, 0.95f));
                    drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.35f, 0.65f, 0.98f, 0.65f));
                    char rTxt[32];
                    if (mCurrentSlot == 4) snprintf(rTxt, sizeof(rTxt), "Reload Quick Slot");
                    else snprintf(rTxt, sizeof(rTxt), "Reload Slot %d", mCurrentSlot);
                    drawText(rTxt, (b1_x0 + b1_x1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
                }
                else if (r.action == ACT_SD_BACKUP_ACTIONS) {
                    drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.18f, 0.22f, 0.30f, 0.95f));
                    drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.40f, 0.55f, 0.75f, 0.60f));
                    drawText("Export to SD", (b0_x0 + b0_x1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(0.90f, 0.94f, 1.0f, 1.0f), 1);

                    drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.18f, 0.25f, 0.24f, 0.95f));
                    drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.40f, 0.75f, 0.60f, 0.60f));
                    drawText("Import from SD", (b1_x0 + b1_x1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(0.90f, 1.0f, 0.94f, 1.0f), 1);
                }
            }
            else if (r.type == ROW_SINGLE_BTN) {
                float btnY = drawY + 4.0f * uiScale;
                float btnH = rowH - 8.0f * uiScale;

                if (r.action == ACT_VIEW_LOGS) {
                    drawRoundRect(rowX0, btnY, rowX1, btnY + btnH, 12.0f * uiScale, rgba(0.12f, 0.24f, 0.40f, 0.95f));
                    drawRoundRect(rowX0, btnY, rowX1, btnY + btnH, 12.0f * uiScale, rgba(0.35f, 0.65f, 0.98f, 0.55f));
                    drawText("View System Logs Console", (rowX0 + rowX1) * 0.5f, btnY + btnH * 0.62f, 0.35f * uiScale, rgba(0.88f, 0.95f, 1.0f, 1.0f), 1);
                }
                else if (r.action == ACT_EXIT_LAUNCHER) {
                    drawRoundRect(rowX0, btnY, rowX1, btnY + btnH, 12.0f * uiScale, rgba(0.38f, 0.12f, 0.15f, 0.95f));
                    drawRoundRect(rowX0, btnY, rowX1, btnY + btnH, 12.0f * uiScale, rgba(0.85f, 0.25f, 0.30f, 0.60f));
                    drawText("Exit to Setup Launcher", (rowX0 + rowX1) * 0.5f, btnY + btnH * 0.62f, 0.35f * uiScale, rgba(1.0f, 0.88f, 0.88f, 1.0f), 1);
                }
                else if (r.action == ACT_RELOAD_MODS) {
                    drawRoundRect(rowX0, btnY, rowX1, btnY + btnH, 12.0f * uiScale, rgba(0.16f, 0.22f, 0.30f, 0.95f));
                    drawRoundRect(rowX0, btnY, rowX1, btnY + btnH, 12.0f * uiScale, rgba(0.38f, 0.55f, 0.80f, 0.60f));
                    drawText("SCAN & RELOAD MOD DIRECTORY", (rowX0 + rowX1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(0.88f, 0.94f, 1.0f, 1.0f), 1);
                }
            }
            else if (r.type == ROW_QUICK_CARDS) {
                float bW = (contentW - 10.0f * uiScale) * 0.5f;
                float btnY = drawY + 4.0f * uiScale;
                float btnH = rowH - 8.0f * uiScale;
                float sX0 = rowX0;
                float sX1 = sX0 + bW;
                float lX0 = sX1 + 10.0f * uiScale;
                float lX1 = rowX1;

                // Quick Save Card
                drawRoundRect(sX0, btnY, sX1, btnY + btnH, 14.0f * uiScale, rgba(0.05f, 0.38f, 0.28f, 0.95f));
                drawRoundRect(sX0, btnY, sX1, btnY + btnH, 14.0f * uiScale, rgba(0.25f, 0.85f, 0.65f, 0.70f));
                drawCircle(sX0 + 18.0f * uiScale, btnY + btnH * 0.5f, 5.0f * uiScale, rgba(0.35f, 0.98f, 0.75f, 1.0f));
                drawText("QUICK SAVE NOW", (sX0 + sX1) * 0.5f + 6.0f * uiScale, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

                // Quick Load Card
                drawRoundRect(lX0, btnY, lX1, btnY + btnH, 14.0f * uiScale, rgba(0.10f, 0.26f, 0.52f, 0.95f));
                drawRoundRect(lX0, btnY, lX1, btnY + btnH, 14.0f * uiScale, rgba(0.35f, 0.68f, 0.98f, 0.70f));
                drawCircle(lX0 + 18.0f * uiScale, btnY + btnH * 0.5f, 5.0f * uiScale, rgba(0.45f, 0.82f, 1.0f, 1.0f));
                drawText("QUICK LOAD NOW", (lX0 + lX1) * 0.5f + 6.0f * uiScale, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
            }
            else if (r.type == ROW_INFO_CARD) {
                float cardH = rowH - 6.0f * uiScale;
                drawRoundRect(rowX0, drawY + 3.0f * uiScale, rowX1, drawY + 3.0f * uiScale + cardH, 12.0f * uiScale, rgba(0.10f, 0.12f, 0.16f, 0.92f));
                drawRoundRect(rowX0, drawY + 3.0f * uiScale, rowX1, drawY + 3.0f * uiScale + cardH, 12.0f * uiScale, rgba(0.22f, 0.28f, 0.38f, 0.50f));

                if (r.action == ACT_DEVICE_INFO_CARD) {
                    char gpuLine[128], resLine[128], apiLine[128];
                    snprintf(gpuLine, sizeof(gpuLine), "GPU: %s", mGpuRendererName.empty() ? "OpenGL ES 3.2" : mGpuRendererName.c_str());
                    const char* resStr = (mSettings.renderScale == 0) ? "480p" : (mSettings.renderScale == 1) ? "720p" : (mSettings.renderScale == 2) ? "1080p" : "1440p";
                    snprintf(resLine, sizeof(resLine), "Display: %dx%d (%s Target • Hor+)", (int)w, (int)h, resStr);
                    snprintf(apiLine, sizeof(apiLine), "Active Engine: %s • Native ARM64", (mActiveBackend == 1) ? "Vulkan 1.0" : "OpenGL ES 3.2 (fallback)");

                    drawText(gpuLine, rowX0 + 12.0f * uiScale, drawY + 20.0f * uiScale, 0.28f * uiScale, rgba(0.40f, 0.75f, 1.0f, 0.95f), 0);
                    drawText(resLine, rowX0 + 12.0f * uiScale, drawY + 37.0f * uiScale, 0.26f * uiScale, rgba(0.80f, 0.84f, 0.90f, 0.85f), 0);
                    drawText(apiLine, rowX0 + 12.0f * uiScale, drawY + 54.0f * uiScale, 0.24f * uiScale, rgba(0.55f, 0.60f, 0.68f, 0.80f), 0);
                }
                else if (r.action == ACT_STORAGE_INFO_CARD) {
                    drawText("NAND SAVE SYSTEM & STORAGE", rowX0 + 12.0f * uiScale, drawY + 20.0f * uiScale, 0.31f * uiScale, rgba(1.0f, 0.78f, 0.25f, 0.95f), 0);
                    drawText("Directory: /sdcard/GalaxyData/saves", rowX0 + 12.0f * uiScale, drawY + 38.0f * uiScale, 0.26f * uiScale, rgba(0.85f, 0.88f, 0.94f, 0.90f), 0);
                    drawText("Dual persistent snapshot system with CRC32 integrity.", rowX0 + 12.0f * uiScale, drawY + 56.0f * uiScale, 0.25f * uiScale, rgba(0.78f, 0.82f, 0.88f, 0.85f), 0);
                    drawText("Reloading a slot instantly respawns in active galaxy.", rowX0 + 12.0f * uiScale, drawY + 74.0f * uiScale, 0.25f * uiScale, rgba(0.78f, 0.82f, 0.88f, 0.85f), 0);
                    drawText("100% interoperable with Dolphin Wii saves.", rowX0 + 12.0f * uiScale, drawY + 92.0f * uiScale, 0.24f * uiScale, rgba(0.55f, 0.65f, 0.80f, 0.75f), 0);
                }
            }
        }

        // Scrollbar Thumb (rendered inside content area)
        if (maxScroll > 1.0f) {
            float trackH = contentBottom - contentTop;
            float thumbH = std::max(28.0f * uiScale, trackH * (trackH / totalContentH));
            float thumbY = contentTop + (mDrawerScrollY / maxScroll) * (trackH - thumbH);
            drawRoundRect(drawerX + drawerW - 5.0f * uiScale, thumbY, drawerX + drawerW - 2.0f * uiScale, thumbY + thumbH, 1.5f * uiScale, rgba(0.85f, 0.92f, 1.0f, 0.40f));
        }

        // ---------------------------------------------------------
        // B. Fixed Top Header & 4-Tab Dock Overlay
        // ---------------------------------------------------------
        drawRoundRect(drawerX - 60.0f, 0.0f, drawerX + drawerW, contentTop, 0.0f, rgba(0.07f, 0.08f, 0.11f, 1.0f));

        // Header Title & Rotating Mario Star / Cogwheel
        drawCogwheel(drawerX + padX + 14.0f * uiScale, 28.0f * uiScale, 14.0f * uiScale, rgba(0.20f, 0.85f, 1.0f, 1.0f), cogRot);
        drawText("SUPER MARIO GALAXY", drawerX + padX + 36.0f * uiScale, 26.0f * uiScale, 0.40f * uiScale, rgba(0.95f, 0.96f, 0.98f, 1.0f), 0);
        drawText("NATIVE ANDROID EDITION • PLUS", drawerX + padX + 36.0f * uiScale, 38.0f * uiScale, 0.23f * uiScale, rgba(0.20f, 0.85f, 1.0f, 0.90f), 0);

        // Active Backend Pill Tag
        const char* backendTag = (mActiveBackend == 1) ? "VULKAN 1.0" : "GLES 3.2";
        float tagW = 68.0f * uiScale;
        float tagX = drawerX + drawerW - 60.0f * uiScale - tagW;
        drawRoundRect(tagX, 18.0f * uiScale, tagX + tagW, 36.0f * uiScale, 9.0f * uiScale, rgba(0.12f, 0.16f, 0.22f, 0.90f));
        drawRoundRect(tagX, 18.0f * uiScale, tagX + tagW, 36.0f * uiScale, 9.0f * uiScale, rgba(0.30f, 0.45f, 0.65f, 0.50f));
        drawText(backendTag, tagX + tagW * 0.5f, 29.0f * uiScale, 0.24f * uiScale, rgba(0.70f, 0.85f, 1.0f, 0.95f), 1);

        // Close [X] circular button
        float closeBtnX = drawerX + drawerW - 32.0f * uiScale;
        float closeBtnY = 27.0f * uiScale;
        drawCircle(closeBtnX, closeBtnY, 15.0f * uiScale, rgba(0.16f, 0.18f, 0.22f, 0.95f));
        drawCloseIcon(closeBtnX, closeBtnY, 8.0f * uiScale, rgba(0.76f, 0.80f, 0.88f, 0.95f));

        // 4 Modern Tab Pills ([ Controls ]  [ Graphics ]  [ Mods ]  [ Save/Sys ])
        float tabBoxY0 = 54.0f * uiScale;
        float tabBoxY1 = 92.0f * uiScale;
        drawRoundRect(drawerX + padX, tabBoxY0, drawerX + drawerW - padX, tabBoxY1, 14.0f * uiScale, rgba(0.12f, 0.14f, 0.18f, 0.95f));
        drawRoundRect(drawerX + padX, tabBoxY0, drawerX + drawerW - padX, tabBoxY1, 14.0f * uiScale, rgba(0.24f, 0.28f, 0.36f, 0.50f));

        float tabSegW = contentW / 4.0f;
        const char* tabNames[4] = {"Controls", "Graphics", "Mods", "Save/Sys"};

        // Highlight Pill for Active Tab
        float hX0 = drawerX + padX + mSettingsTab * tabSegW;
        float hX1 = hX0 + tabSegW;
        drawRoundRect(hX0 + 2.0f * uiScale, tabBoxY0 + 2.0f * uiScale, hX1 - 2.0f * uiScale, tabBoxY1 - 2.0f * uiScale, 12.0f * uiScale, rgba(0.18f, 0.26f, 0.42f, 0.95f));
        drawRoundRect(hX0 + 2.0f * uiScale, tabBoxY0 + 2.0f * uiScale, hX1 - 2.0f * uiScale, tabBoxY1 - 2.0f * uiScale, 12.0f * uiScale, themeColor);

        for (int t = 0; t < 4; t++) {
            float tx0 = drawerX + padX + t * tabSegW;
            float tx1 = tx0 + tabSegW;
            bool active = (t == mSettingsTab);
            Color tCol = active ? rgba(1.0f, 1.0f, 1.0f, 1.0f) : rgba(0.65f, 0.70f, 0.78f, 0.75f);
            drawText(tabNames[t], (tx0 + tx1) * 0.5f, tabBoxY0 + 23.0f * uiScale, 0.30f * uiScale, tCol, 1);
        }

        // Divider below tab bar
        drawRoundRect(drawerX + padX, contentTop - 1.0f, drawerX + drawerW - padX, contentTop, 0.5f, rgba(0.267f, 0.278f, 0.310f, 0.45f));

        // ---------------------------------------------------------
        // C. Fixed Bottom Bar Mask Overlay & [ Close Menu ] Button
        // ---------------------------------------------------------
        drawRoundRect(drawerX - 60.0f, contentBottom, drawerX + drawerW, h, 0.0f, rgba(0.07f, 0.08f, 0.11f, 1.0f));
        drawRoundRect(drawerX + padX, contentBottom, drawerX + drawerW - padX, contentBottom + 1.0f, 0.5f, rgba(0.267f, 0.278f, 0.310f, 0.45f));

        float doneY = h - 46.0f * uiScale;
        float doneX0 = drawerX + padX;
        float doneX1 = drawerX + drawerW - padX;
        drawRoundRect(doneX0, doneY, doneX1, doneY + 36.0f * uiScale, 18.0f * uiScale, rgba(0.043f, 0.341f, 0.816f, 0.95f));
        drawRoundRect(doneX0, doneY, doneX1, doneY + 36.0f * uiScale, 18.0f * uiScale, rgba(0.35f, 0.70f, 1.0f, 0.60f));
        drawText("CLOSE MENU", (doneX0 + doneX1) * 0.5f, doneY + 23.0f * uiScale, 0.36f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
    }

    // -------------------------------------------------------------
    // Full-Screen In-Game Log Viewer Modal (Material 3 Dark Console)
    // -------------------------------------------------------------
    if (mLogViewerOpen) {
        // Scrim backdrop
        drawRoundRect(0, 0, w, h, 0.0f, rgba(0.0f, 0.0f, 0.0f, 0.85f));

        float modalPad = 24.0f * uiScale;
        float modalX0 = modalPad;
        float modalY0 = modalPad;
        float modalX1 = w - modalPad;
        float modalY1 = h - modalPad;

        // Modal container
        drawRoundRect(modalX0, modalY0, modalX1, modalY1, 16.0f * uiScale, rgba(0.08f, 0.09f, 0.12f, 0.98f));
        drawRoundRect(modalX0, modalY0, modalX1, modalY1, 16.0f * uiScale, rgba(0.25f, 0.35f, 0.50f, 0.60f));

        // Header bar
        float headerH = 46.0f * uiScale;
        drawText("SYSTEM LOG VIEWER (REALTIME BUFFER)", modalX0 + 20.0f * uiScale, modalY0 + 29.0f * uiScale, 0.38f * uiScale, rgba(0.38f, 0.74f, 0.98f, 1.0f), 0);

        // Close button [X]
        float closeX = modalX1 - 24.0f * uiScale;
        float closeY = modalY0 + 23.0f * uiScale;
        drawCircle(closeX, closeY, 16.0f * uiScale, rgba(0.20f, 0.22f, 0.28f, 0.95f));
        drawCloseIcon(closeX, closeY, 9.0f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f));

        // Separator line
        drawLine(modalX0, modalY0 + headerH, modalX1, modalY0 + headerH, 1.2f * uiScale, rgba(0.25f, 0.28f, 0.35f, 0.60f));

        // Fetch logs from C/C++ backend
        char logBuffer[16384];
        int logLen = port_get_recent_logs(logBuffer, sizeof(logBuffer));
        if (logLen > 0) {
            float lineY = modalY0 + headerH + 20.0f * uiScale;
            char* curLine = logBuffer;
            int count = 0;
            while (*curLine && lineY < modalY1 - 12.0f * uiScale && count < 32) {
                char* nextLine = strchr(curLine, '\n');
                if (nextLine) *nextLine = '\0';
                drawText(curLine, modalX0 + 18.0f * uiScale, lineY, 0.27f * uiScale, rgba(0.85f, 0.88f, 0.92f, 0.95f), 0);
                lineY += 15.0f * uiScale;
                count++;
                if (!nextLine) break;
                curLine = nextLine + 1;
            }
        } else {
            drawText("No system logs recorded yet.", modalX0 + 20.0f * uiScale, modalY0 + headerH + 30.0f * uiScale, 0.34f * uiScale, rgba(0.60f, 0.65f, 0.72f, 0.85f), 0);
        }
    }

    // -------------------------------------------------------------
    // Save Anywhere Notification Toast (Floating Pill)
    // -------------------------------------------------------------
    if (mSaveToastTimer > 0.0f) {
        float toastAlpha = std::min(1.0f, mSaveToastTimer * 2.0f);
        float toastW = 320.0f * uiScale;
        float toastH = 44.0f * uiScale;
        float toastX = (w - toastW) * 0.5f;
        float toastY = 56.0f * uiScale;

        // Container: dark emerald frosted pill
        drawRoundRect(toastX, toastY, toastX + toastW, toastY + toastH, 22.0f * uiScale, rgba(0.04f, 0.15f, 0.11f, 0.95f * toastAlpha));
        drawRoundRect(toastX, toastY, toastX + toastW, toastY + toastH, 22.0f * uiScale, rgba(0.20f, 0.82f, 0.55f, 0.85f * toastAlpha));

        // Green checkmark indicator circle
        drawCircle(toastX + 24.0f * uiScale, toastY + toastH * 0.5f, 9.0f * uiScale, rgba(0.18f, 0.85f, 0.55f, toastAlpha));
        drawCircle(toastX + 24.0f * uiScale, toastY + toastH * 0.5f, 4.0f * uiScale, rgba(1.0f, 1.0f, 1.0f, toastAlpha));

        drawText(mToastMessage.empty() ? "Game Progress Saved Anywhere!" : mToastMessage.c_str(), toastX + 42.0f * uiScale, toastY + toastH * 0.5f + 5.0f * uiScale, 0.35f * uiScale, rgba(0.95f, 1.0f, 0.97f, toastAlpha), 0);
    }

}

void UIOverlay::render(int screenW, int screenH) {
    if (!init()) return;

    std::vector<UIVertex> verts;
    generateVertices(screenW, screenH, verts);
    if (verts.empty()) return;

    float w = (float)screenW;
    float h = (float)screenH;

    // -------------------------------------------------------------
    // Single GLES Draw Call
    // -------------------------------------------------------------
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(mProg);

    float proj[16] = {
        2.0f / w, 0.0f,      0.0f, 0.0f,
        0.0f,     -2.0f / h, 0.0f, 0.0f,
        0.0f,     0.0f,     -1.0f, 0.0f,
        -1.0f,    1.0f,      0.0f, 1.0f
    };
    GLint uProjLoc = glGetUniformLocation(mProg, "uProj");
    glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, proj);

    GLint uFontLoc = glGetUniformLocation(mProg, "uFontTex");
    glUniform1i(uFontLoc, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mFontTex);

    glBindVertexArray(mVao);
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(UIVertex), verts.data(), GL_STREAM_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());

    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void UIOverlay::setEditMode(bool active) {
    mEditLayoutMode = active;
    if (active) {
        mSettingsOpen = false;
    }
    mDirty = true;
}

void UIOverlay::clearLayoutOffsets() {
    mSettings.stickOffsetX = 0.0f;
    mSettings.stickOffsetY = 0.0f;
    mSettings.buttonOffsetX = 0.0f;
    mSettings.buttonOffsetY = 0.0f;
    mSettings.btnAOffsetX = 0.0f;
    mSettings.btnAOffsetY = 0.0f;
    mSettings.btnBOffsetX = 0.0f;
    mSettings.btnBOffsetY = 0.0f;
    mSettings.btnZOffsetX = 0.0f;
    mSettings.btnZOffsetY = 0.0f;
    mSettings.btnSpinOffsetX = 0.0f;
    mSettings.btnSpinOffsetY = 0.0f;
    mSettings.starStickOffsetX = 0.0f;
    mSettings.starStickOffsetY = 0.0f;
    mSettings.dpadOffsetX = 0.0f;
    mSettings.dpadOffsetY = 0.0f;
}

void UIOverlay::resetLayoutOffsets() {
    clearLayoutOffsets();
    if (!mSavePath.empty()) {
        mSettings.save(mSavePath);
    }
    mDirty = true;
}

bool UIOverlay::exportSave() {
    std::string internalPath = mSavePath;
    size_t lastSlash = internalPath.rfind('/');
    if (lastSlash == std::string::npos) return false;
    std::string saveRoot = internalPath.substr(0, lastSlash);

    // SMG official save name is GameData.bin (also check save.bin and all regions)
    const char* titles[] = {"524d4750", "524d4745", "524d474a", "524d474b"};
    const char* fileNames[] = {"GameData.bin", "save.bin"};
    std::string srcSave;
    FILE* in = nullptr;

    for (const char* t : titles) {
        for (const char* fn : fileNames) {
            std::string candidate = saveRoot + "/title/00010000/" + t + "/data/" + fn;
            in = fopen(candidate.c_str(), "rb");
            if (in) { srcSave = candidate; break; }
        }
        if (in) break;
    }
    if (!in) {
        for (const char* fn : fileNames) {
            std::string candidate = saveRoot + "/" + fn;
            in = fopen(candidate.c_str(), "rb");
            if (in) { srcSave = candidate; break; }
        }
    }
    if (!in) {
        // Also check external backup mirror
        std::string extCandidate = "/sdcard/GalaxyData/saves/GameData.bin";
        in = fopen(extCandidate.c_str(), "rb");
        if (in) srcSave = extCandidate;
    }
    if (!in) {
        showToast("No save data found yet (play & save first)!");
        return false;
    }

    // Write to both GalaxyAndroid_save.bin and GameData.bin in Downloads
    mkdir("/sdcard/Download", 0770);
    std::string dstSave1 = "/sdcard/Download/GalaxyAndroid_save.bin";
    std::string dstSave2 = "/sdcard/Download/GameData.bin";

    FILE* out1 = fopen(dstSave1.c_str(), "wb");
    FILE* out2 = fopen(dstSave2.c_str(), "wb");
    if (!out1 && !out2) {
        fclose(in);
        showToast("Cannot write to Downloads folder!");
        return false;
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (out1) fwrite(buf, 1, n, out1);
        if (out2) fwrite(buf, 1, n, out2);
    }
    fclose(in);
    if (out1) fclose(out1);
    if (out2) fclose(out2);

    showToast("Exported to Download/GalaxyAndroid_save.bin!");
    return true;
}

bool UIOverlay::importSave() {
    std::string internalPath = mSavePath;
    size_t lastSlash = internalPath.rfind('/');
    if (lastSlash == std::string::npos) return false;
    std::string saveRoot = internalPath.substr(0, lastSlash);

    // Look for save in Downloads
    const char* downloadCandidates[] = {
        "/sdcard/Download/GalaxyAndroid_save.bin",
        "/sdcard/Download/GameData.bin",
        "/sdcard/Download/save.bin"
    };
    FILE* in = nullptr;
    for (const char* path : downloadCandidates) {
        in = fopen(path, "rb");
        if (in) break;
    }
    if (!in) {
        showToast("Put GalaxyAndroid_save.bin in Downloads!");
        return false;
    }

    std::vector<uint8_t> data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        data.insert(data.end(), buf, buf + n);
    }
    fclose(in);

    if (data.empty()) {
        showToast("Save file in Downloads is empty!");
        return false;
    }

    // Import into all region directories (PAL, NTSC-U, NTSC-J, NTSC-K)
    const char* titles[] = {"524d4750", "524d4745", "524d474a", "524d474b"};
    mkdir((saveRoot + "/title").c_str(), 0770);
    mkdir((saveRoot + "/title/00010000").c_str(), 0770);
    for (const char* t : titles) {
        std::string dir = saveRoot + "/title/00010000/" + t + "/data";
        mkdir((saveRoot + "/title/00010000/" + t).c_str(), 0770);
        mkdir(dir.c_str(), 0770);
        FILE* out = fopen((dir + "/GameData.bin").c_str(), "wb");
        if (out) {
            fwrite(data.data(), 1, data.size(), out);
            fclose(out);
        }
        FILE* outOld = fopen((dir + "/save.bin").c_str(), "wb");
        if (outOld) {
            fwrite(data.data(), 1, data.size(), outOld);
            fclose(outOld);
        }
    }
    // Also save to external mirror in GalaxyData/saves
    mkdir("/sdcard/GalaxyData", 0770);
    mkdir("/sdcard/GalaxyData/saves", 0770);
    FILE* outExt = fopen("/sdcard/GalaxyData/saves/GameData.bin", "wb");
    if (outExt) {
        fwrite(data.data(), 1, data.size(), outExt);
        fclose(outExt);
    }

    showToast("Imported save data successfully!");
    return true;
}

}  // namespace mobile_ui
