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

#include <algorithm>
#include <string>
#include <vector>

#include "xr/ui_font.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MobileUI", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MobileUI", __VA_ARGS__)

namespace mobile_ui {

namespace {

// In-shader vertex for 2D batching
struct UIVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
    float mode;   // 0 = rect, 1 = circle/ring, 2 = font glyph, 3 = cogwheel, 4 = close 'X'
    float param;  // ring: inner ratio; rect: corner radius ratio; cogwheel: rot angle
    float aspect; // rect aspect ratio (w/h or h/w)
    float extra;  // rect: 0 = horiz/square, 1 = vert
};

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
        // Mode 3: Iconic Material 3 Settings Cogwheel (⚙)
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
        // Mode 5: Spin Cyclone / Galaxy Swirl Glyph (🌀)
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
        // Mode 7: Modern Minimalist Camera Glyph (📷)
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
        }
    }
    fclose(f);
    LOGI("Loaded settings: show=%d, full=%d, op=%.2f, scale=%.2f, dpad=%d, fly=%d, skip=%d, infHp=%d",
         showControls, fullscreen, opacity, scale, showDpad, modFly, modSkipCutscenes, modInfiniteHealth);
}

void Settings::save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    fprintf(f, "show_controls=%d\n", showControls ? 1 : 0);
    fprintf(f, "fullscreen=%d\n", fullscreen ? 1 : 0);
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
    if (!mPlusEdition) {
        mSettings.modFly = false;
        mSettings.modSkipCutscenes = false;
        mSettings.modInfiniteHealth = false;
        mSettingsTab = 0;
    }
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
    const ui_font::Face& face = ui_font::kFaces[0]; // size 40 Inter Bold
    mAtlasW = face.width;
    mAtlasH = face.height;

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

    std::vector<uint8_t> coverage((size_t)face.width * face.height, 0);
    for (size_t i = 0; i < coverage.size(); i++) {
        uint8_t b = (i / 2 < packed.size()) ? packed[i / 2] : 0;
        coverage[i] = (uint8_t)(((i & 1) ? (b & 15) : (b >> 4)) * 17);
    }

    glGenTextures(1, &mFontTex);
    glBindTexture(GL_TEXTURE_2D, mFontTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, face.width, face.height, 0, GL_RED, GL_UNSIGNED_BYTE, coverage.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    mInitialized = true;
    LOGI("Mobile UI initialized successfully (Atlas %dx%d)", mAtlasW, mAtlasH);
    return true;
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
        float totalContentH = (mSettingsTab == 0 || !mPlusEdition) ? (455.0f * uiScale) : (340.0f * uiScale);
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
                        // Row 0: Visibility Switch
                        if (contentRelY >= 0.0f && contentRelY <= 48.0f * uiScale) {
                            mSettings.showControls = !mSettings.showControls;
                        }
                        // Row 1: Camera D-Pad Switch
                        else if (contentRelY >= 50.0f * uiScale && contentRelY <= 98.0f * uiScale) {
                            mSettings.showDpad = !mSettings.showDpad;
                        }
                        // Row 2: D-Pad Position Segmented
                        else if (contentRelY >= 104.0f * uiScale && contentRelY <= 156.0f * uiScale) {
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
                        // Row 3: Fullscreen Switch
                        else if (contentRelY >= 160.0f * uiScale && contentRelY <= 208.0f * uiScale) {
                            mSettings.fullscreen = !mSettings.fullscreen;
                        }
                        // Row 4: Joystick Mode Segmented
                        else if (contentRelY >= 212.0f * uiScale && contentRelY <= 266.0f * uiScale) {
                            float midX = drawerX + padX + contentW * 0.5f;
                            if (px < midX) mSettings.dynamicStick = true;
                            else mSettings.dynamicStick = false;
                        }
                        // Row 5: Controls Opacity Segmented
                        else if (contentRelY >= 270.0f * uiScale && contentRelY <= 324.0f * uiScale) {
                            float segW = contentW / 4.0f;
                            int idx = (int)((px - (drawerX + padX)) / segW);
                            if (idx <= 0) mSettings.opacity = 0.25f;
                            else if (idx == 1) mSettings.opacity = 0.50f;
                            else if (idx == 2) mSettings.opacity = 0.75f;
                            else mSettings.opacity = 1.00f;
                        }
                        // Row 6: Button Size Segmented
                        else if (contentRelY >= 328.0f * uiScale && contentRelY <= 382.0f * uiScale) {
                            float segW = contentW / 3.0f;
                            int idx = (int)((px - (drawerX + padX)) / segW);
                            if (idx <= 0) mSettings.scale = 0.85f;
                            else if (idx == 1) mSettings.scale = 1.00f;
                            else mSettings.scale = 1.25f;
                        }
                        // Row 7: Layout Reposition / Reset
                        else if (contentRelY >= 386.0f * uiScale && contentRelY <= 442.0f * uiScale) {
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
                    } else {
                        // Advanced Tab items
                        // Mod 1: Fly Mode Switch
                        if (contentRelY >= 48.0f * uiScale && contentRelY <= 106.0f * uiScale) {
                            mSettings.modFly = !mSettings.modFly;
                        }
                        // Mod 2: Cutscene Prevention Switch
                        else if (contentRelY >= 110.0f * uiScale && contentRelY <= 168.0f * uiScale) {
                            mSettings.modSkipCutscenes = !mSettings.modSkipCutscenes;
                        }
                        // Mod 3: Infinite Health Switch
                        else if (contentRelY >= 172.0f * uiScale && contentRelY <= 230.0f * uiScale) {
                            mSettings.modInfiniteHealth = !mSettings.modInfiniteHealth;
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

    // Button layout positions
    float btnBaseX = w - 160.0f * scale + mSettings.buttonOffsetX;
    float btnBaseY = h - 160.0f * scale + mSettings.buttonOffsetY;

    // A (Jump) - Primary action
    float aX = btnBaseX, aY = btnBaseY, aR = 54.0f * scale;
    // B (Shoot Star Bit) - Lower left of A
    float bX = btnBaseX - 120.0f * scale, bY = btnBaseY + 25.0f * scale, bR = 45.0f * scale;
    // SPIN - Top right of A
    float spinX = btnBaseX + 35.0f * scale, spinY = btnBaseY - 130.0f * scale, spinR = 48.0f * scale;
    // Z (Crouch) - Top left of A
    float zX = btnBaseX - 90.0f * scale, zY = btnBaseY - 105.0f * scale, zR = 44.0f * scale;
    // Plus (+) - Top right
    float plusX = w - 70.0f * scale, plusY = 70.0f * scale, plusR = 34.0f * scale;

    // Camera D-Pad position
    float baseDpadX = mSettings.dpadOnRight ? (w - 380.0f * scale) : (140.0f * scale);
    float baseDpadY = mSettings.dpadOnRight ? (h - 220.0f * scale) : (h - 390.0f * scale);
    float dpadX = baseDpadX + mSettings.dpadOffsetX;
    float dpadY = baseDpadY + mSettings.dpadOffsetY;
    float dpadArmDist = 38.0f * scale;
    float dpadArmR = 24.0f * scale;
    float dpadTotalR = (dpadArmDist + dpadArmR) * 1.15f;

    // Stick default position - Bottom left thumb area
    float defaultStickX = 175.0f * scale + mSettings.stickOffsetX;
    float defaultStickY = h - 175.0f * scale + mSettings.stickOffsetY;
    float stickBaseR = 95.0f * scale;
    float stickMaxR = 100.0f * scale;

    // Complete release: when all fingers are lifted or gesture canceled
    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        mStickPointerId = -1;
        mStickKnobX = mStickBaseX;
        mStickKnobY = mStickBaseY;
        pad.stickX = 0.0f;
        pad.stickY = 0.0f;
        touchButtons = 0;
        pad.buttons = 0;
        mBtnAPressed = mBtnBPressed = mBtnZPressed = mBtnSpinPressed = mBtnPlusPressed = false;
        mBtnDpadLeft = mBtnDpadRight = mBtnDpadUp = mBtnDpadDown = false;
        mDirty = true;
        pad.pointerValid = 1;
        pad.pointerX = mPointerX;
        pad.pointerY = mPointerY;
        pad.pointerDist = 2.0f;
        return true;
    }

    // Single finger release during multi-touch
    if (action == AMOTION_EVENT_ACTION_POINTER_UP) {
        int32_t liftedPid = AMotionEvent_getPointerId(event, pointerIndex);
        if (liftedPid == mStickPointerId) {
            mStickPointerId = -1;
            mStickKnobX = mStickBaseX;
            mStickKnobY = mStickBaseY;
            pad.stickX = 0.0f;
            pad.stickY = 0.0f;
            mDirty = true;
        }
    }

    // 1. Virtual Stick Acquisition:
    // Only captures fresh touches in the designated bottom-left thumb zone!
    if (mSettings.showControls && (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN)) {
        float px = AMotionEvent_getX(event, pointerIndex);
        float py = AMotionEvent_getY(event, pointerIndex);
        int32_t pid = AMotionEvent_getPointerId(event, pointerIndex);

        bool stickEligible = false;
        if (mSettings.dynamicStick) {
            // Strictly bottom-left quadrant: left 36% of screen AND bottom 50% of screen
            stickEligible = (px <= w * 0.36f && py >= h * 0.50f);
        } else {
            // Fixed mode: within 1.6x of stick center
            float d2 = (px - defaultStickX) * (px - defaultStickX) + (py - defaultStickY) * (py - defaultStickY);
            stickEligible = (d2 <= (stickBaseR * 1.6f) * (stickBaseR * 1.6f));
        }

        if (stickEligible && mStickPointerId < 0) {
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
    }

    // 2. Iterate Active Pointers
    bool stickFound = false;
    for (size_t i = 0; i < pointerCount; i++) {
        // Skip pointer that is releasing right now
        if (action == AMOTION_EVENT_ACTION_POINTER_UP && (int32_t)i == pointerIndex) {
            continue;
        }

        int32_t pid = AMotionEvent_getPointerId(event, i);
        float px = AMotionEvent_getX(event, i);
        float py = AMotionEvent_getY(event, i);

        if (pid == mStickPointerId) {
            stickFound = true;
            float dx = px - mStickBaseX;
            float dy = py - mStickBaseY;
            float dist = sqrtf(dx * dx + dy * dy);
            float deadzone = 8.0f * scale;

            if (dist > stickMaxR && dist > 0.001f) {
                mStickKnobX = mStickBaseX + (dx / dist) * stickMaxR;
                mStickKnobY = mStickBaseY + (dy / dist) * stickMaxR;
                pad.stickX = dx / dist;
                pad.stickY = -dy / dist;
            } else if (dist > deadzone) {
                mStickKnobX = px;
                mStickKnobY = py;
                float norm = (dist - deadzone) / (stickMaxR - deadzone);
                pad.stickX = (dx / dist) * norm;
                pad.stickY = -(dy / dist) * norm;
            } else {
                mStickKnobX = px;
                mStickKnobY = py;
                pad.stickX = 0.0f;
                pad.stickY = 0.0f;
            }
            pad.stickX = std::max(-1.0f, std::min(1.0f, pad.stickX));
            pad.stickY = std::max(-1.0f, std::min(1.0f, pad.stickY));
        } else {
            // Button hit testing
            auto inRadius = [](float tx, float ty, float cx, float cy, float r) {
                float d2 = (tx - cx) * (tx - cx) + (ty - cy) * (ty - cy);
                return d2 <= (r * 1.30f) * (r * 1.30f);
            };

            bool hitButton = false;
            if (mSettings.showControls) {
                if (inRadius(px, py, aX, aY, aR)) {
                    touchButtons |= 0x0800; // WPAD_BUTTON_A
                    mBtnAPressed = true;
                    hitButton = true;
                }
                if (inRadius(px, py, bX, bY, bR)) {
                    touchButtons |= 0x0400; // WPAD_BUTTON_B
                    mBtnBPressed = true;
                    hitButton = true;
                }
                if (inRadius(px, py, spinX, spinY, spinR)) {
                    spinFrames = 6;
                    mBtnSpinPressed = true;
                    hitButton = true;
                }
                if (inRadius(px, py, zX, zY, zR)) {
                    touchButtons |= 0x2000; // WPAD_BUTTON_Z
                    mBtnZPressed = true;
                    hitButton = true;
                }
                if (inRadius(px, py, plusX, plusY, plusR)) {
                    touchButtons |= 0x0010; // WPAD_BUTTON_PLUS
                    mBtnPlusPressed = true;
                    hitButton = true;
                }
                if (mSettings.showDpad) {
                    float dDpadX = px - dpadX;
                    float dDpadY = py - dpadY;
                    float distDpad = sqrtf(dDpadX * dDpadX + dDpadY * dDpadY);
                    if (distDpad <= dpadTotalR) {
                        hitButton = true;
                        float deadzone = 8.0f * scale;
                        if (distDpad > deadzone) {
                            if (dDpadX < -deadzone && fabsf(dDpadX) > fabsf(dDpadY) * 0.40f) {
                                touchButtons |= 0x0001; // WPAD_BUTTON_LEFT (rotate cam left)
                                mBtnDpadLeft = true;
                            } else if (dDpadX > deadzone && fabsf(dDpadX) > fabsf(dDpadY) * 0.40f) {
                                touchButtons |= 0x0002; // WPAD_BUTTON_RIGHT (rotate cam right)
                                mBtnDpadRight = true;
                            }
                            if (dDpadY < -deadzone && fabsf(dDpadY) > fabsf(dDpadX) * 0.40f) {
                                touchButtons |= 0x0008; // WPAD_BUTTON_UP (first-person view / zoom)
                                mBtnDpadUp = true;
                            } else if (dDpadY > deadzone && fabsf(dDpadY) > fabsf(dDpadX) * 0.40f) {
                                touchButtons |= 0x0004; // WPAD_BUTTON_DOWN (recenter camera)
                                mBtnDpadDown = true;
                            }
                        }
                    }
                }
            }

            // Direct screen touch: exclusively moves the in-game Star Pointer!
            // No phantom buttons behind the HUD: button A is exclusively pressed via the on-screen [A] button!
            if (!hitButton && vpW > 0 && vpH > 0) {
                mPointerX = ((px - vpX) / (float)vpW) * 2.0f - 1.0f;
                mPointerY = ((py - vpY) / (float)vpH) * 2.0f - 1.0f;
                mPointerX = std::max(-1.0f, std::min(1.0f, mPointerX));
                mPointerY = std::max(-1.0f, std::min(1.0f, mPointerY));
            }
        }
    }

    if (!stickFound && mStickPointerId >= 0) {
        mStickPointerId = -1;
        mStickKnobX = mStickBaseX;
        mStickKnobY = mStickBaseY;
        pad.stickX = 0.0f;
        pad.stickY = 0.0f;
        mDirty = true;
    }

    pad.buttons = touchButtons;
    pad.pointerValid = 1;
    pad.pointerX = mPointerX;
    pad.pointerY = mPointerY;
    pad.pointerDist = 2.0f;

    return true;
}

void UIOverlay::render(int screenW, int screenH) {
    if (!init()) return;

    std::vector<UIVertex> verts;
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
    auto drawText = [&](const char* text, float x, float baseline, float sizeScale, const Color& c, int align = 0) {
        float strW = 0.0f;
        for (const char* p = text; *p; p++) {
            char ch = *p;
            if (ch >= 32 && ch < 127) {
                strW += (face.glyphs[ch - 32].advance * 0.25f) * sizeScale;
            }
        }
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
        float btnBaseX = w - 160.0f * scale + mSettings.buttonOffsetX;
        float btnBaseY = h - 160.0f * scale + mSettings.buttonOffsetY;

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
        drawModernButton(btnBaseX - 120.0f * scale, btnBaseY + 25.0f * scale, 45.0f * scale, "B", 0xFBBF24, mBtnBPressed);

        // Button SPIN - Luminous Emerald Mint (#34D399) with Cyclone Glyph
        drawModernButton(btnBaseX + 35.0f * scale, btnBaseY - 130.0f * scale, 48.0f * scale, "SPIN", 0x34D399, mBtnSpinPressed, 1);

        // Button Z (Crouch) - Luminous Lavender Violet (#A78BFA)
        drawModernButton(btnBaseX - 90.0f * scale, btnBaseY - 105.0f * scale, 44.0f * scale, "Z", 0xA78BFA, mBtnZPressed);

        // Button + (Pause) - Minimalist Pearl Slate (#E2E8F0) with Vector Cross
        drawModernButton(w - 70.0f * scale, 70.0f * scale, 34.0f * scale, "+", 0x94A3B8, mBtnPlusPressed, 2);

        // ---------------------------------------------------------
        // 3. Camera D-Pad - Directional Precision Disc with Chevrons & Camera Icon
        // ---------------------------------------------------------
        if (mSettings.showDpad) {
            float baseDpadX = mSettings.dpadOnRight ? (w - 380.0f * scale) : (140.0f * scale);
            float baseDpadY = mSettings.dpadOnRight ? (h - 220.0f * scale) : (h - 390.0f * scale);
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

            // 4 Directional Chevron Sectors (◀ ▶ ▲ ▼)
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

            // Center Concave Hub with Modern Camera Icon (📷)
            float hubR = 18.0f * scale;
            drawCircle(dpadX, dpadY, hubR, rgba(0.09f, 0.13f, 0.22f, hudOp * 0.80f));
            drawRing(dpadX, dpadY, hubR, 0.88f, rgba(0.55f, 0.75f, 1.0f, hudOp * 0.55f));
            drawCameraIcon(dpadX, dpadY, 11.0f * scale, rgba(0.85f, 0.93f, 1.0f, hudOp * 0.90f));
        }
    }

    // -------------------------------------------------------------
    // Material 3 Settings Tab Dock (Pinned to Top-Left Edge ⚙)
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
        float totalContentH = (mSettingsTab == 0 || !mPlusEdition) ? (455.0f * uiScale) : (340.0f * uiScale);
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

            // Row 0: Visibility Switch
            renderRow(8.0f * uiScale, 46.0f * uiScale, [&](float y) {
                drawText("On-Screen Controls", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Show HUD buttons", drawerX + padX, y + 36.0f * uiScale, 0.29f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.showControls);
            });

            // Row 1: Camera D-Pad Switch
            renderRow(58.0f * uiScale, 46.0f * uiScale, [&](float y) {
                drawText("Camera D-Pad", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Limited camera rotation & reset", drawerX + padX, y + 36.0f * uiScale, 0.29f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.showDpad);
            });

            // Row 2: D-Pad Position Segmented Button
            renderRow(108.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("D-Pad Position", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* dpadSideOpts[2] = {"Left Side", "Right Side"};
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, dpadSideOpts, 2, mSettings.dpadOnRight ? 1 : 0);
            });

            // Row 3: Fullscreen Switch
            renderRow(166.0f * uiScale, 46.0f * uiScale, [&](float y) {
                drawText("Fullscreen", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Native ultrawide (expand Hor+ FOV)", drawerX + padX, y + 36.0f * uiScale, 0.29f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.fullscreen);
            });

            // Row 4: Joystick Mode Segmented Button
            renderRow(216.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Joystick Mode", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* joyOpts[2] = {"Dynamic", "Fixed"};
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, joyOpts, 2, mSettings.dynamicStick ? 0 : 1);
            });

            // Row 5: Controls Opacity Segmented Button
            renderRow(274.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Controls Opacity", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* opOpts[4] = {"25%", "50%", "75%", "100%"};
                int opIdx = (fabsf(mSettings.opacity - 0.25f) < 0.12f) ? 0 : (fabsf(mSettings.opacity - 0.50f) < 0.12f) ? 1 : (fabsf(mSettings.opacity - 0.75f) < 0.12f) ? 2 : 3;
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, opOpts, 4, opIdx);
            });

            // Row 6: Button Size Segmented Button
            renderRow(332.0f * uiScale, 54.0f * uiScale, [&](float y) {
                drawText("Button Size", drawerX + padX, y + 14.0f * uiScale, 0.38f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                const char* scOpts[3] = {"Small", "Medium", "Large"};
                int scIdx = (fabsf(mSettings.scale - 0.85f) < 0.08f) ? 0 : (fabsf(mSettings.scale - 1.25f) < 0.08f) ? 2 : 1;
                drawSegmentedGroup(drawerX + padX, y + 22.0f * uiScale, contentW, 34.0f * uiScale, scOpts, 3, scIdx);
            });

            // Row 7: Layout Reposition & Reset Buttons
            renderRow(390.0f * uiScale, 54.0f * uiScale, [&](float y) {
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
        } else {
            // Advanced Tab Settings Items (Gameplay Mods)
            auto renderRow = [&](float relY, float rowH, auto func) {
                float drawY = contentTop + relY - mDrawerScrollY;
                if (drawY + rowH >= contentTop - 12.0f && drawY <= contentBottom + 12.0f) {
                    func(drawY);
                }
            };

            // Section Header
            renderRow(6.0f * uiScale, 36.0f * uiScale, [&](float y) {
                drawText("Special Game Mods", drawerX + padX, y + 16.0f * uiScale, 0.40f * uiScale, rgba(0.92f, 0.94f, 0.98f, 1.0f), 0);
                drawText("Cheats & speedrun tools", drawerX + padX, y + 32.0f * uiScale, 0.28f * uiScale, rgba(0.65f, 0.75f, 0.90f, 0.85f), 0);
            });

            // Mod 1: Fly Mode Switch
            renderRow(48.0f * uiScale, 52.0f * uiScale, [&](float y) {
                drawText("Fly Mode", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Hold A to fly up, hold Z to fly down", drawerX + padX, y + 36.0f * uiScale, 0.28f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.modFly);
            });

            // Mod 2: Cutscene Prevention Switch
            renderRow(110.0f * uiScale, 52.0f * uiScale, [&](float y) {
                drawText("Cutscene Prevention", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Instantly skip video cutscenes", drawerX + padX, y + 36.0f * uiScale, 0.28f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.modSkipCutscenes);
            });

            // Mod 3: Infinite Health Switch
            renderRow(172.0f * uiScale, 52.0f * uiScale, [&](float y) {
                drawText("Infinite Health", drawerX + padX, y + 20.0f * uiScale, 0.40f * uiScale, rgba(0.902f, 0.906f, 0.922f, 1.0f), 0);
                drawText("Invincible; teleports out of black holes", drawerX + padX, y + 36.0f * uiScale, 0.28f * uiScale, rgba(0.761f, 0.776f, 0.812f, 0.85f), 0);
                drawM3Switch(drawerX + drawerW - padX - swW, y + 10.0f * uiScale, swW, swH, mSettings.modInfiniteHealth);
            });

            // Info Card
            renderRow(238.0f * uiScale, 84.0f * uiScale, [&](float y) {
                drawRoundRect(drawerX + padX, y + 6.0f * uiScale, drawerX + drawerW - padX, y + 80.0f * uiScale, 14.0f * uiScale, rgba(0.14f, 0.17f, 0.22f, 0.95f));
                drawRoundRect(drawerX + padX, y + 6.0f * uiScale, drawerX + drawerW - padX, y + 80.0f * uiScale, 14.0f * uiScale, rgba(0.24f, 0.32f, 0.45f, 0.60f));
                drawText("MOD STATUS", drawerX + padX + 16.0f * uiScale, y + 26.0f * uiScale, 0.32f * uiScale, rgba(0.38f, 0.74f, 0.98f, 0.95f), 0);
                drawText("Mods take effect in real-time during gameplay.", drawerX + padX + 16.0f * uiScale, y + 44.0f * uiScale, 0.27f * uiScale, rgba(0.80f, 0.84f, 0.90f, 0.85f), 0);
                drawText("Turn toggles ON to activate each special ability.", drawerX + padX + 16.0f * uiScale, y + 62.0f * uiScale, 0.27f * uiScale, rgba(0.80f, 0.84f, 0.90f, 0.85f), 0);
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
            // Material 3 Tab Dock ([ General ]  [ Advanced ])
            float tabBoxY0 = 62.0f * uiScale;
            float tabBoxY1 = 98.0f * uiScale;
            drawRoundRect(drawerX + padX, tabBoxY0, drawerX + drawerW - padX, tabBoxY1, 18.0f * uiScale, rgba(0.149f, 0.160f, 0.190f, 0.95f));
            drawRoundRect(drawerX + padX, tabBoxY0, drawerX + drawerW - padX, tabBoxY1, 18.0f * uiScale, rgba(0.24f, 0.26f, 0.30f, 0.50f));

            float tabMidX = drawerX + padX + contentW * 0.5f;
            if (mSettingsTab == 0) {
                // General Tab Active
                drawRoundRect(drawerX + padX + 2.0f * uiScale, tabBoxY0 + 2.0f * uiScale, tabMidX - 2.0f * uiScale, tabBoxY1 - 2.0f * uiScale, 16.0f * uiScale, rgba(0.14f, 0.28f, 0.48f, 0.95f));
                drawText("General", (drawerX + padX + tabMidX) * 0.5f, tabBoxY0 + 23.0f * uiScale, 0.36f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
                drawText("Advanced", (tabMidX + drawerX + drawerW - padX) * 0.5f, tabBoxY0 + 23.0f * uiScale, 0.36f * uiScale, rgba(0.68f, 0.71f, 0.77f, 0.75f), 1);
            } else {
                // Advanced Tab Active
                drawRoundRect(tabMidX + 2.0f * uiScale, tabBoxY0 + 2.0f * uiScale, drawerX + drawerW - padX - 2.0f * uiScale, tabBoxY1 - 2.0f * uiScale, 16.0f * uiScale, rgba(0.14f, 0.28f, 0.48f, 0.95f));
                drawText("General", (drawerX + padX + tabMidX) * 0.5f, tabBoxY0 + 23.0f * uiScale, 0.36f * uiScale, rgba(0.68f, 0.71f, 0.77f, 0.75f), 1);
                drawText("Advanced", (tabMidX + drawerX + drawerW - padX) * 0.5f, tabBoxY0 + 23.0f * uiScale, 0.36f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
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
        drawText("Done", (doneX0 + doneX1) * 0.5f, doneY + 24.0f * uiScale, 0.40f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
    }

    if (verts.empty()) return;

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

}  // namespace mobile_ui
