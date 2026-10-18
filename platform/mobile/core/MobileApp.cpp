#include "MobileApp.h"

#include <android/log.h>
#include <atomic>
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include "GameBooter.h"
#include "../input/Sensors.h"
#include "../render/ScenePresenter.h"
#include "../render/VulkanLifecycle.h"
#include "../system/StorageManager.h"
#include "../system/SystemManager.h"
#include "../ui/UIOverlay.h"
#include "port/port.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GalaxyCore", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "GalaxyCore", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GalaxyCore", __VA_ARGS__)

extern "C" void port_mem_set_reserved_window(uintptr_t base, size_t size);
extern "C" void port_boot(const char* dataRoot, const char* saveRoot);
extern "C" void port_audio_pause(void);
extern "C" void port_audio_resume(void);
extern std::atomic<uint64_t> gPresentedFrames;

namespace mobile_core {

MobileApp& MobileApp::get() {
    static MobileApp sInstance;
    return sInstance;
}

void MobileApp::handleAppCmd(int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW: {
        bool useVulkan = (mobile_ui::UIOverlay::get().settings().graphicsBackend == 1);
        if (useVulkan) {
            LOGI("Selected Vulkan 1.0 backend, initializing VkRenderer...");
            if (!mobile_render::VulkanLifecycle::bringUp(mAndroid->window, mScreenWidth, mScreenHeight, mVkTarget, mVpMgr)) {
                LOGE("Vulkan initialization failed; falling back to OpenGL ES 3.2");
                mVkReady = false;
                mEgl.init();
                mEgl.createSurface(mAndroid->window, mScreenWidth, mScreenHeight);
            } else {
                mVkReady = true;
            }
        } else {
            mEgl.init();
            mEgl.createSurface(mAndroid->window, mScreenWidth, mScreenHeight);
        }
        mobile_system::SystemManager::enforceOrientation(mAndroid, mobile_ui::UIOverlay::get().settings().screenOrientationChoice);
        mWindowReady = true;
        break;
    }
    case APP_CMD_TERM_WINDOW:
        LOGI("APP_CMD_TERM_WINDOW");
        if (mVkReady) {
            if (mVkTarget.framebuffer) gpu::VkRenderer::get().destroyTarget(mVkTarget);
            gpu::VkRenderer::get().shutdown();
            mVkReady = false;
        } else {
            mEgl.destroySurface();
        }
        mWindowReady = false;
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        LOGI("Window resized / config changed");
        if (mVkReady) {
            gpu::VkRenderer::get().onWindowResized(mAndroid->window);
            mScreenWidth = ANativeWindow_getWidth(mAndroid->window);
            mScreenHeight = ANativeWindow_getHeight(mAndroid->window);
            mVpMgr.update(mScreenWidth, mScreenHeight, mobile_ui::UIOverlay::get().settings().fullscreen);
        } else if (mEgl.hasSurface()) {
            mEgl.destroySurface();
            mEgl.createSurface(mAndroid->window, mScreenWidth, mScreenHeight);
        }
        break;
    case APP_CMD_RESUME:
        LOGI("APP_CMD_RESUME");
        port_audio_resume();
        port_set_paused(0);
        break;
    case APP_CMD_PAUSE:
        LOGI("APP_CMD_PAUSE");
        if (mVkReady) gpu::VkRenderer::get().savePipelineCache();
        mGamepad.reset();
        mSpinFrames = 0;
        mTouchscreenDown = false;
        mPad.buttons = mobile_ui::UIOverlay::get().getTouchButtons();
        port_input_set(0, &mPad);
        port_audio_pause();
        port_set_paused(1);
        break;
    case APP_CMD_DESTROY:
        LOGI("APP_CMD_DESTROY");
        if (mVkReady) {
            if (mVkTarget.framebuffer) gpu::VkRenderer::get().destroyTarget(mVkTarget);
            gpu::VkRenderer::get().shutdown();
            mVkReady = false;
        } else {
            mEgl.destroySurface();
        }
        mWindowReady = false;
        break;
    default:
        break;
    }
}

int32_t MobileApp::handleInputEvent(AInputEvent* event) {
    int32_t eventType = AInputEvent_getType(event);
    if (eventType == AINPUT_EVENT_TYPE_MOTION) {
        if (mGamepad.handleMotionEvent(event, mPad, mSpinFrames, mTouchscreenDown)) {
            updateInputState();
            return 1;
        }

        int32_t touchAction = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (touchAction == AMOTION_EVENT_ACTION_DOWN) mTouchscreenDown = true;
        if (touchAction == AMOTION_EVENT_ACTION_UP || touchAction == AMOTION_EVENT_ACTION_CANCEL) mTouchscreenDown = false;

        mVpMgr.update(mScreenWidth, mScreenHeight, mobile_ui::UIOverlay::get().settings().fullscreen);
        mobile_display::Rect ptrBounds;
        mobile_display::ViewportManager::computePointerBounds(
            mScreenWidth, mScreenHeight, mobile_ui::UIOverlay::get().settings().fullscreen,
            mVpMgr.getViewport(), ptrBounds);

        if (mobile_ui::UIOverlay::get().handleInput(event, mPad, mSpinFrames,
                                                    mScreenWidth, mScreenHeight,
                                                    ptrBounds.x, ptrBounds.y, ptrBounds.w, ptrBounds.h)) {
            updateInputState();
            return 1;
        }
    } else if (eventType == AINPUT_EVENT_TYPE_KEY) {
        if (mGamepad.handleKeyEvent(event, mPad, mSpinFrames)) {
            updateInputState();
            return 1;
        }
    }
    return 0;
}

