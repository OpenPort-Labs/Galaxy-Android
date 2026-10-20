#pragma once

#include "UICommon.h"
#include "Settings.h"
#include "LayoutManager.h"
#include "port/input.h"
#include <android/input.h>

namespace mobile_ui {

class TouchInputHandler {
public:
    TouchInputHandler();

    void update(PortPadState& pad, int& spinFrames, bool fullTouchMode);
    void reset();

    uint32_t getTouchButtons() const;

    bool isStickActive() const { return mStickPointerId >= 0; }
    float getTouchStickX() const { return (mStickPointerId >= 0) ? mTouchStickX : 0.0f; }
    float getTouchStickY() const { return (mStickPointerId >= 0) ? mTouchStickY : 0.0f; }

    bool isStarStickActive() const { return mStarStickPointerId >= 0; }
    float getTouchStarStickX() const { return (mStarStickPointerId >= 0) ? mStarStickX : 0.0f; }
    float getTouchStarStickY() const { return (mStarStickPointerId >= 0) ? mStarStickY : 0.0f; }

    void setPointerPos(float x, float y) { mPointerX = x; mPointerY = y; mPointerActiveFrames = 120; }
    float getPointerX() const { return mPointerX; }
    float getPointerY() const { return mPointerY; }
    bool isPointerActive() const { return mPointerActiveFrames > 0 || mStarStickPointerId >= 0; }

    bool handleTouch(const AInputEvent* event, PortPadState& pad, int& spinFrames,
                     int screenW, int screenH, int vpX, int vpY, int vpW, int vpH,
                     const Settings& settings, const LayoutManager& layoutMgr,
                     bool& outOpenSettings, bool& outQuickSave);

    bool btnAPressed() const { return mBtnAPressed; }
    bool btnBPressed() const { return mBtnBPressed; }
    bool btnZPressed() const { return mBtnZPressed; }
    bool btnSpinPressed() const { return mBtnSpinPressed; }
    bool btnPlusPressed() const { return mBtnPlusPressed; }
    bool btnDpadLeft() const { return mBtnDpadLeft; }
    bool btnDpadRight() const { return mBtnDpadRight; }
    bool btnDpadUp() const { return mBtnDpadUp; }
    bool btnDpadDown() const { return mBtnDpadDown; }
    bool btnCfgPressed() const { return mBtnCfgPressed; }

    float stickBaseX() const { return mStickBaseX; }
    float stickBaseY() const { return mStickBaseY; }
    float stickKnobX() const { return mStickKnobX; }
    float stickKnobY() const { return mStickKnobY; }

    float starStickBaseX() const { return mStarStickBaseX; }
    float starStickBaseY() const { return mStarStickBaseY; }
    float starStickKnobX() const { return mStarStickKnobX; }
    float starStickKnobY() const { return mStarStickKnobY; }

private:
    TouchPoint mTouches[10];

    int mStickPointerId = -1;
    float mStickBaseX = 0.0f, mStickBaseY = 0.0f;
    float mStickKnobX = 0.0f, mStickKnobY = 0.0f;
    float mTouchStickX = 0.0f, mTouchStickY = 0.0f;

    int mStarStickPointerId = -1;
    float mStarStickBaseX = 0.0f, mStarStickBaseY = 0.0f;
    float mStarStickKnobX = 0.0f, mStarStickKnobY = 0.0f;
    float mStarStickX = 0.0f, mStarStickY = 0.0f;

    float mPointerX = 0.0f, mPointerY = 0.0f;
    int mPointerActiveFrames = 0;

    bool mBtnAPressed = false, mBtnBPressed = false, mBtnZPressed = false;
    bool mBtnSpinPressed = false, mBtnPlusPressed = false, mBtnCfgPressed = false;
    bool mBtnDpadLeft = false, mBtnDpadRight = false, mBtnDpadUp = false, mBtnDpadDown = false;

    bool mFingerDownOnA = false, mFingerDownOnB = false, mFingerDownOnZ = false;
    bool mFingerDownOnSpin = false, mFingerDownOnPlus = false;

    void updateActivePointers(const AInputEvent* event, int32_t action, int32_t pointerIndex,
                             size_t pointerCount, const Settings& settings, const ButtonLayout& bl,
                             float plusX, float plusY, float plusR, PortPadState& pad,
                             int& spinFrames, float scale, float stickMaxR, float dpadX,
                             float dpadY, float dpadTotalR, int vpX, int vpY, int vpW, int vpH,
                             uint32_t& touchButtons);

    void assignTouchRoleOnDown(int slot, int32_t pid, float px, float py, float w, float h,
                               const Settings& settings, const LayoutManager& layout,
                               const ButtonLayout& bl, float plusX, float plusY, float plusR,
                               float dpadX, float dpadY, float dpadTotalR,
                               float defaultStickX, float defaultStickY, float stickBaseR,
                               int vpX, int vpY, int vpW, int vpH);

    int mTapPendingFrames = 0, mTapDelayFrames = 0;
    int mTouchPointerId = -1;
    int64_t mTouchStartTime = 0;
    float mTouchStartX = 0.0f, mTouchStartY = 0.0f;
};


}  // namespace mobile_ui
