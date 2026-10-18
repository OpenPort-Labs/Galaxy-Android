#include "UIOverlay.h"

#include <android/log.h>
#include <sys/system_properties.h>
#include <time.h>
#include <math.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MobileUI", __VA_ARGS__)

extern "C" {
void port_set_character_choice(int choice);
void port_set_odyssey_moveset(int enabled);
void port_set_coop_mode(int enabled);
void port_set_speedrun_hud_enabled(int enabled);
}

namespace mobile_ui {

UIOverlay& UIOverlay::get() {
    static UIOverlay instance;
    return instance;
}

UIOverlay::UIOverlay() {
    char value[PROP_VALUE_MAX] = {0};
    if (__system_property_get("debug.petari.editlayout", value) > 0 && value[0] == '1') {
        mLayout.setEditMode(true);
        LOGI("layout edit: opened by debug.petari.editlayout");
    }
}

bool UIOverlay::init() {
    port_set_character_choice(mSettings.characterChoice);
    port_set_odyssey_moveset(mSettings.odysseyMoveset ? 1 : 0);
    port_set_coop_mode(mSettings.coopMode ? 1 : 0);
    port_set_speedrun_hud_enabled(mSettings.speedrunHud ? 1 : 0);
    return mGles.init();
}

void UIOverlay::setSavePath(const std::string& path) {
    mSave.setSavePath(path);
    mSettings.load(path);
}

void UIOverlay::update(PortPadState& pad, int& spinFrames) {
    mTouch.update(pad, spinFrames, mSettings.fullTouchMode);
    mPerf.update(mSettings.targetFps, mSettings.batterySaver);

    std::string toast;
    if (mSave.update(toast)) {
        showToast(toast);
    }

    if (mToastTimer > 0.0f) {
        mToastTimer -= 0.016f;
        if (mToastTimer < 0.0f) mToastTimer = 0.0f;
        mDirty = true;
    }
}

void UIOverlay::showToast(const std::string& msg) {
    mToastMessage = msg;
    mToastTimer = 2.8f;
    mDirty = true;
}

void UIOverlay::triggerSaveAnywhere() {
    showToast(mSave.triggerSaveAnywhere(mSettings.haptics));
}

void UIOverlay::triggerSaveSlot(int slot) {
    showToast(mSave.triggerSaveSlot(slot, mSettings.haptics));
}

void UIOverlay::triggerLoadSlot(int slot) {
    showToast(mSave.triggerLoadSlot(slot, mSettings.haptics));
}

bool UIOverlay::handleInput(const AInputEvent* event, PortPadState& pad, int& spinFrames,
                            int screenW, int screenH, int vpX, int vpY, int vpW, int vpH) {
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return false;

    // 1. Check layout edit mode touches
    if (mLayout.isEditMode()) {
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        float px = AMotionEvent_getX(event, 0);
        float py = AMotionEvent_getY(event, 0);

        if (action == AMOTION_EVENT_ACTION_DOWN && py >= 65.0f && py <= 90.0f) {
            float w = (float)screenW;
            float saveBtnX = w * 0.5f - 110.0f;
            float rstBtnX = w * 0.5f + 10.0f;
            if (px >= saveBtnX && px <= saveBtnX + 100.0f) {
                mLayout.setEditMode(false);
                if (!mSave.savePath().empty()) mSettings.save(mSave.savePath());
                showToast("Touch Layout Saved!");
                mDirty = true;
                return true;
            }
            if (px >= rstBtnX && px <= rstBtnX + 100.0f) {
                mLayout.resetLayoutOffsets(mSettings, mSave.savePath());
                showToast("Layout reset to defaults");
                mDirty = true;
                return true;
            }
        }
        bool handled = mLayout.handleEditTouch(px, py, action, screenW, screenH, mSettings, mSave.savePath());
        mDirty = true;
        return handled;
    }

    // 2. Check settings drawer touches
    std::string toast;
    bool editMode = false;
    bool resetLayout = false;
    if (mMenu.handleTouch(event, screenW, screenH, mSettings, mSave, mPerf,
                          toast, editMode, resetLayout, mSave.savePath())) {
        if (!toast.empty()) showToast(toast);
        if (editMode) mLayout.setEditMode(true);
        if (resetLayout) mLayout.resetLayoutOffsets(mSettings, mSave.savePath());
        mDirty = true;
        return true;
    }

    // 3. Normal on-screen touch controls
    bool openSettings = false;
    bool quickSave = false;
    bool res = mTouch.handleTouch(event, pad, spinFrames, screenW, screenH,
                                 vpX, vpY, vpW, vpH, mSettings, mLayout,
                                 openSettings, quickSave);
    if (openSettings) {
        mMenu.openSettings();
        mDirty = true;
    }
    if (quickSave) {
        triggerSaveAnywhere();
    }
    mDirty = true;
    return res;
}

void UIOverlay::generateVertices(int screenW, int screenH, std::vector<UIVertex>& outVerts) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t now = (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
    if (mLastAnimTime == 0) mLastAnimTime = now;
    float dt = (float)(now - mLastAnimTime) / 1e9f;
    mLastAnimTime = now;
    if (dt < 0.0001f) dt = 0.016f;
    if (dt > 0.1f) dt = 0.1f;

    mMenu.updateAnimation(dt);

    mRenderer.generateVertices(screenW, screenH, mSettings, mLayout, mTouch,
                               mPerf, mMenu, mSave, mActiveBackend,
                               mToastMessage, mToastTimer, outVerts);
}

void UIOverlay::render(int screenW, int screenH) {
    if (!init()) return;
    std::vector<UIVertex> verts;
    generateVertices(screenW, screenH, verts);
    if (!verts.empty()) {
        mGles.render(screenW, screenH, verts);
    }
}

}  // namespace mobile_ui
