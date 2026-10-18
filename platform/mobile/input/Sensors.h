#pragma once

#include <android_native_app_glue.h>
#include <android/sensor.h>
#include "port/input.h"

namespace mobile_input {

class Sensors {
public:
    static Sensors& get();

    // Initializes sensor manager and creates event queue
    void init(android_app* app);

    // Activates or deactivates gyro and accelerometer event delivery
    void setActive(bool enable);

    // Reads pending sensor events and updates pointer and tilt angles
    void poll(PortPadState& pad, bool isStickActive);

    bool hasGyro() const { return mGyroSensor != nullptr; }

private:
    Sensors() = default;

    ASensorManager* mSensorManager = nullptr;
    const ASensor* mGyroSensor = nullptr;
    const ASensor* mAccelSensor = nullptr;
    ASensorEventQueue* mSensorQueue = nullptr;
    bool mSensorsActive = false;
};

}  // namespace mobile_input
