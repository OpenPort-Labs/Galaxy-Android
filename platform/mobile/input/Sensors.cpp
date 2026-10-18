#include "Sensors.h"

#include <android/log.h>
#include <algorithm>
#include <math.h>

#include "../ui/UIOverlay.h"
#include "port/port.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GalaxySensors", __VA_ARGS__)

namespace mobile_input {

Sensors& Sensors::get() {
    static Sensors sInstance;
    return sInstance;
}

void Sensors::init(android_app* app) {
    if (!mSensorManager) {
        mSensorManager = ASensorManager_getInstanceForPackage(nullptr);
        if (!mSensorManager) {
            mSensorManager = ASensorManager_getInstance();
        }
    }
    if (mSensorManager && !mSensorQueue) {
        mGyroSensor = ASensorManager_getDefaultSensor(mSensorManager, ASENSOR_TYPE_GYROSCOPE);
        mAccelSensor = ASensorManager_getDefaultSensor(mSensorManager, ASENSOR_TYPE_ACCELEROMETER);
        mSensorQueue = ASensorManager_createEventQueue(mSensorManager, app->looper, 3, nullptr, nullptr);
        mSensorsActive = false;
    }
}

void Sensors::setActive(bool enable) {
    if (!mSensorQueue) return;
    if (enable && !mSensorsActive) {
        if (mGyroSensor) {
            ASensorEventQueue_enableSensor(mSensorQueue, mGyroSensor);
            ASensorEventQueue_setEventRate(mSensorQueue, mGyroSensor, 16666);
            LOGI("Activated gyroscope");
        }
        if (mAccelSensor) {
            ASensorEventQueue_enableSensor(mSensorQueue, mAccelSensor);
            ASensorEventQueue_setEventRate(mSensorQueue, mAccelSensor, 16666);
            LOGI("Activated accelerometer");
        }
        mSensorsActive = true;
    } else if (!enable && mSensorsActive) {
        if (mGyroSensor) {
            ASensorEventQueue_disableSensor(mSensorQueue, mGyroSensor);
        }
        if (mAccelSensor) {
            ASensorEventQueue_disableSensor(mSensorQueue, mAccelSensor);
        }
        mSensorsActive = false;
        LOGI("Deactivated sensors");
    }
}

void Sensors::poll(PortPadState& pad, bool isStickActive) {
    if (!mSensorQueue) return;
    if (!mSensorsActive) {
        setActive(true);
    }

    ASensorEvent event;
    while (ASensorEventQueue_getEvents(mSensorQueue, &event, 1) > 0) {
        if (!mSensorsActive) continue;
        if (event.type == ASENSOR_TYPE_GYROSCOPE) {
            if (mobile_ui::UIOverlay::get().settings().gyroAiming) {
                float rotH = event.vector.x - event.vector.z * 0.35f;
                float rotV = event.vector.y;

                const auto& settings = mobile_ui::UIOverlay::get().settings();
                if (settings.gyroInvertX) rotH = -rotH;
                if (settings.gyroInvertY) rotV = -rotV;

                float sens = 0.035f;
                pad.pointerX += rotH * sens;
                pad.pointerY -= rotV * sens;
                pad.pointerX = std::max(-1.0f, std::min(1.0f, pad.pointerX));
                pad.pointerY = std::max(-1.0f, std::min(1.0f, pad.pointerY));
                pad.pointerValid = 1;
                mobile_ui::UIOverlay::get().setPointerPos(pad.pointerX, pad.pointerY);
                port_input_set(0, &pad);
            }
        } else if (event.type == ASENSOR_TYPE_ACCELEROMETER) {
            float rawRoll, rawPitch;
            if (mobile_ui::UIOverlay::get().settings().screenOrientationChoice == 2) {
                rawRoll = (event.acceleration.y / 9.81f) * 1.4f;
                rawPitch = ((-event.acceleration.x - 3.5f) / 9.81f) * 1.4f;
            } else {
                rawRoll = (-event.acceleration.y / 9.81f) * 1.4f;
                rawPitch = ((event.acceleration.x - 3.5f) / 9.81f) * 1.4f;
            }
            rawRoll = std::max(-1.0f, std::min(1.0f, rawRoll));
            rawPitch = std::max(-1.0f, std::min(1.0f, rawPitch));

            if (!isStickActive) {
                if (fabsf(rawRoll) > 0.05f || fabsf(rawPitch) > 0.05f) {
                    pad.tilted = 1;
                    pad.downX = rawRoll;
                    pad.downZ = rawPitch;
                    float rem = 1.0f - (pad.downX * pad.downX + pad.downZ * pad.downZ);
                    pad.downY = -(rem > 0.0f ? sqrtf(rem) : 0.0f);
                } else {
                    pad.tilted = 0;
                }
            } else {
                pad.tilted = 0;
            }

            if (mobile_ui::UIOverlay::get().settings().gyroAiming && !mGyroSensor) {
                float tiltX = -event.acceleration.y * 0.15f;
                float tiltY = event.acceleration.x * 0.15f;
                const auto& settings = mobile_ui::UIOverlay::get().settings();
                if (settings.gyroInvertX) tiltX = -tiltX;
                if (settings.gyroInvertY) tiltY = -tiltY;
                pad.pointerX = std::max(-1.0f, std::min(1.0f, tiltX));
                pad.pointerY = std::max(-1.0f, std::min(1.0f, tiltY));
                pad.pointerValid = 1;
                mobile_ui::UIOverlay::get().setPointerPos(pad.pointerX, pad.pointerY);
            }
            port_input_set(0, &pad);
        }
    }
}

}  // namespace mobile_input
