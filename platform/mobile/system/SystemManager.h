#pragma once

#include <stdint.h>
#include <android_native_app_glue.h>

namespace mobile_system {

class SystemManager {
public:
    // Installs crash handlers for SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT
    static void installCrashHandlers();

    // Pins calling thread to CPU big/prime performance cores
    static void pinThreadToPerformanceCores();

    // Elevates process/thread priority to reduce scheduling jitter
    static void elevateThreadPriority(int priority = -10);

    // Triggers device hardware vibration via Android JNI
    static void triggerHaptic(android_app* app, int ms);

    // Exits cleanly back to the Galaxy launcher activity via JNI
    static void returnToLauncher(android_app* app);

    // Requests Android display refresh rate (e.g. 60Hz, 90Hz, 120Hz)
    static void setPreferredRefreshRate(android_app* app, float targetFps);

    // Sets screen orientation: 0 = sensor landscape, 1 = landscape, 2 = reverse landscape
    static void enforceOrientation(android_app* app, int orientationChoice);
};

}  // namespace mobile_system
