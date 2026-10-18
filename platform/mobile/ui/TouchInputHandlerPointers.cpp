#include "TouchInputHandler.h"

#include <math.h>
#include <algorithm>

extern "C" void port_android_vibrate(int ms);

namespace mobile_ui {

void TouchInputHandler::updateActivePointers(
    const AInputEvent* event, int32_t action, int32_t pointerIndex,
    size_t pointerCount, const Settings& settings, PortPadState& pad,
    int& spinFrames, float scale, float stickMaxR, float dpadX,
    float dpadY, float dpadTotalR, int vpX, int vpY, int vpW, int vpH,
    uint32_t& touchButtons) {

    mFingerDownOnA = mFingerDownOnB = mFingerDownOnZ = mFingerDownOnSpin = mFingerDownOnPlus = false;
    mBtnDpadLeft = mBtnDpadRight = mBtnDpadUp = mBtnDpadDown = false;
    bool stickFound = false, starStickFound = false, cameraSwipeFound = false;

    for (size_t i = 0; i < pointerCount; i++) {
        if (action == AMOTION_EVENT_ACTION_POINTER_UP && (int32_t)i == pointerIndex) continue;
        int32_t pid = AMotionEvent_getPointerId(event, i);
        float px = AMotionEvent_getX(event, i);
        float py = AMotionEvent_getY(event, i);

        TouchRole role = TouchRole::NONE;
        int slot = -1;
        for (int s = 0; s < 10; s++) {
            if (mTouches[s].id == pid) { role = mTouches[s].role; slot = s; break; }
        }

        if (role == TouchRole::STICK) {
            stickFound = true;
            float dx = px - mStickBaseX, dy = py - mStickBaseY;
            float dist = sqrtf(dx * dx + dy * dy);
            float deadzone = 10.0f * scale;
            if (dist > stickMaxR && dist > 0.001f) {
                mStickKnobX = mStickBaseX + (dx / dist) * stickMaxR;
                mStickKnobY = mStickBaseY + (dy / dist) * stickMaxR;
                mTouchStickX = (dx / dist) * settings.stickSensitivity;
                mTouchStickY = (-dy / dist) * settings.stickSensitivity;
            } else if (dist > deadzone) {
                mStickKnobX = px;
                mStickKnobY = py;
                float norm = (dist - deadzone) / (stickMaxR - deadzone);
                mTouchStickX = ((dx / dist) * norm) * settings.stickSensitivity;
                mTouchStickY = (-(dy / dist) * norm) * settings.stickSensitivity;
            } else {
                mStickKnobX = px;
                mStickKnobY = py;
                mTouchStickX = mTouchStickY = 0.0f;
            }
            pad.stickX = mTouchStickX = std::max(-1.0f, std::min(1.0f, mTouchStickX));
            pad.stickY = mTouchStickY = std::max(-1.0f, std::min(1.0f, mTouchStickY));
        } else if (role == TouchRole::DPAD) {
            float dDpadX = px - dpadX, dDpadY = py - dpadY;
            float dist = sqrtf(dDpadX * dDpadX + dDpadY * dDpadY);
            float deadzone = 8.0f * scale;
            if (dist > deadzone && dist <= dpadTotalR * 1.35f) {
                if (dDpadX < -deadzone && fabsf(dDpadX) > fabsf(dDpadY) * 0.40f) { touchButtons |= 0x0001; mBtnDpadLeft = true; }
                else if (dDpadX > deadzone && fabsf(dDpadX) > fabsf(dDpadY) * 0.40f) { touchButtons |= 0x0002; mBtnDpadRight = true; }
                if (dDpadY < -deadzone && fabsf(dDpadY) > fabsf(dDpadX) * 0.40f) { touchButtons |= 0x0008; mBtnDpadUp = true; }
                else if (dDpadY > deadzone && fabsf(dDpadY) > fabsf(dDpadX) * 0.40f) { touchButtons |= 0x0004; mBtnDpadDown = true; }
            }
        } else if (role == TouchRole::BTN_A) {
            touchButtons |= 0x0800;
            if (!mBtnAPressed && settings.haptics) port_android_vibrate(22);
            mBtnAPressed = mFingerDownOnA = true;
        } else if (role == TouchRole::BTN_B) {
            touchButtons |= 0x0C00;
            if (!mBtnBPressed && settings.haptics) port_android_vibrate(18);
            mBtnBPressed = mFingerDownOnB = true;
        } else if (role == TouchRole::BTN_Z) {
            touchButtons |= 0x2000;
            if (!mBtnZPressed && settings.haptics) port_android_vibrate(20);
            mBtnZPressed = mFingerDownOnZ = true;
        } else if (role == TouchRole::BTN_SPIN) {
            spinFrames = 6;
            if (!mBtnSpinPressed && settings.haptics) port_android_vibrate(35);
            mBtnSpinPressed = mFingerDownOnSpin = true;
        } else if (role == TouchRole::BTN_PLUS) {
            touchButtons |= 0x0010;
            if (!mBtnPlusPressed && settings.haptics) port_android_vibrate(25);
            mBtnPlusPressed = mFingerDownOnPlus = true;
        } else if (role == TouchRole::STAR_STICK) {
            starStickFound = true;
            float dx = px - mStarStickBaseX, dy = py - mStarStickBaseY;
            float dist = sqrtf(dx * dx + dy * dy);
            float deadzone = 10.0f * scale;
            if (dist > stickMaxR && dist > 0.001f) {
                mStarStickKnobX = mStarStickBaseX + (dx / dist) * stickMaxR;
                mStarStickKnobY = mStarStickBaseY + (dy / dist) * stickMaxR;
                mStarStickX = dx / dist; mStarStickY = -dy / dist;
            } else if (dist > deadzone) {
                mStarStickKnobX = px; mStarStickKnobY = py;
                float norm = (dist - deadzone) / (stickMaxR - deadzone);
                mStarStickX = (dx / dist) * norm; mStarStickY = -(dy / dist) * norm;
            } else {
                mStarStickKnobX = px; mStarStickKnobY = py;
                mStarStickX = mStarStickY = 0.0f;
            }
            float slew = 0.038f * settings.starStickSensitivity;
            mPointerX = std::max(-1.0f, std::min(1.0f, mPointerX + mStarStickX * slew));
            mPointerY = std::max(-1.0f, std::min(1.0f, mPointerY - mStarStickY * slew));
            mPointerActiveFrames = 120;
        } else if (role == TouchRole::CAMERA_SWIPE) {
            cameraSwipeFound = true;
            if (slot >= 0) {
                float dx = px - mTouches[slot].lastX;
                float dy = py - mTouches[slot].lastY;
                mTouches[slot].lastX = px;
                mTouches[slot].lastY = py;
                float deadzone = 2.0f * scale;
                if (fabsf(dx) > deadzone || fabsf(dy) > deadzone) {
                    float sens = 0.055f * settings.starStickSensitivity;
                    pad.rightStickX = std::max(-1.0f, std::min(1.0f, -dx * sens));
                    pad.rightStickY = std::max(-1.0f, std::min(1.0f, -dy * sens));
                } else {
                    pad.rightStickX = pad.rightStickY = 0.0f;
                }
            }
        } else if (role == TouchRole::STAR_POINTER && vpW > 0 && vpH > 0) {
            if (settings.relativePointer && slot >= 0) {
                float dx = px - mTouches[slot].lastX;
                float dy = py - mTouches[slot].lastY;
                mPointerX = std::max(-1.0f, std::min(1.0f, mPointerX + (dx / (float)vpW) * 2.0f));
                mPointerY = std::max(-1.0f, std::min(1.0f, mPointerY + (dy / (float)vpH) * 2.0f));
            } else {
                mPointerX = std::max(-1.0f, std::min(1.0f, ((px - vpX) / (float)vpW) * 2.0f - 1.0f));
                mPointerY = std::max(-1.0f, std::min(1.0f, ((py - vpY) / (float)vpH) * 2.0f - 1.0f));
            }
            mPointerActiveFrames = 120;
            if (slot >= 0) { mTouches[slot].lastX = px; mTouches[slot].lastY = py; }
        }
    }

    if (!stickFound) { mTouchStickX = mTouchStickY = pad.stickX = pad.stickY = 0.0f; mStickPointerId = -1; }
    if (!starStickFound) { mStarStickX = mStarStickY = 0.0f; mStarStickPointerId = -1; }
    if (!cameraSwipeFound) pad.rightStickX = pad.rightStickY = 0.0f;
}

}  // namespace mobile_ui
