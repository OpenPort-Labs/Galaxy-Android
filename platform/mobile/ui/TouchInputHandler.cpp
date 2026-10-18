#include "TouchInputHandler.h"

#include <math.h>
#include <time.h>
#include <algorithm>

extern "C" int port_is_title_screen(void);
extern "C" void port_android_vibrate(int ms);

namespace mobile_ui {

namespace {

static int64_t currentTimeMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

}  // namespace

TouchInputHandler::TouchInputHandler() = default;

void TouchInputHandler::update(PortPadState& pad, int& spinFrames, bool fullTouchMode) {
    (void)fullTouchMode;
    if (mTapDelayFrames > 0) {
        mTapDelayFrames--;
    } else if (mTapPendingFrames > 0) {
        mTapPendingFrames--;
    }

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

    pad.pointerValid = 1;
    pad.pointerX = mPointerX;
    pad.pointerY = mPointerY;
    pad.pointerDist = 2.0f;
}

uint32_t TouchInputHandler::getTouchButtons() const {
    uint32_t b = 0;
    if (mBtnAPressed) b |= 0x0800;
    if (mBtnBPressed) b |= 0x0C00;
    if (mBtnZPressed) b |= 0x2000;
    if (mBtnPlusPressed) b |= 0x0010;
    if (mBtnDpadLeft) b |= 0x0001;
    if (mBtnDpadRight) b |= 0x0002;
    if (mBtnDpadDown) b |= 0x0004;
    if (mBtnDpadUp) b |= 0x0008;
    return b;
}

bool TouchInputHandler::handleTouch(const AInputEvent* event, PortPadState& pad, int& spinFrames,
                                   int screenW, int screenH, int vpX, int vpY, int vpW, int vpH,
                                   const Settings& settings, const LayoutManager& layoutMgr,
                                   bool& outOpenSettings, bool& outQuickSave) {
    int32_t actionWithIndex = AMotionEvent_getAction(event);
    int32_t action = actionWithIndex & AMOTION_EVENT_ACTION_MASK;
    int32_t pointerIndex = (actionWithIndex & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    size_t pointerCount = AMotionEvent_getPointerCount(event);

    float baseDpi = (float)screenH / 650.0f;
    float uiScale = baseDpi;
    float scale = baseDpi * settings.scale;
    float w = (float)screenW;
    float h = (float)screenH;

    // Check Settings Tab Dock (Top-Left)
    float tabW = 56.0f * uiScale;
    float tabH = 48.0f * uiScale;
    float tabY = 22.0f * uiScale;
    float qsX = tabW + 16.0f * uiScale;
    float qsW = 58.0f * uiScale;

    if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        float px = AMotionEvent_getX(event, pointerIndex);
        float py = AMotionEvent_getY(event, pointerIndex);
        if (px >= 0.0f && px <= tabW + 8.0f * uiScale && py >= tabY - 8.0f * uiScale && py <= tabY + tabH + 8.0f * uiScale) {
            mBtnCfgPressed = true;
            outOpenSettings = true;
            return true;
        }
        if (px >= qsX && px <= qsX + qsW && py >= tabY - 4.0f * uiScale && py <= tabY + tabH + 4.0f * uiScale) {
            outQuickSave = true;
            return true;
        }
    }
    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        mBtnCfgPressed = false;
    }

    pad.connected = 1;
    uint32_t touchButtons = 0;

    mBtnAPressed = mBtnBPressed = mBtnZPressed = mBtnSpinPressed = mBtnPlusPressed = false;
    mBtnDpadLeft = mBtnDpadRight = mBtnDpadUp = mBtnDpadDown = false;

    ButtonLayout bl = layoutMgr.calculateButtonLayout(screenW, screenH, settings);
    float aX = bl.ax, aY = bl.ay, aR = bl.ar;
    float bX = bl.bx, bY = bl.by, bR = bl.br;
    float spinX = bl.spinX, spinY = bl.spinY, spinR = bl.spinR;
    float zX = bl.zx, zY = bl.zy, zR = bl.zr;
    float plusX = w - 65.0f * scale, plusY = 65.0f * scale, plusR = 34.0f * scale;

