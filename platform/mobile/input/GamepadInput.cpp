#include "GamepadInput.h"

#include <android/keycodes.h>
#include <algorithm>
#include <math.h>

#include "../ui/UIOverlay.h"
#include "port/port.h"

namespace mobile_input {

bool GamepadInput::handleMotionEvent(AInputEvent* event, PortPadState& pad, int& spinFrames, bool touchscreenDown) {
    int32_t source = AInputEvent_getSource(event);
    bool isJoystick = (source & AINPUT_SOURCE_CLASS_JOYSTICK) != 0 ||
                      ((source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD) ||
                      ((source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK);
    if (!isJoystick) return false;

    if (!mGamepadDetected) {
        mGamepadDetected = true;
        mobile_ui::UIOverlay::get().showToast("Gamepad Connected");
    }

    // Left Analog Stick
    float rawX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
    float rawY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
    float stickMag = sqrtf(rawX * rawX + rawY * rawY);
    const float kDeadzone = 0.18f;
    if (stickMag <= kDeadzone) {
        mPhysicalStickX = 0.0f;
        mPhysicalStickY = 0.0f;
    } else {
        float norm = (stickMag - kDeadzone) / (1.0f - kDeadzone);
        float sens = mobile_ui::UIOverlay::get().settings().stickSensitivity;
        mPhysicalStickX = (rawX / stickMag) * std::min(1.0f, norm * sens);
        mPhysicalStickY = -(rawY / stickMag) * std::min(1.0f, norm * sens); // Invert Y for Wii convention
    }

    // D-Pad Hat axes
    float hatX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
    float hatY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
    if (hatX < -0.5f) { mPhysicalAxisButtons |= 0x0001; mPhysicalAxisButtons &= ~0x0002; }
    else if (hatX > 0.5f) { mPhysicalAxisButtons |= 0x0002; mPhysicalAxisButtons &= ~0x0001; }
    else { mPhysicalAxisButtons &= ~0x0003; }

    if (hatY < -0.5f) { mPhysicalAxisButtons |= 0x0008; mPhysicalAxisButtons &= ~0x0004; }
    else if (hatY > 0.5f) { mPhysicalAxisButtons |= 0x0004; mPhysicalAxisButtons &= ~0x0008; }
    else { mPhysicalAxisButtons &= ~0x000C; }

    // Analog Triggers (L2 / R2)
    float rawL = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0));
    float rawBrake = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_BRAKE, 0));
    float lTrig = std::max(rawL, rawBrake);

    float rawR = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0));
    float rawGas = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_GAS, 0));
    float rTrig = std::max(rawR, rawGas);

    if (lTrig > 0.4f) mPhysicalAxisButtons |= 0x2000; // Z button (Crouch / Ground Pound)
    else mPhysicalAxisButtons &= ~0x2000;

    if (rTrig > 0.4f) spinFrames = 6; // Spin shake

    // Right Stick (Orbital camera & Star Pointer)
    float rightX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
    float rightY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
    if (fabsf(rightX) <= 0.05f && fabsf(rightY) <= 0.05f) {
        rightX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RX, 0);
        rightY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RY, 0);
    }
    float deadzone = 0.08f;
    float mag = sqrtf(rightX * rightX + rightY * rightY);
    float rStickX = 0.0f, rStickY = 0.0f;
    if (mag > deadzone) {
        float scale = std::min(1.0f, (mag - deadzone) / (1.0f - deadzone));
        rStickX = (rightX / mag) * scale;
        rStickY = (-rightY / mag) * scale;
    }
    float starSens = mobile_ui::UIOverlay::get().settings().starStickSensitivity;
    if (mobile_ui::UIOverlay::get().settings().rightStickMode == 0) {
        pad.rightStickX = std::max(-1.0f, std::min(1.0f, -rStickX * starSens));
        pad.rightStickY = std::max(-1.0f, std::min(1.0f, rStickY * starSens));
    } else {
        pad.rightStickX = 0.0f;
        pad.rightStickY = 0.0f;
        if ((fabsf(rStickX) > 0.01f || fabsf(rStickY) > 0.01f) && !touchscreenDown) {
            float speed = 0.035f * starSens;
            pad.pointerX = std::max(-1.0f, std::min(1.0f, pad.pointerX + rStickX * speed));
            pad.pointerY = std::max(-1.0f, std::min(1.0f, pad.pointerY + rStickY * speed));
            pad.pointerValid = 1;
            mobile_ui::UIOverlay::get().setPointerPos(pad.pointerX, pad.pointerY);
        }
    }

    pad.connected = 1;
    return true;
}

