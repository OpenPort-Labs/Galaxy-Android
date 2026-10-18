#pragma once

#include "UICommon.h"
#include "Settings.h"
#include "LayoutManager.h"
#include "TouchInputHandler.h"
#include "PerformanceMonitor.h"
#include "SaveManager.h"
#include "SettingsMenuHandler.h"
#include "OverlayRenderer.h"
#include "GlesOverlayPipeline.h"
#include "FontAtlas.h"

#include <string>
#include <vector>

namespace mobile_ui {

class UIOverlay {
public:
    static UIOverlay& get();

    bool init();
    void setSavePath(const std::string& path);

    void update(PortPadState& pad, int& spinFrames);

    uint32_t getTouchButtons() const { return mTouch.getTouchButtons(); }

    bool isStickActive() const { return mTouch.isStickActive(); }
    float getTouchStickX() const { return mTouch.getTouchStickX(); }
    float getTouchStickY() const { return mTouch.getTouchStickY(); }

    bool isStarStickActive() const { return mTouch.isStarStickActive(); }
    float getTouchStarStickX() const { return mTouch.getTouchStarStickX(); }
    float getTouchStarStickY() const { return mTouch.getTouchStarStickY(); }

    bool handleInput(const AInputEvent* event, PortPadState& pad, int& spinFrames,
                     int screenW, int screenH, int vpX, int vpY, int vpW, int vpH);

    void generateVertices(int screenW, int screenH, std::vector<UIVertex>& outVerts);

    static void getFontAtlas(int& width, int& height, std::vector<uint8_t>& coverage) {
        FontAtlas::getAtlas(width, height, coverage);
    }

    void render(int screenW, int screenH);

    void reportFramePresented() { mPerf.reportFramePresented(); }
    void reportGpuRenderTime(int64_t dur) { mPerf.reportGpuRenderTime(dur); }
    void resetInput() { mTouch.reset(); }

    bool needsRedraw() { return mDirty || mMenu.isOpen() || mToastTimer > 0.0f; }

    bool isSettingsOpen() const { return mMenu.isOpen(); }
    void closeSettings() { mMenu.closeSettings(mSettings, mSave.savePath()); }

    void triggerSaveAnywhere();
    void triggerSaveSlot(int slot);
    void triggerLoadSlot(int slot);

    int currentSlot() const { return mSave.currentSlot(); }
    void setCurrentSlot(int s) { mSave.setCurrentSlot(s); }

    void showToast(const std::string& msg);

    Settings& settings() { return mSettings; }
    const Settings& settings() const { return mSettings; }

    void setActiveBackend(int b) { mActiveBackend = b; }
    int activeBackend() const { return mActiveBackend; }

    void setGpuRendererName(const std::string& name) { mPerf.setGpuRendererName(name); }

    void setPlusEdition(bool plus) { mPlusEdition = plus; }
    bool isPlusEdition() const { return mPlusEdition; }

    void setPointerPos(float x, float y) { mTouch.setPointerPos(x, y); }
    float getPointerX() const { return mTouch.getPointerX(); }
    float getPointerY() const { return mTouch.getPointerY(); }

    bool isEditModeActive() const { return mLayout.isEditMode(); }
    void setEditMode(bool active) { mLayout.setEditMode(active); }
    void resetLayoutOffsets() { mLayout.resetLayoutOffsets(mSettings, mSave.savePath()); }

    bool exportSave() { return mSave.exportSave(mToastMessage); }
    bool importSave() { return mSave.importSave(mToastMessage); }

private:
    UIOverlay();

    Settings mSettings;
    LayoutManager mLayout;
    TouchInputHandler mTouch;
    PerformanceMonitor mPerf;
    SaveManager mSave;
    SettingsMenuHandler mMenu;
    OverlayRenderer mRenderer;
    GlesOverlayPipeline mGles;

    int mActiveBackend = 1;
    bool mPlusEdition = true;
    bool mDirty = false;
    int64_t mLastAnimTime = 0;

    std::string mToastMessage;
    float mToastTimer = 0.0f;
};

inline UIOverlay& get() {
    return UIOverlay::get();
}

}  // namespace mobile_ui