    float baseDpadX = settings.dpadOnRight ? (w - 140.0f * scale) : (130.0f * scale);
    float baseDpadY = settings.dpadOnRight ? (170.0f * scale) : (160.0f * scale);
    float dpadX = baseDpadX + settings.dpadOffsetX;
    float dpadY = baseDpadY + settings.dpadOffsetY;
    float dpadTotalR = 58.0f * scale;

    float defaultStickX = 175.0f * scale + settings.stickOffsetX;
    float defaultStickY = h - 175.0f * scale + settings.stickOffsetY;
    float stickBaseR = 95.0f * scale;
    float stickMaxR = 100.0f * scale;

    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        for (int i = 0; i < 10; i++) mTouches[i] = {};
        mStickPointerId = -1;
        mStickKnobX = mStickBaseX;
        mStickKnobY = mStickBaseY;
        mTouchStickX = mTouchStickY = 0.0f;
        pad.stickX = pad.stickY = 0.0f;
        pad.buttons &= ~0x3C1F;
        mFingerDownOnA = mFingerDownOnB = mFingerDownOnZ = mFingerDownOnSpin = mFingerDownOnPlus = false;

        if (mTouchPointerId >= 0) {
            int64_t dur = currentTimeMs() - mTouchStartTime;
            float px = AMotionEvent_getX(event, pointerIndex);
            float py = AMotionEvent_getY(event, pointerIndex);
            float fdx = px - mTouchStartX, fdy = py - mTouchStartY;
            if (port_is_title_screen() && dur < 350 && (fdx * fdx + fdy * fdy <= 45.0f * uiScale * 45.0f * uiScale)) {
                mTapPendingFrames = 6;
            }
            mTouchPointerId = -1;
        }
        return true;
    }

    if (action == AMOTION_EVENT_ACTION_POINTER_UP) {
        int32_t liftedPid = AMotionEvent_getPointerId(event, pointerIndex);
        for (int i = 0; i < 10; i++) {
            if (mTouches[i].id == liftedPid) {
                if (mTouches[i].role == TouchRole::STICK) {
                    mStickPointerId = -1;
                    mStickKnobX = mStickBaseX;
                    mStickKnobY = mStickBaseY;
                    mTouchStickX = mTouchStickY = 0.0f;
                    pad.stickX = pad.stickY = 0.0f;
                } else if (mTouches[i].role == TouchRole::STAR_STICK) {
                    mStarStickPointerId = -1;
                    mStarStickKnobX = mStarStickBaseX;
                    mStarStickKnobY = mStarStickBaseY;
                    mStarStickX = mStarStickY = 0.0f;
                } else if (mTouches[i].role == TouchRole::CAMERA_SWIPE) {
                    pad.rightStickX = pad.rightStickY = 0.0f;
                }
                mTouches[i] = {};
            }
        }
    }

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

            int bestBtnIdx = -1;
            if (settings.showControls) {
                struct BtnCandidate { TouchRole role; float distSq; float maxDistSq; };
                BtnCandidate cands[5] = {
                    {TouchRole::BTN_A, (px - aX)*(px - aX) + (py - aY)*(py - aY), (aR * 1.15f) * (aR * 1.15f)},
                    {TouchRole::BTN_B, (px - bX)*(px - bX) + (py - bY)*(py - bY), (bR * 1.15f) * (bR * 1.15f)},
                    {TouchRole::BTN_Z, (px - zX)*(px - zX) + (py - zY)*(py - zY), (zR * 1.15f) * (zR * 1.15f)},
                    {TouchRole::BTN_SPIN, (px - spinX)*(px - spinX) + (py - spinY)*(py - spinY), (spinR * 1.15f) * (spinR * 1.15f)},
                    {TouchRole::BTN_PLUS, (px - plusX)*(px - plusX) + (py - plusY)*(py - plusY), (plusR * 1.25f) * (plusR * 1.25f)},
                };
                float bestNormDist = 1.0f;
                for (int b = 0; b < 5; b++) {
                    float normDist = cands[b].distSq / cands[b].maxDistSq;
                    if (normDist <= 1.0f && normDist < bestNormDist) {
                        bestNormDist = normDist;
                        bestBtnIdx = b;
                    }
                }
            }

            StarStickLayout starL = layoutMgr.calculateStarStickLayout(screenW, screenH, settings);
            float d2Star = (px - starL.cx)*(px - starL.cx) + (py - starL.cy)*(py - starL.cy);
            bool starEligible = (settings.starStick && mStarStickPointerId < 0 && d2Star <= (starL.r * 1.45f)*(starL.r * 1.45f));

            bool dpadEligible = false;
            if (settings.showControls && settings.showDpad) {
                float d2Dpad = (px - dpadX)*(px - dpadX) + (py - dpadY)*(py - dpadY);
                dpadEligible = (d2Dpad <= (dpadTotalR * 1.35f)*(dpadTotalR * 1.35f));
            }

            bool stickEligible = false;
            if (settings.showControls && mStickPointerId < 0 && !starEligible && !dpadEligible && bestBtnIdx < 0) {
                if (settings.dynamicStick) {
                    stickEligible = (px <= w * 0.50f && (py >= 85.0f * uiScale || px > 160.0f * uiScale));
                } else {
                    float d2 = (px - defaultStickX)*(px - defaultStickX) + (py - defaultStickY)*(py - defaultStickY);
                    stickEligible = (d2 <= (stickBaseR * 2.5f)*(stickBaseR * 2.5f)) || (px <= w * 0.45f && py >= h * 0.35f);
                }
            }

            if (bestBtnIdx >= 0) {
                TouchRole btnRoles[5] = {TouchRole::BTN_A, TouchRole::BTN_B, TouchRole::BTN_Z, TouchRole::BTN_SPIN, TouchRole::BTN_PLUS};
                mTouches[slot].role = btnRoles[bestBtnIdx];
            } else if (starEligible) {
                mTouches[slot].role = TouchRole::STAR_STICK;
                mStarStickPointerId = pid;
                mStarStickBaseX = starL.cx;
                mStarStickBaseY = starL.cy;
                mStarStickKnobX = px;
                mStarStickKnobY = py;
            } else if (dpadEligible) {
                mTouches[slot].role = TouchRole::DPAD;
            } else if (stickEligible) {
                mTouches[slot].role = TouchRole::STICK;
                mStickPointerId = pid;
                mStickBaseX = settings.dynamicStick ? px : defaultStickX;
                mStickBaseY = settings.dynamicStick ? py : defaultStickY;
                mStickKnobX = px;
                mStickKnobY = py;
            } else if (settings.touchCamera && px >= w * 0.50f) {
                mTouches[slot].role = TouchRole::CAMERA_SWIPE;
                mTouches[slot].lastX = px;
                mTouches[slot].lastY = py;
            } else {
                mTouches[slot].role = TouchRole::STAR_POINTER;
            }

            if (mTouches[slot].role == TouchRole::STAR_POINTER && !settings.relativePointer && vpW > 0 && vpH > 0) {
                mPointerX = std::max(-1.0f, std::min(1.0f, ((px - vpX) / (float)vpW) * 2.0f - 1.0f));
                mPointerY = std::max(-1.0f, std::min(1.0f, ((py - vpY) / (float)vpH) * 2.0f - 1.0f));
            }
        }
    }

    updateActivePointers(event, action, pointerIndex, pointerCount, settings, pad,
                         spinFrames, scale, stickMaxR, dpadX, dpadY, dpadTotalR,
                         vpX, vpY, vpW, vpH, touchButtons);


    if (mTapPendingFrames > 0 && port_is_title_screen()) {
        touchButtons |= 0x0C00;
        mBtnBPressed = mBtnAPressed = true;
    }

    pad.buttons = (pad.buttons & ~0x3C1F) | touchButtons;
    return true;
}

}  // namespace mobile_ui
