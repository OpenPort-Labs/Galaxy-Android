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

#include "port/port.h"
#include "xr/ui_font.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MobileUI", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MobileUI", __VA_ARGS__)

namespace mobile_ui {

extern "C" void port_android_exit_to_launcher();
extern "C" void port_set_game_speed(float speed);
extern "C" float port_get_game_speed(void);
extern "C" void port_language_set(const char* name);

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

inline void applyLanguage(int langIdx) {
    switch (langIdx) {
        case 1: port_language_set("english"); break;
        case 2: port_language_set("spanish"); break;
        case 3: port_language_set("french"); break;
        case 4: port_language_set("german"); break;
        case 5: port_language_set("italian"); break;
        default: break;
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
            else if (!strcmp(key, "show_dpad")) showDpad = (val > 0.5f);
            else if (!strcmp(key, "dpad_on_right")) dpadOnRight = (val > 0.5f);
            else if (!strcmp(key, "dpad_offset_x")) dpadOffsetX = val;
            else if (!strcmp(key, "dpad_offset_y")) dpadOffsetY = val;
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
            else if (!strcmp(key, "graphics_backend")) {
                graphicsBackend = (int)val;
            }
            else if (!strcmp(key, "relative_pointer")) relativePointer = (val > 0.5f);
            else if (!strcmp(key, "screen_orientation")) screenOrientationChoice = std::max(0, std::min(2, (int)val));
        }
    }
    fclose(f);
    LOGI("Loaded settings: show=%d, full=%d, touch=%d, hud=%d, backend=%d, relPtr=%d, orient=%d, saver=%d, gyro=%d, invX=%d, invY=%d, op=%.2f, scale=%.2f, dpad=%d, fly=%d, skip=%d, infHp=%d, infLives=%d, infBits=%d, spd=%d, lang=%d, res=%d, hap=%d",
         showControls, fullscreen, fullTouchMode, perfHudMode, graphicsBackend, relativePointer ? 1 : 0, screenOrientationChoice, batterySaver ? 1 : 0, gyroAiming ? 1 : 0, gyroInvertX ? 1 : 0, gyroInvertY ? 1 : 0, opacity, scale, showDpad, modFly, modSkipCutscenes, modInfiniteHealth, modInfiniteLives, modInfiniteStarBits, gameSpeed, languageChoice, renderScale, haptics ? 1 : 0);
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
    fprintf(f, "opacity=%.2f\n", opacity);
    fprintf(f, "scale=%.2f\n", scale);
    fprintf(f, "dynamic_stick=%d\n", dynamicStick ? 1 : 0);
    fprintf(f, "stick_offset_x=%.1f\n", stickOffsetX);
    fprintf(f, "stick_offset_y=%.1f\n", stickOffsetY);
    fprintf(f, "button_offset_x=%.1f\n", buttonOffsetX);
    fprintf(f, "button_offset_y=%.1f\n", buttonOffsetY);
    fprintf(f, "show_dpad=%d\n", showDpad ? 1 : 0);
    fprintf(f, "dpad_on_right=%d\n", dpadOnRight ? 1 : 0);
    fprintf(f, "dpad_offset_x=%.1f\n", dpadOffsetX);
    fprintf(f, "dpad_offset_y=%.1f\n", dpadOffsetY);
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

static void copyDirRecursive(const std::string& src, const std::string& dst) {
    mkdir(dst.c_str(), 0770);
    DIR* dir = opendir(src.c_str());
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        std::string sPath = src + "/" + entry->d_name;
        std::string dPath = dst + "/" + entry->d_name;
        struct stat st;
        if (stat(sPath.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                copyDirRecursive(sPath, dPath);
            } else if (S_ISREG(st.st_mode)) {
                FILE* in = fopen(sPath.c_str(), "rb");
                FILE* out = fopen(dPath.c_str(), "wb");
                if (in && out) {
                    char buf[8192];
                    size_t bytes;
                    while ((bytes = fread(buf, 1, sizeof(buf), in)) > 0) {
                        fwrite(buf, 1, bytes, out);
                    }
                }
                if (in) fclose(in);
                if (out) fclose(out);
            }
        }
    }
    closedir(dir);
}

void UIOverlay::triggerSaveAnywhere() {
    port_trigger_quick_save();
    if (!mSavePath.empty()) {
        size_t lastSlash = mSavePath.find_last_of('/');
        if (lastSlash != std::string::npos) {
            std::string nandDir = mSavePath.substr(0, lastSlash);
            mkdir("/sdcard/GalaxyData", 0775);
            mkdir("/sdcard/GalaxyData/saves", 0775);
            copyDirRecursive(nandDir, "/sdcard/GalaxyData/saves/current");
        }
    }
    if (mSettings.haptics) port_android_vibrate(50);
    showToast("Game Progress Saved Anywhere! (Mirrored to /sdcard/GalaxyData/saves)");
    LOGI("Save anywhere requested and triggered from Mobile UI");
}

void UIOverlay::triggerSaveSlot(int slot) {
    port_trigger_quick_save();
    if (mSavePath.empty()) return;
    size_t lastSlash = mSavePath.find_last_of('/');
    if (lastSlash == std::string::npos) return;
    std::string nandDir = mSavePath.substr(0, lastSlash);
    std::string slotDir = nandDir + "_slot" + std::to_string(slot);
    copyDirRecursive(nandDir, slotDir);

    mkdir("/sdcard/GalaxyData", 0775);
    mkdir("/sdcard/GalaxyData/saves", 0775);
    std::string pubSlot = "/sdcard/GalaxyData/saves/slot" + std::to_string(slot);
    copyDirRecursive(nandDir, pubSlot);
    copyDirRecursive(nandDir, "/sdcard/GalaxyData/saves/current");

    if (mSettings.haptics) port_android_vibrate(50);
    char msg[96];
    snprintf(msg, sizeof(msg), "Saved to Slot %d! (GalaxyData/saves)", slot);
    showToast(msg);
    LOGI("NAND saved to slot %d: %s and mirrored to %s", slot, slotDir.c_str(), pubSlot.c_str());
}