bool GamepadInput::handleKeyEvent(AInputEvent* event, PortPadState& pad, int& spinFrames) {
    int32_t keyCode = AKeyEvent_getKeyCode(event);
    if (keyCode == AKEYCODE_VOLUME_UP || keyCode == AKEYCODE_VOLUME_DOWN || keyCode == AKEYCODE_VOLUME_MUTE) {
        return false;
    }

    if (!mGamepadDetected && (keyCode >= AKEYCODE_BUTTON_A && keyCode <= AKEYCODE_BUTTON_16)) {
        mGamepadDetected = true;
        mobile_ui::UIOverlay::get().showToast("Gamepad Connected");
    }

    int32_t keyAction = AKeyEvent_getAction(event);
    bool down = (keyAction == AKEY_EVENT_ACTION_DOWN);

    switch (keyCode) {
    case AKEYCODE_BUTTON_A:
    case AKEYCODE_BUTTON_1:
    case AKEYCODE_DPAD_CENTER:
    case AKEYCODE_ENTER:
    case AKEYCODE_NUMPAD_ENTER:
        pad.connected = 1;
        if (down) mPhysicalButtons |= 0x0800;  // Switch A: Jump / Confirm
        else mPhysicalButtons &= ~0x0800;
        break;
    case AKEYCODE_BUTTON_B:
    case AKEYCODE_BUTTON_2:
    case AKEYCODE_BUTTON_C:
        pad.connected = 1;
        if (down) {
            mPhysicalButtons |= 0x0800; // Jump
            mPhysicalButtons |= 0x0400; // Cancel/Shoot
        } else {
            mPhysicalButtons &= ~0x0800;
            mPhysicalButtons &= ~0x0400;
        }
        break;
    case AKEYCODE_BUTTON_X:
    case AKEYCODE_BUTTON_3:
    case AKEYCODE_BUTTON_Y:
    case AKEYCODE_BUTTON_4:
        pad.connected = 1;
        if (down) spinFrames = 6;  // Spin Attack
        break;
    case AKEYCODE_BUTTON_L1:
    case AKEYCODE_BUTTON_5:
        pad.connected = 1;
        if (down) mPhysicalButtons |= 0x1000;  // Switch L: Camera snap behind Mario (C button)
        else mPhysicalButtons &= ~0x1000;
        break;
    case AKEYCODE_BUTTON_R1:
    case AKEYCODE_BUTTON_6:
        pad.connected = 1;
        if (down) {
            pad.pointerX = 0.0f;
            pad.pointerY = 0.0f;
            pad.pointerValid = 1;
            mobile_ui::UIOverlay::get().setPointerPos(0.0f, 0.0f);
            mPhysicalButtons |= 0x1000;
        } else {
            mPhysicalButtons &= ~0x1000;
        }
        break;
    case AKEYCODE_BUTTON_L2:
    case AKEYCODE_BUTTON_7:
    case AKEYCODE_BUTTON_Z:
    case AKEYCODE_BUTTON_THUMBL:
    case AKEYCODE_BUTTON_11:
        pad.connected = 1;
        if (down) mPhysicalButtons |= 0x2000;  // Crouch / Ground Pound
        else mPhysicalButtons &= ~0x2000;
        break;
    case AKEYCODE_BUTTON_R2:
    case AKEYCODE_BUTTON_8:
        pad.connected = 1;
        if (down) {
            mPhysicalButtons |= 0x0400;
            mPhysicalButtons |= 0x2000;
        } else {
            mPhysicalButtons &= ~0x0400;
            mPhysicalButtons &= ~0x2000;
        }
        break;
    case AKEYCODE_BUTTON_THUMBR:
    case AKEYCODE_BUTTON_12:
        pad.connected = 1;
        if (down) {
            pad.pointerX = 0.0f;
            pad.pointerY = 0.0f;
            pad.pointerValid = 1;
            mobile_ui::UIOverlay::get().setPointerPos(0.0f, 0.0f);
        }
        break;
    case AKEYCODE_BUTTON_START:
    case AKEYCODE_BUTTON_10:
    case AKEYCODE_BUTTON_SELECT:
    case AKEYCODE_BUTTON_9:
    case AKEYCODE_BUTTON_MODE:
    case AKEYCODE_MENU:
        pad.connected = 1;
        if (down) mPhysicalButtons |= 0x0010;  // Plus / Pause
        else mPhysicalButtons &= ~0x0010;
        break;
    case AKEYCODE_DPAD_UP:
        pad.connected = 1;
        if (down) mPhysicalButtons |= 0x0008;
        else mPhysicalButtons &= ~0x0008;
        break;
    case AKEYCODE_DPAD_DOWN:
        pad.connected = 1;
        if (down) mPhysicalButtons |= 0x0004;
        else mPhysicalButtons &= ~0x0004;
        break;
    case AKEYCODE_DPAD_LEFT:
        pad.connected = 1;
        if (down) mPhysicalButtons |= 0x0001;
        else mPhysicalButtons &= ~0x0001;
        break;
    case AKEYCODE_DPAD_RIGHT:
        pad.connected = 1;
        if (down) mPhysicalButtons |= 0x0002;
        else mPhysicalButtons &= ~0x0002;
        break;
    case AKEYCODE_BACK:
    case AKEYCODE_ESCAPE:
        if (down) {
            if (mobile_ui::UIOverlay::get().isSettingsOpen()) {
                mobile_ui::UIOverlay::get().closeSettings();
            } else {
                mPhysicalButtons |= 0x0010;
            }
        } else {
            mPhysicalButtons &= ~0x0010;
        }
        return true;
    default:
        return false;
    }

    return true;
}

void GamepadInput::reset() {
    mPhysicalButtons = 0;
    mPhysicalAxisButtons = 0;
}

}  // namespace mobile_input