void MobileApp::updateInputState() {
    if (mobile_ui::UIOverlay::get().isStickActive()) {
        mPad.stickX = mobile_ui::UIOverlay::get().getTouchStickX();
        mPad.stickY = mobile_ui::UIOverlay::get().getTouchStickY();
    } else if (mGamepad.isGamepadDetected() && (fabsf(mGamepad.stickX()) > 0.001f || fabsf(mGamepad.stickY()) > 0.001f)) {
        mPad.stickX = mGamepad.stickX();
        mPad.stickY = mGamepad.stickY();
    } else {
        mPad.stickX = 0.0f;
        mPad.stickY = 0.0f;
    }
    mPad.buttons = mGamepad.buttons() | mobile_ui::UIOverlay::get().getTouchButtons();
    port_input_set(0, &mPad);
}



void MobileApp::run(android_app* app, uintptr_t windowBase, size_t windowSize) {
    mobile_system::SystemManager::installCrashHandlers();
    mobile_system::SystemManager::elevateThreadPriority(-10);
    mobile_system::SystemManager::pinThreadToPerformanceCores();

    mAndroid = app;
    app->userData = this;
    app->onAppCmd = [](android_app* a, int32_t cmd) {
        MobileApp::get().handleAppCmd(cmd);
    };
    app->onInputEvent = [](android_app* a, AInputEvent* ev) -> int32_t {
        return MobileApp::get().handleInputEvent(ev);
    };

    mobile_input::Sensors::get().init(app);
    port_mem_set_reserved_window(windowBase, windowSize);

    mPad.connected = 1;
    port_input_set(0, &mPad);
    port_skip_set_hold_seconds(0.0f);

    std::string internalPath = app->activity->internalDataPath ? app->activity->internalDataPath : "/data/data/com.openport.galaxyandroid/files";
    bool isPlus = (internalPath.find("plus") != std::string::npos);
    mobile_ui::UIOverlay::get().setPlusEdition(isPlus);

    std::string ext = app->activity->externalDataPath ? app->activity->externalDataPath : "/sdcard/Android/data/com.openport.galaxyandroid/files";
    mSaveRoot = internalPath + "/nand";
    mkdir(mSaveRoot.c_str(), 0770);
    mobile_ui::UIOverlay::get().setSavePath(mSaveRoot + (isPlus ? "/touch_settings_plus.ini" : "/touch_settings.ini"));

    int initialFps = mobile_ui::UIOverlay::get().settings().targetFps;
    if (mobile_ui::UIOverlay::get().settings().batterySaver) initialFps = 30;
    mobile_system::SystemManager::setPreferredRefreshRate(app, (float)initialFps);

    std::string primaryLog = "/sdcard/GalaxyData/galaxy_log.txt";
    FILE* tst = fopen(primaryLog.c_str(), "a");
    if (tst) {
        fclose(tst);
        port_log_file(primaryLog.c_str());
    } else {
        port_log_file((ext + "/galaxy_log.txt").c_str());
    }

    mobile_render::VulkanLifecycle::initCaches(internalPath);

    mDataRoot = mobile_system::StorageManager::findGameData(ext);
    if (mDataRoot.empty()) {
        mobile_system::StorageManager::promptStoragePermissionIfMissing(app->activity);
    }

    bool running = true;
    while (running && !app->destroyRequested) {
        int events;
        android_poll_source* source = nullptr;
        int timeout = (mWindowReady) ? 0 : 50;
        int ident;
        while ((ident = ALooper_pollOnce(timeout, nullptr, &events, (void**)&source)) >= 0) {
            if (ident == 3) {
                mobile_input::Sensors::get().poll(mPad, mobile_ui::UIOverlay::get().isStickActive());
            }
            if (source) {
                source->process(app, source);
            }
            if (app->destroyRequested) {
                running = false;
                break;
            }
            timeout = 0;
        }

        if (!mWindowReady || port_is_paused()) {
            usleep(25000);
            continue;
        }

        mobile_core::GameBooter::pollAndBoot(mGameBooted, mDataRoot, mSaveRoot, ext, mAndroid);

        mobile_ui::UIOverlay::get().update(mPad, mSpinFrames);
        if (mSpinFrames > 0) {
            float s = (mSpinFrames & 1) ? 2.5f : -2.5f;
            mPad.accX = s;
            mPad.accY = s;
            mSpinFrames--;
        } else {
            mPad.accX = 0.0f;
            mPad.accY = 0.0f;
        }
        updateInputState();

        if (mobile_ui::UIOverlay::get().settings().haptics) {
            static int sLastRumbleState = 0;
            int r = port_input_rumble(0);
            if (r && !sLastRumbleState) {
                mobile_system::SystemManager::triggerHaptic(mAndroid, 45);
            }
            sLastRumbleState = r;
        }

        if (mVkReady && gpu::VkRenderer::get().hasFailed()) {
            if (!mobile_render::VulkanLifecycle::handleDeviceLoss(
                    mAndroid->window, mScreenWidth, mScreenHeight, mVkTarget, mVpMgr, mEgl, mVkReady)) {
                continue;
            }
        }

        if (mVkReady) {
            mobile_render::ScenePresenter::presentVulkan(
                gpu::VkRenderer::get(), mVkTarget, mScreenWidth, mScreenHeight,
                mGameBooted, mVpMgr, mFramePacer, mAndroid);
        } else {
            mobile_render::ScenePresenter::presentGles(
                gpu::renderer(), mEfbTarget, mTargetReady, mScreenWidth, mScreenHeight,
                mGameBooted, mVpMgr, mFramePacer, mEgl, mAndroid);
        }
    }

    LOGI("GalaxyMobile exiting cleanly");
    mEgl.destroySurface();
    _exit(0);
}

}  // namespace mobile_core