void UIOverlay::triggerLoadSlot(int slot) {
    if (mSavePath.empty()) return;
    size_t lastSlash = mSavePath.find_last_of('/');
    if (lastSlash == std::string::npos) return;
    std::string nandDir = mSavePath.substr(0, lastSlash);
    std::string slotDir = nandDir + "_slot" + std::to_string(slot);
    struct stat st;
    if (stat(slotDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        std::string pubSlot = "/sdcard/GalaxyData/saves/slot" + std::to_string(slot);
        if (stat(pubSlot.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            slotDir = pubSlot;
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), "Slot %d is empty!", slot);
            showToast(msg);
            return;
        }
    }
    copyDirRecursive(slotDir, nandDir);
    if (mSettings.haptics) port_android_vibrate(75);
    char msg[64];
    snprintf(msg, sizeof(msg), "Slot %d Loaded!", slot);
    showToast(msg);
    LOGI("NAND restored from slot %d: %s", slot, slotDir.c_str());
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
                mSettings.stickOffsetX = 0.0f;
                mSettings.stickOffsetY = 0.0f;
                mSettings.buttonOffsetX = 0.0f;
                mSettings.buttonOffsetY = 0.0f;
                mSettings.dpadOffsetX = 0.0f;
                mSettings.dpadOffsetY = 0.0f;
                if (!mSavePath.empty()) mSettings.save(mSavePath);
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

            if (mSettings.showDpad && dDpad2 <= (95.0f * scale) * (95.0f * scale)) {
                mDraggingCluster = 3; // D-Pad
                mDragStartX = px;
                mDragStartY = py;
                mInitialDpadOfsX = mSettings.dpadOffsetX;
                mInitialDpadOfsY = mSettings.dpadOffsetY;
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
            if (mDraggingCluster == 1) {
                mSettings.stickOffsetX = mInitialStickOfsX + (px - mDragStartX);
                mSettings.stickOffsetY = mInitialStickOfsY + (py - mDragStartY);
            } else if (mDraggingCluster == 2) {
                mSettings.buttonOffsetX = mInitialBtnOfsX + (px - mDragStartX);
                mSettings.buttonOffsetY = mInitialBtnOfsY + (py - mDragStartY);
            } else if (mDraggingCluster == 3) {
                mSettings.dpadOffsetX = mInitialDpadOfsX + (px - mDragStartX);
                mSettings.dpadOffsetY = mInitialDpadOfsY + (py - mDragStartY);
            }
            mDirty = true;
            return true;
        } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
            mDraggingCluster = 0;
            if (!mSavePath.empty()) mSettings.save(mSavePath);
            mDirty = true;
            return true;
        }
        return true;
    }

    // Handle Material 3 Side Sheet Drawer Touches (Tabbed & Scrollable)
    if (mSettingsOpen || mDrawerAnim > 0.05f) {
        float padX = 22.0f * uiScale;
        float contentW = drawerW - padX * 2.0f;
        float contentTop = mPlusEdition ? (112.0f * uiScale) : (68.0f * uiScale);
        float contentBottom = h - 56.0f * uiScale;
        float totalContentH = (mSettingsTab == 0 || !mPlusEdition) ? (1520.0f * uiScale) : (440.0f * uiScale);
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
                    float contentRelY = py - contentTop + mDrawerScrollY;

                    if (mSettingsTab == 0 || !mPlusEdition) {
                        // General Tab items
                        // Row 0: Quick Save Anywhere Action Button
                        if (contentRelY >= 0.0f && contentRelY <= 52.0f * uiScale) {
                            triggerSaveAnywhere();
                        }
                        // Row 1: Visibility Switch
                        else if (contentRelY >= 54.0f * uiScale && contentRelY <= 102.0f * uiScale) {
                            mSettings.showControls = !mSettings.showControls;
                        }
                        // Row 2: Camera D-Pad Switch
                        else if (contentRelY >= 104.0f * uiScale && contentRelY <= 152.0f * uiScale) {
                            mSettings.showDpad = !mSettings.showDpad;
                        }
                        // Row 3: D-Pad Position Segmented
                        else if (contentRelY >= 156.0f * uiScale && contentRelY <= 208.0f * uiScale) {
                            float midX = drawerX + padX + contentW * 0.5f;
                            if (px < midX) {
                                mSettings.dpadOnRight = false;
                                mSettings.dpadOffsetX = 0.0f;
                                mSettings.dpadOffsetY = 0.0f;
                            } else {
                                mSettings.dpadOnRight = true;
                                mSettings.dpadOffsetX = 0.0f;
                                mSettings.dpadOffsetY = 0.0f;
                            }
                        }
                        // Row 4: Fullscreen Switch
                        else if (contentRelY >= 212.0f * uiScale && contentRelY <= 260.0f * uiScale) {
                            mSettings.fullscreen = !mSettings.fullscreen;
                        }
                        // Row 5: Full Touch Mode Switch
                        else if (contentRelY >= 262.0f * uiScale && contentRelY <= 310.0f * uiScale) {
                            mSettings.fullTouchMode = !mSettings.fullTouchMode;
                        }
                        // Row 6: Joystick Mode Segmented
                        else if (contentRelY >= 314.0f * uiScale && contentRelY <= 368.0f * uiScale) {
                            float midX = drawerX + padX + contentW * 0.5f;
                            if (px < midX) mSettings.dynamicStick = true;
                            else mSettings.dynamicStick = false;
                        }
                        // Row 7: Controls Opacity Segmented
                        else if (contentRelY >= 372.0f * uiScale && contentRelY <= 426.0f * uiScale) {
                            float segW = contentW / 4.0f;
                            int idx = (int)((px - (drawerX + padX)) / segW);
                            if (idx <= 0) mSettings.opacity = 0.25f;
                            else if (idx == 1) mSettings.opacity = 0.50f;
                            else if (idx == 2) mSettings.opacity = 0.75f;
                            else mSettings.opacity = 1.00f;
                        }
                        // Row 8: Button Size Segmented
                        else if (contentRelY >= 430.0f * uiScale && contentRelY <= 484.0f * uiScale) {
                            float segW = contentW / 3.0f;
                            int idx = (int)((px - (drawerX + padX)) / segW);
                            if (idx <= 0) mSettings.scale = 0.85f;
                            else if (idx == 1) mSettings.scale = 1.00f;
                            else mSettings.scale = 1.25f;
                        }
                        // Row 9: Layout Reposition / Reset
                        else if (contentRelY >= 488.0f * uiScale && contentRelY <= 544.0f * uiScale) {
                            float splitX = drawerX + padX + contentW * 0.56f;
                            if (px < splitX) {
                                mSettingsOpen = false;
                                mEditLayoutMode = true;
                            } else {
                                mSettings.stickOffsetX = 0.0f;
                                mSettings.stickOffsetY = 0.0f;
                                mSettings.buttonOffsetX = 0.0f;
                                mSettings.buttonOffsetY = 0.0f;
                                mSettings.dpadOffsetX = 0.0f;
                                mSettings.dpadOffsetY = 0.0f;
                            }
                        }
                        // Row 10: View System Logs
                        else if (contentRelY >= 548.0f * uiScale && contentRelY <= 600.0f * uiScale) {
                            mSettingsOpen = false;
                            mLogViewerOpen = true;
                            mDirty = true;
                        }
                        // Row 11: Hardware Monitor HUD
                        else if (contentRelY >= 602.0f * uiScale && contentRelY < 660.0f * uiScale) {
                            float segW = contentW / 3.0f;
                            int idx = (int)((px - (drawerX + padX)) / segW);
                            if (idx <= 0) mSettings.perfHudMode = 0; // Off
                            else if (idx == 1) mSettings.perfHudMode = 1; // FPS
                            else mSettings.perfHudMode = 2; // Full Stats
                        }
                        // Row 12: Target Frame Rate
                        else if (contentRelY >= 660.0f * uiScale && contentRelY < 718.0f * uiScale) {
                            float segW = contentW / 5.0f;
                            float relX = px - (drawerX + padX);
                            int idx = std::max(0, std::min(4, (int)(relX / segW)));
                            if (idx == 0) {
                                mSettings.targetFps = 30;
                                mSettings.batterySaver = true;
                                showToast("Target FPS: 30 FPS (Battery Saver)");
                            } else if (idx == 1) {
                                mSettings.targetFps = 60;
                                mSettings.batterySaver = false;
                                showToast("Target FPS: 60 FPS (Native Standard)");
                            } else if (idx == 2) {
                                mSettings.targetFps = 90;
                                mSettings.batterySaver = false;
                                showToast("Target FPS: 90 FPS (Smooth Refresh)");
                            } else if (idx == 3) {
                                mSettings.targetFps = 120;
                                mSettings.batterySaver = false;
                                showToast("Target FPS: 120 FPS (Ultra Smooth)");
                            } else {
                                mSettings.targetFps = 144;
                                mSettings.batterySaver = false;
                                showToast("Target FPS: 144 FPS (Max Refresh)");
                            }
                            if (!mSavePath.empty()) mSettings.save(mSavePath);
                            mDirty = true;
                        }
                        // Row 13: Graphics Backend
                        else if (contentRelY >= 718.0f * uiScale && contentRelY < 776.0f * uiScale) {
                            float midX = drawerX + padX + contentW * 0.5f;
                            if (px < midX) {
                                mSettings.graphicsBackend = 0; // OpenGL ES 3.2
                                showToast("Backend: OpenGL ES 3.2 (Active & Stable)");
                            } else {
                                mSettings.graphicsBackend = 1; // Vulkan 1.0
                                showToast("Backend: Vulkan 1.0 (Core Engine)");
                            }
                        }
                        // Row 14: Game Speed Multiplier (Turbo / Fast-Forward)
                        else if (contentRelY >= 776.0f * uiScale && contentRelY < 834.0f * uiScale) {
                            float segW = contentW / 5.0f;
                            int idx = (int)((px - (drawerX + padX)) / segW);
                            mSettings.gameSpeed = std::max(1, std::min(5, idx + 1));
                            applyGameSpeed(mSettings.gameSpeed);
                            showToast(mSettings.gameSpeed == 1 ? "Game Speed: 1.0x (Normal)" :
                                      mSettings.gameSpeed == 2 ? "Game Speed: 1.5x" :
                                      mSettings.gameSpeed == 3 ? "Game Speed: 2.0x (Fast)" :
                                      mSettings.gameSpeed == 4 ? "Game Speed: 3.0x" : "Game Speed: 4.0x (Turbo)");
                        }
                        // Row 15: Game Language
                        else if (contentRelY >= 834.0f * uiScale && contentRelY < 892.0f * uiScale) {
                            float segW = contentW / 6.0f;
                            int idx = (int)((px - (drawerX + padX)) / segW);
                            mSettings.languageChoice = std::max(0, std::min(5, idx));
                            applyLanguage(mSettings.languageChoice);
                            showToast(mSettings.languageChoice == 0 ? "Language: System Auto" :
                                      mSettings.languageChoice == 1 ? "Language: English" :
                                      mSettings.languageChoice == 2 ? "Language: Español" :
                                      mSettings.languageChoice == 3 ? "Language: Français" :
                                      mSettings.languageChoice == 4 ? "Language: Deutsch" : "Language: Italiano");
                        }
                        // Row 16: Render Resolution
                        else if (contentRelY >= 892.0f * uiScale && contentRelY < 950.0f * uiScale) {
                            float segW = contentW / 4.0f;
                            int idx = (int)((px - (drawerX + padX)) / segW);
                            mSettings.renderScale = std::max(0, std::min(3, idx));
                            showToast(mSettings.renderScale == 0 ? "Resolution: 480p (Max Performance)" :
                                      mSettings.renderScale == 1 ? "Resolution: 720p (HD Balanced)" :
                                      mSettings.renderScale == 2 ? "Resolution: 1080p (Full HD)" : "Resolution: 1440p (2K Ultra)");
                        }
                        // Row 17: Vibration Haptics
                        else if (contentRelY >= 950.0f * uiScale && contentRelY < 1004.0f * uiScale) {
                            mSettings.haptics = !mSettings.haptics;
                            showToast(mSettings.haptics ? "Vibration Haptics: ON" : "Vibration Haptics: OFF");
                        }
                        // Row 18: Battery Saver Switch
                        else if (contentRelY >= 1004.0f * uiScale && contentRelY < 1058.0f * uiScale) {
                            mSettings.batterySaver = !mSettings.batterySaver;
                            if (mSettings.batterySaver) {
                                mSettings.targetFps = 30;
                            } else if (mSettings.targetFps <= 30) {
                                mSettings.targetFps = 60;
                            }
                            if (!mSavePath.empty()) mSettings.save(mSavePath);
                            mDirty = true;
                            showToast(mSettings.batterySaver ? "Battery Saver: ON (30 FPS Limit)" : "Battery Saver: OFF (Performance Mode)");
                        }
                        // Row 19: Gyroscope Star Pointer Switch
                        else if (contentRelY >= 1058.0f * uiScale && contentRelY < 1112.0f * uiScale) {
                            mSettings.gyroAiming = !mSettings.gyroAiming;
                            showToast(mSettings.gyroAiming ? "Gyro Aiming: ON (Tilt Device to Aim)" : "Gyro Aiming: OFF");
                        }
                        // Row 19b: Gyro Invert X / Invert Y Buttons
                        else if (contentRelY >= 1112.0f * uiScale && contentRelY < 1162.0f * uiScale) {
                            float midX = drawerX + padX + contentW * 0.5f;
                            if (px < midX) {
                                mSettings.gyroInvertX = !mSettings.gyroInvertX;
                                showToast(mSettings.gyroInvertX ? "Gyro Invert Horizontal (X): ON" : "Gyro Invert Horizontal (X): OFF");
                            } else {
                                mSettings.gyroInvertY = !mSettings.gyroInvertY;
                                showToast(mSettings.gyroInvertY ? "Gyro Invert Vertical (Y): ON" : "Gyro Invert Vertical (Y): OFF");
                            }
                        }
                        // Row 19c: Relative Pointer Switch (Issue #11)
                        else if (contentRelY >= 1162.0f * uiScale && contentRelY < 1216.0f * uiScale) {
                            mSettings.relativePointer = !mSettings.relativePointer;
                            showToast(mSettings.relativePointer ? "Relative Pointer: ON (Trackpad mode)" : "Relative Pointer: OFF (Direct touch)");
                        }
                        // Row 19d: Screen Orientation Choice (Issues #18 & #19)
                        else if (contentRelY >= 1216.0f * uiScale && contentRelY < 1276.0f * uiScale) {
                            float segW = contentW / 3.0f;
                            int idx = (int)((px - (drawerX + padX)) / segW);
                            mSettings.screenOrientationChoice = std::max(0, std::min(2, idx));
                            showToast(mSettings.screenOrientationChoice == 0 ? "Orientation: Auto Sensor (Flips 180°)" :
                                      mSettings.screenOrientationChoice == 1 ? "Orientation: Fixed Landscape" : "Orientation: Fixed Reverse Landscape");
                        }
                        // Row 20: Save Backup (Export / Import)
                        else if (contentRelY >= 1276.0f * uiScale && contentRelY < 1336.0f * uiScale) {
                            float midX = drawerX + padX + contentW * 0.5f;
                            if (px < midX) {
                                exportSave();
                            } else {
                                importSave();
                            }
                        }
                        // Row 22: Return to Setup / Launcher Button
                        else if (contentRelY >= 1410.0f * uiScale && contentRelY < 1490.0f * uiScale) {
                            mSettingsOpen = false;
                            port_android_exit_to_launcher();
                            return true;
                        }
                    } else {
                        // Advanced / Cheats Tab items
                        // Mod 1: Fly Mode Switch
                        if (contentRelY >= 48.0f * uiScale && contentRelY <= 104.0f * uiScale) {
                            mSettings.modFly = !mSettings.modFly;
                            showToast(mSettings.modFly ? "Fly Mode: ON" : "Fly Mode: OFF");
                        }
                        // Mod 2: Cutscene Prevention Switch
                        else if (contentRelY >= 106.0f * uiScale && contentRelY <= 160.0f * uiScale) {
                            mSettings.modSkipCutscenes = !mSettings.modSkipCutscenes;
                            showToast(mSettings.modSkipCutscenes ? "Skip Cutscenes: ON" : "Skip Cutscenes: OFF");
                        }
                        // Mod 3: Infinite Health Switch
                        else if (contentRelY >= 162.0f * uiScale && contentRelY <= 216.0f * uiScale) {
                            mSettings.modInfiniteHealth = !mSettings.modInfiniteHealth;
                            showToast(mSettings.modInfiniteHealth ? "Infinite Health: ON" : "Infinite Health: OFF");
                        }
                        // Mod 4: Infinite Lives Switch
                        else if (contentRelY >= 218.0f * uiScale && contentRelY <= 272.0f * uiScale) {
                            mSettings.modInfiniteLives = !mSettings.modInfiniteLives;
                            showToast(mSettings.modInfiniteLives ? "Infinite Lives (99): ON" : "Infinite Lives: OFF");
                        }
                        // Mod 5: Infinite Star Bits Switch
                        else if (contentRelY >= 274.0f * uiScale && contentRelY <= 334.0f * uiScale) {
                            mSettings.modInfiniteStarBits = !mSettings.modInfiniteStarBits;
                            showToast(mSettings.modInfiniteStarBits ? "Infinite Star Bits (999): ON" : "Infinite Star Bits: OFF");
                        }
                        // Row 6: Save Slot Selector
                        else if (contentRelY >= 336.0f * uiScale && contentRelY < 396.0f * uiScale) {
                            float segW = contentW / 3.0f;
                            int idx = (int)((px - (drawerX + padX)) / segW);
                            mCurrentSlot = std::max(1, std::min(3, idx + 1));
                        }
                        // Row 7: Slot Save / Reload Buttons
                        else if (contentRelY >= 398.0f * uiScale && contentRelY < 458.0f * uiScale) {
                            float midX = drawerX + padX + contentW * 0.5f;
                            if (px < midX) {
                                triggerSaveSlot(mCurrentSlot);
                            } else {
                                triggerLoadSlot(mCurrentSlot);
                            }
                        }
                    }

                    if (!mSavePath.empty()) mSettings.save(mSavePath);
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
            float closeBtnX = drawerX + drawerW - 36.0f * uiScale;
            float closeBtnY = 44.0f * uiScale;
            float dx = px - closeBtnX;
            float dy = py - closeBtnY;
            if (dx * dx + dy * dy <= 26.0f * uiScale * 26.0f * uiScale) {
                mSettingsOpen = false;
                if (!mSavePath.empty()) mSettings.save(mSavePath);
                mDirty = true;
                return true;
            }

            // Tab Bar Tap (General vs Advanced) - only in Plus Edition
            if (mPlusEdition && py >= 58.0f * uiScale && py <= 104.0f * uiScale) {
                float midX = drawerX + padX + contentW * 0.5f;
                int newTab = (px < midX) ? 0 : 1;
                if (newTab != mSettingsTab) {
                    mSettingsTab = newTab;
                    mDrawerScrollY = 0.0f;
                    mDrawerScrollTarget = 0.0f;
                }
                mDirty = true;
                return true;
            }

            // Fixed [ Done ] button at bottom
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

    // Button layout positions - Ergonomic arc matching render exactly
    float btnBaseX = w - 150.0f * scale + mSettings.buttonOffsetX;
    float btnBaseY = h - 150.0f * scale + mSettings.buttonOffsetY;

    // A (Jump) - Primary action
    float aX = btnBaseX, aY = btnBaseY, aR = 54.0f * scale;
    // B (Shoot Star Bit) - Left of A, elevated comfortably
    float bX = btnBaseX - 110.0f * scale, bY = btnBaseY - 20.0f * scale, bR = 45.0f * scale;
    // SPIN - Above A, shifted slightly right for natural thumb roll
    float spinX = btnBaseX + 30.0f * scale, spinY = btnBaseY - 125.0f * scale, spinR = 48.0f * scale;
    // Z (Crouch) - Upper left of A
    float zX = btnBaseX - 85.0f * scale, zY = btnBaseY - 115.0f * scale, zR = 44.0f * scale;
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

            // Stick acquisition: bottom-left thumb zone
            bool stickEligible = false;
            if (mSettings.showControls && mStickPointerId < 0) {
                if (mSettings.dynamicStick) {
                    stickEligible = (px <= w * 0.38f && py >= h * 0.45f);
                } else {
                    float d2 = (px - defaultStickX) * (px - defaultStickX) + (py - defaultStickY) * (py - defaultStickY);
                    stickEligible = (d2 <= (stickBaseR * 1.5f) * (stickBaseR * 1.5f));
                }
            }

            if (stickEligible) {
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
            }
            // Camera D-Pad
            else if (mSettings.showControls && mSettings.showDpad &&
                     ((px - dpadX)*(px - dpadX) + (py - dpadY)*(py - dpadY) <= dpadTotalR * dpadTotalR)) {
                mTouches[slot].role = TouchRole::DPAD;
                mDirty = true;
            }
            // Action Buttons - NEAREST EXCLUSIVE MATCHING
            else if (mSettings.showControls) {
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

                int bestIdx = -1;
                float bestNormDist = 1.0f;
                for (int b = 0; b < 5; b++) {
                    float normDist = candidates[b].distSq / candidates[b].maxDistSq;
                    if (normDist <= 1.0f && normDist < bestNormDist) {
                        bestNormDist = normDist;
                        bestIdx = b;
                    }
                }

                if (bestIdx >= 0) {
                    mTouches[slot].role = candidates[bestIdx].role;
                    mDirty = true;
                } else {
                    mTouches[slot].role = TouchRole::STAR_POINTER;
                }
            } else {
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
                mTouchStickX = dx / dist;
                mTouchStickY = -dy / dist;
            } else if (dist > deadzone) {
                mStickKnobX = px;
                mStickKnobY = py;
                float norm = (dist - deadzone) / (stickMaxR - deadzone);
                mTouchStickX = (dx / dist) * norm;
                mTouchStickY = -(dy / dist) * norm;
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
            touchButtons |= 0x0400; // WPAD_BUTTON_B
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

    auto drawSegmentedGroup = [&](float x, float y, float grpW, float grpH, const char** labels, int count, int activeIdx) {
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
        // 2. Action Buttons - Frosted Glass with Signature Micro-Accents
        // ---------------------------------------------------------
        float btnBaseX = w - 150.0f * scale + mSettings.buttonOffsetX;
        float btnBaseY = h - 150.0f * scale + mSettings.buttonOffsetY;

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

            if (specialGlyph == 1) { // SPIN: Procedural Cyclone glyph + clean micro-label
                float iconR = pr * 0.44f;
                drawCyclone(cx, cy - 5.0f * scale, iconR, glyphCol, 0.0f);
                drawText("SPIN", cx, cy + pr * 0.58f, 0.26f * scale, glyphCol, 1);
            } else if (specialGlyph == 2) { // PLUS (+): Modern precision cross
                float armLen = pr * 0.38f;
                float thick = 2.4f * scale;
                drawLine(cx - armLen, cy, cx + armLen, cy, thick, glyphCol);
                drawLine(cx, cy - armLen, cx, cy + armLen, thick, glyphCol);
            } else { // Standard typography (A, B, Z)
                float textSize = (r > 48.0f * scale) ? (0.64f * scale) : (0.54f * scale);
                drawText(label, cx, cy + 11.0f * scale, textSize, glyphCol, 1);
            }
        };

        // Button A (Jump) - Luminous Electric Cyan (#38BDF8)
        drawModernButton(btnBaseX, btnBaseY, 54.0f * scale, "A", 0x38BDF8, mBtnAPressed);

        // Button B (Star Bit Shoot) - Luminous Star Amber (#FBBF24)
        drawModernButton(btnBaseX - 110.0f * scale, btnBaseY - 20.0f * scale, 45.0f * scale, "B", 0xFBBF24, mBtnBPressed);

        // Button SPIN - Luminous Emerald Mint (#34D399) with Cyclone Glyph
        drawModernButton(btnBaseX + 30.0f * scale, btnBaseY - 125.0f * scale, 48.0f * scale, "SPIN", 0x34D399, mBtnSpinPressed, 1);

        // Button Z (Crouch) - Luminous Lavender Violet (#A78BFA)
        drawModernButton(btnBaseX - 85.0f * scale, btnBaseY - 115.0f * scale, 44.0f * scale, "Z", 0xA78BFA, mBtnZPressed);

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
        drawText("DRAG TO REPOSITION CONTROLS", w * 0.5f, 55.0f, 0.46f, rgba(0.90f, 0.92f, 0.96f, 1.0f), 1);

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
        drawText("BUTTONS", btnBaseX - 60.0f * scale, btnBaseY - 230.0f * scale, 0.45f, rgba(0.40f, 0.65f, 0.98f, 0.95f), 1);

        // Camera D-Pad bounding outline
        if (mSettings.showDpad) {
            float baseDpadX = mSettings.dpadOnRight ? (w - 380.0f * scale) : (140.0f * scale);
            float baseDpadY = mSettings.dpadOnRight ? (h - 220.0f * scale) : (h - 390.0f * scale);
            float curDpadX = baseDpadX + mSettings.dpadOffsetX;
            float curDpadY = baseDpadY + mSettings.dpadOffsetY;
            drawRing(curDpadX, curDpadY, 78.0f * scale, 0.96f, rgba(0.40f, 0.65f, 0.98f, 0.85f));
            drawText("CAMERA D-PAD", curDpadX, curDpadY - 88.0f * scale, 0.42f, rgba(0.40f, 0.65f, 0.98f, 0.95f), 1);
        }
    }

    // -------------------------------------------------------------
    // Material 3 Side Sheet Navigation Drawer (Tabbed & Scrollable)
    // -------------------------------------------------------------
    if (mDrawerAnim > 0.001f) {
        // Scrim backdrop (smoothly fades with animation)
        drawRoundRect(0, 0, w, h, 0.0f, rgba(0.0f, 0.0f, 0.0f, mDrawerAnim * 0.52f));

        // Side Sheet container: Surface Container (#1B1D23) with 28dp rounded right corners
        drawRoundRect(drawerX - 60.0f, 0.0f, drawerX + drawerW, h, 28.0f * uiScale, rgba(0.106f, 0.114f, 0.137f, 0.98f));
        // Right edge outline border
        drawRoundRect(drawerX + drawerW - 2.0f, 0.0f, drawerX + drawerW, h, 0.0f, rgba(0.267f, 0.278f, 0.310f, 0.55f));

        float padX = 22.0f * uiScale;
        float contentW = drawerW - padX * 2.0f;
        float contentTop = mPlusEdition ? (112.0f * uiScale) : (68.0f * uiScale);
        float contentBottom = h - 56.0f * uiScale;
        float totalContentH = (mSettingsTab == 0 || !mPlusEdition) ? (1540.0f * uiScale) : (580.0f * uiScale);
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

        float swW = 50.0f * uiScale, swH = 28.0f * uiScale;

        // ---------------------------------------------------------
        // A. Scrollable Content Layer
        // ---------------------------------------------------------
        if (mSettingsTab == 0 || !mPlusEdition) {
            // General Tab Settings Items
            auto renderRow = [&](float relY, float rowH, auto func) {
                float drawY = contentTop + relY - mDrawerScrollY;
                if (drawY + rowH >= contentTop - 12.0f && drawY <= contentBottom + 12.0f) {
                    func(drawY);
                }
            };

            // Row 0: Quick Save Anywhere Action Button
            renderRow(8.0f * uiScale, 48.0f * uiScale, [&](float y) {
                float btnY = y + 4.0f * uiScale;
                float bX0 = drawerX + padX;
                float bX1 = drawerX + drawerW - padX;
                drawRoundRect(bX0, btnY, bX1, btnY + 40.0f * uiScale, 14.0f * uiScale, rgba(0.06f, 0.42f, 0.32f, 0.95f));
                drawRoundRect(bX0, btnY, bX1, btnY + 40.0f * uiScale, 14.0f * uiScale, rgba(0.25f, 0.85f, 0.65f, 0.60f));
                drawCircle(bX0 + 24.0f * uiScale, btnY + 20.0f * uiScale, 6.0f * uiScale, rgba(0.35f, 0.98f, 0.75f, 1.0f));
                drawText("QUICK SAVE ANYWHERE", (bX0 + bX1) * 0.5f + 8.0f * uiScale, btnY + 26.0f * uiScale, 0.38f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
            });

            // Row 1: Visibility Switch
            renderRow(62.0f * uiScale, 46.0f * uiScale, [&](float y) {
                drawText("On-Screen Controls", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Show HUD buttons", drawerX + padX, y + 36.0f * uiScale, 0.29f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.showControls);
            });

            // Row 2: Camera D-Pad Switch
            renderRow(112.0f * uiScale, 46.0f * uiScale, [&](float y) {
                drawText("Camera D-Pad", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Limited camera rotation & reset", drawerX + padX, y + 36.0f * uiScale, 0.29f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.showDpad);
            });

            // Row 3: D-Pad Position Segmented Button
            renderRow(162.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("D-Pad Position", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* dpadSideOpts[2] = {"Left Side", "Right Side"};
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, dpadSideOpts, 2, mSettings.dpadOnRight ? 1 : 0);
            });

            // Row 4: Fullscreen Switch
            renderRow(220.0f * uiScale, 46.0f * uiScale, [&](float y) {
                drawText("Fullscreen", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Native ultrawide (expand Hor+ FOV)", drawerX + padX, y + 36.0f * uiScale, 0.29f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.fullscreen);
            });

            // Row 5: Full Touch Mode Switch
            renderRow(270.0f * uiScale, 46.0f * uiScale, [&](float y) {
                drawText("Full Touch Mode", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Tap to jump/advance, flick to spin", drawerX + padX, y + 36.0f * uiScale, 0.29f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.fullTouchMode);
            });

            // Row 6: Joystick Mode Segmented Button
            renderRow(320.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Joystick Mode", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* joyOpts[2] = {"Dynamic", "Fixed"};
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, joyOpts, 2, mSettings.dynamicStick ? 0 : 1);
            });

            // Row 7: Controls Opacity Segmented Button
            renderRow(378.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Controls Opacity", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* opOpts[4] = {"25%", "50%", "75%", "100%"};
                int opIdx = (fabsf(mSettings.opacity - 0.25f) < 0.12f) ? 0 : (fabsf(mSettings.opacity - 0.50f) < 0.12f) ? 1 : (fabsf(mSettings.opacity - 0.75f) < 0.12f) ? 2 : 3;
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, opOpts, 4, opIdx);
            });

            // Row 8: Button Size Segmented Button
            renderRow(436.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Button Size", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* scOpts[3] = {"Small", "Medium", "Large"};
                int scIdx = (fabsf(mSettings.scale - 0.85f) < 0.08f) ? 0 : (fabsf(mSettings.scale - 1.25f) < 0.08f) ? 2 : 1;
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, scOpts, 3, scIdx);
            });

            // Row 9: Layout Reposition & Reset Buttons
            renderRow(494.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Controls Layout", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                float btnY = y + 22.0f * uiScale;
                float b1X0 = drawerX + padX;
                float b1X1 = drawerX + padX + contentW * 0.56f - 4.0f * uiScale;
                float b2X0 = drawerX + padX + contentW * 0.56f + 4.0f * uiScale;
                float b2X1 = drawerX + drawerW - padX;
                drawRoundRect(b1X0, btnY, b1X1, btnY + 34.0f * uiScale, 14.0f * uiScale, rgba(0.18f, 0.23f, 0.31f, 0.95f));
                drawText("Reposition", (b1X0 + b1X1) * 0.5f, btnY + 22.0f * uiScale, 0.36f * uiScale, rgba(0.659f, 0.780f, 0.980f, 1.0f), 1);
                drawRoundRect(b2X0, btnY, b2X1, btnY + 34.0f * uiScale, 14.0f * uiScale, rgba(0.35f, 0.15f, 0.17f, 0.95f));
                drawText("Reset", (b2X0 + b2X1) * 0.5f, btnY + 22.0f * uiScale, 0.36f * uiScale, rgba(1.0f, 0.72f, 0.72f, 1.0f), 1);
            });

            // Row 10: View System Logs Button
            renderRow(552.0f * uiScale, 48.0f * uiScale, [&](float y) {
                float btnY = y + 8.0f * uiScale;
                float bX0 = drawerX + padX;
                float bX1 = drawerX + drawerW - padX;
                drawRoundRect(bX0, btnY, bX1, btnY + 36.0f * uiScale, 14.0f * uiScale, rgba(0.12f, 0.28f, 0.44f, 0.95f));
                drawRoundRect(bX0, btnY, bX1, btnY + 36.0f * uiScale, 14.0f * uiScale, rgba(0.38f, 0.74f, 0.98f, 0.50f));
                drawText("View System Logs", (bX0 + bX1) * 0.5f, btnY + 23.0f * uiScale, 0.38f * uiScale, rgba(0.85f, 0.94f, 1.0f, 1.0f), 1);
            });

            // Row 11: Hardware Monitor HUD Segmented Button
            renderRow(606.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Hardware Monitor HUD", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* hudOpts[3] = {"Off", "FPS", "Full Stats"};
                int hudIdx = std::max(0, std::min(2, mSettings.perfHudMode));
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, hudOpts, 3, hudIdx);
            });

            // Row 12: Target Frame Rate Segmented Button
            renderRow(664.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Target Frame Rate", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* fpsOpts[5] = {"30", "60", "90", "120", "144"};
                int fpsIdx = (mSettings.targetFps >= 144) ? 4 : (mSettings.targetFps >= 120) ? 3 : (mSettings.targetFps >= 90) ? 2 : (mSettings.targetFps >= 60) ? 1 : 0;
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, fpsOpts, 5, fpsIdx);
            });

            // Row 13: Graphics Backend Segmented Button
            renderRow(722.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Graphics Backend", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* backendOpts[2] = {"OpenGL ES 3.2", "Vulkan 1.0"};
                int bkIdx = (mSettings.graphicsBackend == 1) ? 1 : 0;
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, backendOpts, 2, bkIdx);
            });

            // Row 14: Game Speed Multiplier (Fast-Forward / Turbo)
            renderRow(776.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Game / Dialogue Speed", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* spdOpts[5] = {"1.0x", "1.5x", "2.0x", "3.0x", "4.0x"};
                int spdIdx = std::max(0, std::min(4, mSettings.gameSpeed - 1));
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, spdOpts, 5, spdIdx);
            });

            // Row 15: Game Language Selector
            renderRow(834.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Game Language", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* langOpts[6] = {"Auto", "EN", "ES", "FR", "DE", "IT"};
                int langIdx = std::max(0, std::min(5, mSettings.languageChoice));
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, langOpts, 6, langIdx);
            });

            // Row 16: Render Resolution Segmented Button
            renderRow(892.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Render Resolution", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* resOpts[4] = {"480p", "720p", "1080p", "1440p"};
                int resIdx = std::max(0, std::min(3, mSettings.renderScale));
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, resOpts, 4, resIdx);
            });

            // Row 17: Vibration Haptics Switch
            renderRow(950.0f * uiScale, 46.0f * uiScale, [&](float y) {
                drawText("Vibration Haptics", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Tactile feedback on button presses", drawerX + padX, y + 36.0f * uiScale, 0.29f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.haptics);
            });

            // Row 18: Battery Saver Switch
            renderRow(1004.0f * uiScale, 48.0f * uiScale, [&](float y) {
                drawText("Battery Saver / Cool Mode", drawerX + padX, y + 18.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("30 FPS lock, minimizes thermal load", drawerX + padX, y + 34.0f * uiScale, 0.28f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.batterySaver);
            });

            // Row 19: Gyroscope Star Pointer Switch
            renderRow(1058.0f * uiScale, 48.0f * uiScale, [&](float y) {
                drawText("Gyro Star Pointer Aiming", drawerX + padX, y + 18.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Tilt phone to steer Star Pointer", drawerX + padX, y + 34.0f * uiScale, 0.28f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.gyroAiming);
            });

            // Row 19b: Gyro Invert Axes
            renderRow(1112.0f * uiScale, 46.0f * uiScale, [&](float y) {
                float b1X0 = drawerX + padX;
                float b1X1 = drawerX + padX + contentW * 0.5f - 4.0f * uiScale;
                float b2X0 = drawerX + padX + contentW * 0.5f + 4.0f * uiScale;
                float b2X1 = drawerX + drawerW - padX;

                Color c1 = mSettings.gyroInvertX ? rgba(0.043f, 0.341f, 0.816f, 0.95f) : rgba(0.18f, 0.20f, 0.25f, 0.85f);
                Color c2 = mSettings.gyroInvertY ? rgba(0.043f, 0.341f, 0.816f, 0.95f) : rgba(0.18f, 0.20f, 0.25f, 0.85f);
                drawRoundRect(b1X0, y + 8.0f * uiScale, b1X1, y + 38.0f * uiScale, 10.0f * uiScale, c1);
                drawText(mSettings.gyroInvertX ? "Invert X: ON" : "Invert X: OFF", (b1X0 + b1X1) * 0.5f, y + 25.0f * uiScale, 0.32f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

                drawRoundRect(b2X0, y + 8.0f * uiScale, b2X1, y + 38.0f * uiScale, 10.0f * uiScale, c2);
                drawText(mSettings.gyroInvertY ? "Invert Y: ON" : "Invert Y: OFF", (b2X0 + b2X1) * 0.5f, y + 25.0f * uiScale, 0.32f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
            });

            // Row 19c: Relative Pointer Switch (Issue #11)
            renderRow(1164.0f * uiScale, 48.0f * uiScale, [&](float y) {
                drawText("Relative Star Pointer (Trackpad)", drawerX + padX, y + 18.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Drag anywhere to steer pointer without blocking view", drawerX + padX, y + 34.0f * uiScale, 0.28f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.relativePointer);
            });

            // Row 19d: Screen Orientation Segmented Button (Issues #18 & #19)
            renderRow(1218.0f * uiScale, 56.0f * uiScale, [&](float y) {
                drawText("Screen Orientation", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* orientOpts[3] = {"Auto Sensor", "Landscape", "Reverse"};
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, orientOpts, 3, mSettings.screenOrientationChoice);
            });

            // Row 20: Save Data Backup (Downloads)
            renderRow(1280.0f * uiScale, 56.0f * uiScale, [&](float y) {
                drawText("Save Data Backup (Downloads)", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                float btnY = y + 22.0f * uiScale;
                float b1X0 = drawerX + padX;
                float b1X1 = drawerX + padX + contentW * 0.5f - 4.0f * uiScale;
                float b2X0 = drawerX + padX + contentW * 0.5f + 4.0f * uiScale;
                float b2X1 = drawerX + drawerW - padX;
                drawRoundRect(b1X0, btnY, b1X1, btnY + 32.0f * uiScale, 12.0f * uiScale, rgba(0.12f, 0.28f, 0.44f, 0.95f));
                drawText("Export Save", (b1X0 + b1X1) * 0.5f, btnY + 21.0f * uiScale, 0.34f * uiScale, rgba(0.85f, 0.94f, 1.0f, 1.0f), 1);
                drawRoundRect(b2X0, btnY, b2X1, btnY + 32.0f * uiScale, 12.0f * uiScale, rgba(0.18f, 0.34f, 0.24f, 0.95f));
                drawText("Import Save", (b2X0 + b2X1) * 0.5f, btnY + 21.0f * uiScale, 0.34f * uiScale, rgba(0.85f, 1.0f, 0.90f, 1.0f), 1);
            });

            // Row 21: Realtime Hardware & Device Info Card
            renderRow(1342.0f * uiScale, 64.0f * uiScale, [&](float y) {
                float cardX0 = drawerX + padX;
                float cardX1 = drawerX + drawerW - padX;
                float cardH = 60.0f * uiScale;
                drawRoundRect(cardX0, y, cardX1, y + cardH, 12.0f * uiScale, rgba(0.10f, 0.12f, 0.16f, 0.90f));
                drawRoundRect(cardX0 + 1.0f, y + 1.0f, cardX1 - 1.0f, y + cardH - 1.0f, 11.0f * uiScale, rgba(0.20f, 0.25f, 0.34f, 0.50f));

                char gpuLine[128], resLine[128], apiLine[128];
                snprintf(gpuLine, sizeof(gpuLine), "GPU: %s", mGpuRendererName.empty() ? "OpenGL ES 3.2" : mGpuRendererName.c_str());
                const char* resStr = (mSettings.renderScale == 0) ? "480p" : (mSettings.renderScale == 1) ? "720p" : (mSettings.renderScale == 2) ? "1080p" : "1440p";
                snprintf(resLine, sizeof(resLine), "Display: %dx%d (%s Target • Hor+)", (int)w, (int)h, resStr);
                snprintf(apiLine, sizeof(apiLine), "Active Engine: %s • Native ARM64", (mActiveBackend == 1) ? "Vulkan 1.0" : "OpenGL ES 3.2 (fallback)");

                drawText(gpuLine, cardX0 + 12.0f * uiScale, y + 18.0f * uiScale, 0.28f * uiScale, rgba(0.40f, 0.75f, 1.0f, 0.95f), 0);
                drawText(resLine, cardX0 + 12.0f * uiScale, y + 34.0f * uiScale, 0.26f * uiScale, rgba(0.80f, 0.84f, 0.90f, 0.85f), 0);
                drawText(apiLine, cardX0 + 12.0f * uiScale, y + 49.0f * uiScale, 0.24f * uiScale, rgba(0.55f, 0.60f, 0.68f, 0.80f), 0);
            });

            // Row 22: Return to Setup / Launcher Button
            renderRow(1414.0f * uiScale, 52.0f * uiScale, [&](float y) {
                float bX0 = drawerX + padX;
                float bX1 = drawerX + drawerW - padX;
                float btnY = y + 4.0f * uiScale;
                drawRoundRect(bX0, btnY, bX1, btnY + 40.0f * uiScale, 14.0f * uiScale, rgba(0.40f, 0.12f, 0.15f, 0.95f));
                drawRoundRect(bX0, btnY, bX1, btnY + 40.0f * uiScale, 14.0f * uiScale, rgba(0.90f, 0.25f, 0.30f, 0.60f));
                drawText("Exit to Setup Launcher", (bX0 + bX1) * 0.5f, btnY + 25.0f * uiScale, 0.38f * uiScale, rgba(1.0f, 0.88f, 0.88f, 1.0f), 1);
            });
        } else {
            // Advanced / Cheats Tab Settings Items (Gameplay Mods)
            auto renderRow = [&](float relY, float rowH, auto func) {
                float drawY = contentTop + relY - mDrawerScrollY;
                if (drawY + rowH >= contentTop - 12.0f && drawY <= contentBottom + 12.0f) {
                    func(drawY);
                }
            };

            // Section Header
            renderRow(6.0f * uiScale, 36.0f * uiScale, [&](float y) {
                drawText("Special Cheats & Mods", drawerX + padX, y + 16.0f * uiScale, 0.40f * uiScale, rgba(0.92f, 0.94f, 0.98f, 1.0f), 0);
                drawText("Instant boosts & speedrun tools", drawerX + padX, y + 32.0f * uiScale, 0.28f * uiScale, rgba(0.65f, 0.75f, 0.90f, 0.85f), 0);
            });

            // Mod 1: Fly Mode Switch
            renderRow(48.0f * uiScale, 52.0f * uiScale, [&](float y) {
                drawText("Fly Mode", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Hold A to fly up, hold Z to fly down", drawerX + padX, y + 36.0f * uiScale, 0.28f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.modFly);
            });

            // Mod 2: Cutscene Prevention Switch
            renderRow(106.0f * uiScale, 52.0f * uiScale, [&](float y) {
                drawText("Cutscene Prevention", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Instantly skip video cutscenes", drawerX + padX, y + 36.0f * uiScale, 0.28f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.modSkipCutscenes);
            });

            // Mod 3: Infinite Health Switch
            renderRow(164.0f * uiScale, 52.0f * uiScale, [&](float y) {
                drawText("Infinite Health", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Invincible; teleports out of black holes", drawerX + padX, y + 36.0f * uiScale, 0.28f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.modInfiniteHealth);
            });

            // Mod 4: Infinite Lives Switch
            renderRow(222.0f * uiScale, 52.0f * uiScale, [&](float y) {
                drawText("Infinite Lives (99)", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Lock Mario's life counter to 99 lives", drawerX + padX, y + 36.0f * uiScale, 0.28f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.modInfiniteLives);
            });

            // Mod 5: Infinite Star Bits Switch
            renderRow(280.0f * uiScale, 52.0f * uiScale, [&](float y) {
                drawText("Infinite Star Bits (999)", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Max out Star Bits pouch to 999", drawerX + padX, y + 36.0f * uiScale, 0.28f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.modInfiniteStarBits);
            });

            // Row 6: Save Slot Selector
            renderRow(340.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Multi-Save Slot", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* slotOpts[3] = {"Slot 1", "Slot 2", "Slot 3"};
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, slotOpts, 3, mCurrentSlot - 1);
            });

            // Row 7: Slot Save / Reload Action Buttons
            renderRow(400.0f * uiScale, 52.0f * uiScale, [&](float y) {
                float bW = (contentW - 12.0f * uiScale) * 0.5f;
                float btnY = y + 4.0f * uiScale;
                // Save Button (Green)
                float sX0 = drawerX + padX;
                float sX1 = sX0 + bW;
                drawRoundRect(sX0, btnY, sX1, btnY + 40.0f * uiScale, 12.0f * uiScale, rgba(0.06f, 0.42f, 0.32f, 0.95f));
                drawRoundRect(sX0, btnY, sX1, btnY + 40.0f * uiScale, 12.0f * uiScale, rgba(0.25f, 0.85f, 0.65f, 0.60f));
                char sTxt[32]; snprintf(sTxt, sizeof(sTxt), "Save Slot %d", mCurrentSlot);
                drawText(sTxt, (sX0 + sX1) * 0.5f, btnY + 25.0f * uiScale, 0.34f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

                // Reload Button (Blue)
                float rX0 = sX1 + 12.0f * uiScale;
                float rX1 = rX0 + bW;
                drawRoundRect(rX0, btnY, rX1, btnY + 40.0f * uiScale, 12.0f * uiScale, rgba(0.12f, 0.28f, 0.55f, 0.95f));
                drawRoundRect(rX0, btnY, rX1, btnY + 40.0f * uiScale, 12.0f * uiScale, rgba(0.35f, 0.65f, 0.98f, 0.60f));
                char rTxt[32]; snprintf(rTxt, sizeof(rTxt), "Reload Slot %d", mCurrentSlot);
                drawText(rTxt, (rX0 + rX1) * 0.5f, btnY + 25.0f * uiScale, 0.34f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
            });

            // Info Card
            renderRow(464.0f * uiScale, 84.0f * uiScale, [&](float y) {
                drawRoundRect(drawerX + padX, y + 6.0f * uiScale, drawerX + drawerW - padX, y + 80.0f * uiScale, 14.0f * uiScale, rgba(0.14f, 0.17f, 0.22f, 0.95f));
                drawRoundRect(drawerX + padX, y + 6.0f * uiScale, drawerX + drawerW - padX, y + 80.0f * uiScale, 14.0f * uiScale, rgba(0.24f, 0.32f, 0.45f, 0.60f));
                drawText("CHEATS & SLOTS STATUS", drawerX + padX + 16.0f * uiScale, y + 26.0f * uiScale, 0.32f * uiScale, rgba(0.38f, 0.74f, 0.98f, 0.95f), 0);
                drawText("Save slots let you branch your game at any point.", drawerX + padX + 16.0f * uiScale, y + 44.0f * uiScale, 0.27f * uiScale, rgba(0.80f, 0.84f, 0.90f, 0.85f), 0);
                drawText("Reload instantly restores the selected slot's NAND save.", drawerX + padX + 16.0f * uiScale, y + 62.0f * uiScale, 0.27f * uiScale, rgba(0.80f, 0.84f, 0.90f, 0.85f), 0);
            });
        }

        // Scrollbar Thumb (rendered inside content area)
        if (maxScroll > 1.0f) {
            float trackH = contentBottom - contentTop;
            float thumbH = std::max(28.0f * uiScale, trackH * (trackH / totalContentH));
            float thumbY = contentTop + (mDrawerScrollY / maxScroll) * (trackH - thumbH);
            drawRoundRect(drawerX + drawerW - 5.0f * uiScale, thumbY, drawerX + drawerW - 2.0f * uiScale, thumbY + thumbH, 1.5f * uiScale, rgba(0.85f, 0.92f, 1.0f, 0.40f));
        }

        // ---------------------------------------------------------
        // B. Fixed Top Header & Tab Bar Mask Overlay
        // ---------------------------------------------------------
        drawRoundRect(drawerX - 60.0f, 0.0f, drawerX + drawerW, contentTop, 0.0f, rgba(0.106f, 0.114f, 0.137f, 1.0f));

        // Header Title & Cogwheel
        drawCogwheel(drawerX + padX + 16.0f * uiScale, 36.0f * uiScale, 15.0f * uiScale, rgba(0.659f, 0.780f, 0.980f, 1.0f), cogRot);
        drawText(mPlusEdition ? "GalaxyMobile+" : "Settings", drawerX + padX + 40.0f * uiScale, 44.0f * uiScale, 0.54f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);

        // Close [X] circular tonal button
        float closeBtnX = drawerX + drawerW - 36.0f * uiScale;
        float closeBtnY = 36.0f * uiScale;
        drawCircle(closeBtnX, closeBtnY, 17.0f * uiScale, rgba(0.188f, 0.196f, 0.224f, 0.95f));
        drawCloseIcon(closeBtnX, closeBtnY, 9.0f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.95f));

        if (mPlusEdition) {
            // Material 3 Tab Dock ([ General ]  [ Cheats & Mods ])
            float tabBoxY0 = 62.0f * uiScale;
            float tabBoxY1 = 98.0f * uiScale;
            drawRoundRect(drawerX + padX, tabBoxY0, drawerX + drawerW - padX, tabBoxY1, 18.0f * uiScale, rgba(0.149f, 0.160f, 0.190f, 0.95f));
            drawRoundRect(drawerX + padX, tabBoxY0, drawerX + drawerW - padX, tabBoxY1, 18.0f * uiScale, rgba(0.24f, 0.26f, 0.30f, 0.50f));

            float tabMidX = drawerX + padX + contentW * 0.5f;
            if (mSettingsTab == 0) {
                // General Tab Active
                drawRoundRect(drawerX + padX + 2.0f * uiScale, tabBoxY0 + 2.0f * uiScale, tabMidX - 2.0f * uiScale, tabBoxY1 - 2.0f * uiScale, 16.0f * uiScale, rgba(0.14f, 0.28f, 0.48f, 0.95f));
                drawText("General", (drawerX + padX + tabMidX) * 0.5f, tabBoxY0 + 23.0f * uiScale, 0.36f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
                drawText("Cheats & Mods", (tabMidX + drawerX + drawerW - padX) * 0.5f, tabBoxY0 + 23.0f * uiScale, 0.34f * uiScale, rgba(0.68f, 0.71f, 0.77f, 0.75f), 1);
            } else {
                // Cheats Tab Active
                drawRoundRect(tabMidX + 2.0f * uiScale, tabBoxY0 + 2.0f * uiScale, drawerX + drawerW - padX - 2.0f * uiScale, tabBoxY1 - 2.0f * uiScale, 16.0f * uiScale, rgba(0.14f, 0.28f, 0.48f, 0.95f));
                drawText("General", (drawerX + padX + tabMidX) * 0.5f, tabBoxY0 + 23.0f * uiScale, 0.36f * uiScale, rgba(0.68f, 0.71f, 0.77f, 0.75f), 1);
                drawText("Cheats & Mods", (tabMidX + drawerX + drawerW - padX) * 0.5f, tabBoxY0 + 23.0f * uiScale, 0.34f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
            }

            // Divider below tab bar
            drawRoundRect(drawerX + padX, contentTop - 1.0f, drawerX + drawerW - padX, contentTop, 0.5f, rgba(0.267f, 0.278f, 0.310f, 0.45f));
        } else {
            // Divider below header in standard edition
            drawRoundRect(drawerX + padX, contentTop - 1.0f, drawerX + drawerW - padX, contentTop, 0.5f, rgba(0.267f, 0.278f, 0.310f, 0.45f));
        }

        // ---------------------------------------------------------
        // C. Fixed Bottom Bar Mask Overlay & [ Done ] Button
        // ---------------------------------------------------------
        drawRoundRect(drawerX - 60.0f, contentBottom, drawerX + drawerW, h, 0.0f, rgba(0.106f, 0.114f, 0.137f, 1.0f));
        drawRoundRect(drawerX + padX, contentBottom, drawerX + drawerW - padX, contentBottom + 1.0f, 0.5f, rgba(0.267f, 0.278f, 0.310f, 0.45f));

        float doneY = h - 48.0f * uiScale;
        float doneX0 = drawerX + padX;
        float doneX1 = drawerX + drawerW - padX;
        drawRoundRect(doneX0, doneY, doneX1, doneY + 38.0f * uiScale, 19.0f * uiScale, rgba(0.043f, 0.341f, 0.816f, 1.0f));
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

void UIOverlay::resetLayoutOffsets() {
    mSettings.stickOffsetX = 0.0f;
    mSettings.stickOffsetY = 0.0f;
    mSettings.buttonOffsetX = 0.0f;
    mSettings.buttonOffsetY = 0.0f;
    mSettings.dpadOffsetX = 0.0f;
    mSettings.dpadOffsetY = 0.0f;
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
