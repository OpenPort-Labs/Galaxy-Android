#include "LayoutManager.h"

#include <android/input.h>
#include <android/log.h>
#include <math.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MobileUI", __VA_ARGS__)

namespace mobile_ui {

LayoutManager::LayoutManager() = default;

ButtonLayout LayoutManager::calculateButtonLayout(int screenW, int screenH, const Settings& settings) const {
    float baseDpi = (float)screenH / 650.0f;
    float scale = baseDpi * settings.scale;
    float w = (float)screenW;
    float h = (float)screenH;

    float baseX = w - 150.0f * scale + settings.buttonOffsetX;
    float baseY = h - 150.0f * scale + settings.buttonOffsetY;

    ButtonLayout l{};

    // Nintendo Switch Diamond Geometry:
    // B (Bottom): Primary Jump button where thumb naturally rests
    l.bx = baseX - 36.0f * scale + settings.btnBOffsetX;
    l.by = baseY + 24.0f * scale + settings.btnBOffsetY;
    l.br = 52.0f * scale;

    // A (Right): Jump & Talk / Select
    l.ax = baseX + 42.0f * scale + settings.btnAOffsetX;
    l.ay = baseY - 42.0f * scale + settings.btnAOffsetY;
    l.ar = 50.0f * scale;

    // Y / SPIN (Left): Spin Attack
    l.spinX = baseX - 114.0f * scale + settings.btnSpinOffsetX;
    l.spinY = baseY - 42.0f * scale + settings.btnSpinOffsetY;
    l.spinR = 50.0f * scale;

    // ZL / Z (Upper-Left): Crouch / Ground Pound
    l.zx = baseX - 105.0f * scale + settings.btnZOffsetX;
    l.zy = baseY - 134.0f * scale + settings.btnZOffsetY;
    l.zr = 44.0f * scale;

    return l;
}

StarStickLayout LayoutManager::calculateStarStickLayout(int screenW, int screenH, const Settings& settings) const {
    float baseDpi = (float)screenH / 650.0f;
    float scale = baseDpi * settings.scale;
    float w = (float)screenW;
    float h = (float)screenH;

    float baseStarX = w - 180.0f * scale;
    float baseStarY = h - 410.0f * scale;

    StarStickLayout l{};
    l.cx = baseStarX + settings.starStickOffsetX;
    l.cy = baseStarY + settings.starStickOffsetY;
    l.r = 70.0f * scale;
    return l;
}

void LayoutManager::clearLayoutOffsets(Settings& settings) {
    settings.stickOffsetX = 0.0f;
    settings.stickOffsetY = 0.0f;
    settings.buttonOffsetX = 0.0f;
    settings.buttonOffsetY = 0.0f;
    settings.btnAOffsetX = 0.0f;
    settings.btnAOffsetY = 0.0f;
    settings.btnBOffsetX = 0.0f;
    settings.btnBOffsetY = 0.0f;
    settings.btnZOffsetX = 0.0f;
    settings.btnZOffsetY = 0.0f;
    settings.btnSpinOffsetX = 0.0f;
    settings.btnSpinOffsetY = 0.0f;
    settings.starStickOffsetX = 0.0f;
    settings.starStickOffsetY = 0.0f;
    settings.dpadOffsetX = 0.0f;
    settings.dpadOffsetY = 0.0f;
}

void LayoutManager::resetLayoutOffsets(Settings& settings, const std::string& savePath) {
    clearLayoutOffsets(settings);
    if (!savePath.empty()) {
        settings.save(savePath);
    }
}

bool LayoutManager::handleEditTouch(float px, float py, int32_t action, int screenW, int screenH,
                                   Settings& settings, const std::string& savePath) {
    float baseDpi = (float)screenH / 650.0f;
    float scale = baseDpi * settings.scale;
    float w = (float)screenW;
    float h = (float)screenH;

    if (action == AMOTION_EVENT_ACTION_DOWN) {
        ButtonLayout bL = calculateButtonLayout(screenW, screenH, settings);
        float dA = (px - bL.ax) * (px - bL.ax) + (py - bL.ay) * (py - bL.ay);
        float dB = (px - bL.bx) * (px - bL.bx) + (py - bL.by) * (py - bL.by);
        float dZ = (px - bL.zx) * (px - bL.zx) + (py - bL.zy) * (py - bL.zy);
        float dSpin = (px - bL.spinX) * (px - bL.spinX) + (py - bL.spinY) * (py - bL.spinY);

        int picked = -1;
        float best = 1e9f;
        if (dA <= bL.ar * bL.ar && dA < best) { picked = 0; best = dA; }
        if (dB <= bL.br * bL.br && dB < best) { picked = 1; best = dB; }
        if (dZ <= bL.zr * bL.zr && dZ < best) { picked = 2; best = dZ; }
        if (dSpin <= bL.spinR * bL.spinR && dSpin < best) { picked = 3; best = dSpin; }

        if (picked >= 0) {
            mDraggingButton = picked;
            mDragStartX = px;
            mDragStartY = py;
            switch (picked) {
            case 0: mInitialOwnOfsX = settings.btnAOffsetX; mInitialOwnOfsY = settings.btnAOffsetY; break;
            case 1: mInitialOwnOfsX = settings.btnBOffsetX; mInitialOwnOfsY = settings.btnBOffsetY; break;
            case 2: mInitialOwnOfsX = settings.btnZOffsetX; mInitialOwnOfsY = settings.btnZOffsetY; break;
            default: mInitialOwnOfsX = settings.btnSpinOffsetX; mInitialOwnOfsY = settings.btnSpinOffsetY; break;
            }
            return true;
        }

        // Check clusters
        float baseDpadX = settings.dpadOnRight ? (w - 380.0f * scale) : (140.0f * scale);
        float baseDpadY = settings.dpadOnRight ? (h - 220.0f * scale) : (h - 390.0f * scale);
        float curDpadX = baseDpadX + settings.dpadOffsetX;
        float curDpadY = baseDpadY + settings.dpadOffsetY;

        float curStickX = 175.0f * scale + settings.stickOffsetX;
        float curStickY = h - 175.0f * scale + settings.stickOffsetY;

        float dDpad2 = (px - curDpadX) * (px - curDpadX) + (py - curDpadY) * (py - curDpadY);
        float dStick2 = (px - curStickX) * (px - curStickX) + (py - curStickY) * (py - curStickY);

        StarStickLayout starL = calculateStarStickLayout(screenW, screenH, settings);
        float dStar2 = (px - starL.cx) * (px - starL.cx) + (py - starL.cy) * (py - starL.cy);
        float starHitR = starL.r * 1.30f;

        if (settings.showDpad && dDpad2 <= (95.0f * scale) * (95.0f * scale)) {
            mDraggingCluster = 3;
            mDragStartX = px;
            mDragStartY = py;
            mInitialDpadOfsX = settings.dpadOffsetX;
            mInitialDpadOfsY = settings.dpadOffsetY;
        } else if (settings.starStick && dStar2 <= starHitR * starHitR) {
            mDraggingCluster = 4;
            mDragStartX = px;
            mDragStartY = py;
            mInitialStarStickOfsX = settings.starStickOffsetX;
            mInitialStarStickOfsY = settings.starStickOffsetY;
        } else if (dStick2 <= (140.0f * scale) * (140.0f * scale) || px < w * 0.40f) {
            mDraggingCluster = 1;
            mDragStartX = px;
            mDragStartY = py;
            mInitialStickOfsX = settings.stickOffsetX;
            mInitialStickOfsY = settings.stickOffsetY;
        } else {
            mDraggingCluster = 2;
            mDragStartX = px;
            mDragStartY = py;
            mInitialBtnOfsX = settings.buttonOffsetX;
            mInitialBtnOfsY = settings.buttonOffsetY;
        }
        return true;
    } else if (action == AMOTION_EVENT_ACTION_MOVE) {
        if (mDraggingButton >= 0) {
            float dx = px - mDragStartX;
            float dy = py - mDragStartY;
            switch (mDraggingButton) {
            case 0: settings.btnAOffsetX = mInitialOwnOfsX + dx; settings.btnAOffsetY = mInitialOwnOfsY + dy; break;
            case 1: settings.btnBOffsetX = mInitialOwnOfsX + dx; settings.btnBOffsetY = mInitialOwnOfsY + dy; break;
            case 2: settings.btnZOffsetX = mInitialOwnOfsX + dx; settings.btnZOffsetY = mInitialOwnOfsY + dy; break;
            default: settings.btnSpinOffsetX = mInitialOwnOfsX + dx; settings.btnSpinOffsetY = mInitialOwnOfsY + dy; break;
            }
        } else if (mDraggingCluster == 1) {
            settings.stickOffsetX = mInitialStickOfsX + (px - mDragStartX);
            settings.stickOffsetY = mInitialStickOfsY + (py - mDragStartY);
        } else if (mDraggingCluster == 2) {
            settings.buttonOffsetX = mInitialBtnOfsX + (px - mDragStartX);
            settings.buttonOffsetY = mInitialBtnOfsY + (py - mDragStartY);
        } else if (mDraggingCluster == 3) {
            settings.dpadOffsetX = mInitialDpadOfsX + (px - mDragStartX);
            settings.dpadOffsetY = mInitialDpadOfsY + (py - mDragStartY);
        } else if (mDraggingCluster == 4) {
            settings.starStickOffsetX = mInitialStarStickOfsX + (px - mDragStartX);
            settings.starStickOffsetY = mInitialStarStickOfsY + (py - mDragStartY);
        }
        return true;
    } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        if (mDraggingButton >= 0) {
            float ox = 0.0f, oy = 0.0f;
            switch (mDraggingButton) {
            case 0: ox = settings.btnAOffsetX; oy = settings.btnAOffsetY; break;
            case 1: ox = settings.btnBOffsetX; oy = settings.btnBOffsetY; break;
            case 2: ox = settings.btnZOffsetX; oy = settings.btnZOffsetY; break;
            default: ox = settings.btnSpinOffsetX; oy = settings.btnSpinOffsetY; break;
            }
            LOGI("layout edit: button %d now offset %.0f,%.0f", mDraggingButton, ox, oy);
        }
        mDraggingCluster = 0;
        mDraggingButton = -1;
        if (!savePath.empty()) settings.save(savePath);
        return true;
    }
    return true;
}

}  // namespace mobile_ui
