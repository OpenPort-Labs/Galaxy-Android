#pragma once

#include <android_native_app_glue.h>
#include <stdint.h>
#include <string>

#include "gx/vk_renderer.h"
#include "port/input.h"
#include "../display/FramePacer.h"
#include "../display/ViewportManager.h"
#include "../input/GamepadInput.h"

namespace mobile_core {

class MobileApp {
public:
    static MobileApp& get();

    // Main execution entry point invoked by port_android_main
    void run(android_app* app, uintptr_t windowBase, size_t windowSize);

    // Lifecycle command dispatcher from android_native_app_glue
    void handleAppCmd(int32_t cmd);

    // Input event dispatcher
    int32_t handleInputEvent(AInputEvent* event);

    // State getters for external port queries
    int screenWidth() const { return mScreenWidth; }
    int screenHeight() const { return mScreenHeight; }
    android_app* androidApp() const { return mAndroid; }

private:
    MobileApp() = default;

    void updateInputState();

    android_app* mAndroid = nullptr;
    gpu::VkTarget mVkTarget{};

    bool mWindowReady = false;
    bool mVkReady = false;
    bool mGameBooted = false;
    int mScreenWidth = 0;
    int mScreenHeight = 0;

    PortPadState mPad{};
    int mSpinFrames = 0;
    bool mTouchscreenDown = false;

    std::string mDataRoot;
    std::string mSaveRoot;

    mobile_display::ViewportManager mVpMgr;
    mobile_display::FramePacer mFramePacer;
    mobile_input::GamepadInput mGamepad;
};

}  // namespace mobile_core
