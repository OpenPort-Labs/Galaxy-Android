#pragma once

#include "Settings.h"
#include "SettingsMenuDefs.h"
#include "VertexBatcher.h"
#include <android/input.h>
#include <string>

namespace mobile_ui {

class SaveManager;
class PerformanceMonitor;

class SettingsMenuHandler {
public:
    SettingsMenuHandler();

    bool isOpen() const { return mSettingsOpen || mDrawerAnim > 0.01f; }
    void openSettings();
    void closeSettings(const Settings& settings, const std::string& savePath);

    float drawerAnim() const { return mDrawerAnim; }
    void updateAnimation(float dt);

    bool handleTouch(const AInputEvent* event, int screenW, int screenH,
                     Settings& settings, SaveManager& saveMgr,
                     const PerformanceMonitor& perfMon,
                     std::string& outToastMsg, bool& outEditMode,
                     bool& outResetLayout, const std::string& savePath);

    void renderDrawer(VertexBatcher& vb, int screenW, int screenH,
                      const Settings& settings, const SaveManager& saveMgr,
                      const PerformanceMonitor& perfMon, int activeBackend);

    int activeTab() const { return mSettingsTab; }

private:
    bool mSettingsOpen = false;
    float mDrawerAnim = 0.0f; // 0.0 = closed offscreen, 1.0 = fully open
    int mSettingsTab = 0;     // 0 = Controls, 1 = Graphics, 2 = Mods, 3 = Save & System

    // Scroll state
    float mDrawerScrollY = 0.0f;
    float mDrawerScrollTarget = 0.0f;
    bool mDrawerIsScrolling = false;
    int mDrawerScrollPointerId = -1;
    float mDrawerTouchStartY = 0.0f;
    float mDrawerScrollStartOffset = 0.0f;

    // Interactive Slider tracking
    MenuActionId mActiveSliderAction = ACT_NONE;
    int mSliderPointerId = -1;

    // Interactive Dropdown overlay tracking
    MenuActionId mActiveDropdownAction = ACT_NONE;
    float mDropdownX = 0.0f;
    float mDropdownY = 0.0f;
    float mDropdownW = 0.0f;
    float mDropdownH = 0.0f;
    int mDropdownCount = 0;
    const char* mDropdownLabels[6] = {nullptr};
    int mDropdownActiveIdx = 0;

    void drawSegmentedGroup(VertexBatcher& vb, float x, float y, float w, float h,
                            const char* const* labels, int count, int activeIdx, float uiScale);

    void drawSliderRow(VertexBatcher& vb, const MenuRowDef& r, float rowX0, float rowX1,
                       float drawY, float rowH, float uiScale, const Settings& settings);

    void drawDropdownRow(VertexBatcher& vb, const MenuRowDef& r, float rowX0, float rowX1,
                         float drawY, float rowH, float uiScale, const Settings& settings,
                         const SaveManager& saveMgr);

    void drawActiveDropdownOverlay(VertexBatcher& vb, float uiScale);

    void drawDrawerChrome(VertexBatcher& vb, float drawerX, float drawerW, float padX,
                          float contentW, float contentTop, float contentBottom, float h,
                          float uiScale, float cogRot, Color themeColor, int activeBackend);
};


}  // namespace mobile_ui
