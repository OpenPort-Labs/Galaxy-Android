#pragma once

#include "UICommon.h"
#include "Settings.h"
#include "LayoutManager.h"
#include "TouchInputHandler.h"
#include "PerformanceMonitor.h"
#include "SettingsMenuHandler.h"
#include "VertexBatcher.h"
#include <vector>
#include <string>

namespace mobile_ui {

class OverlayRenderer {
public:
    OverlayRenderer();

    void generateVertices(int screenW, int screenH,
                          const Settings& settings,
                          const LayoutManager& layoutMgr,
                          const TouchInputHandler& touchHandler,
                          const PerformanceMonitor& perfMon,
                          SettingsMenuHandler& menuHandler,
                          const SaveManager& saveMgr,
                          int activeBackend,
                          const std::string& toastMessage,
                          float toastTimer,
                          std::vector<UIVertex>& outVerts);

private:
    void renderControls(VertexBatcher& vb, int screenW, int screenH,
                        const Settings& settings, const LayoutManager& layoutMgr,
                        const TouchInputHandler& touchHandler, float opacity);

    void renderDpad(VertexBatcher& vb, float dpadX, float dpadY, float scale,
                    float op, const TouchInputHandler& touchHandler);

    void renderButtons(VertexBatcher& vb, const ButtonLayout& bl, float scale,
                       float op, const TouchInputHandler& touchHandler,
                       const Settings& settings);

    void renderPerfHud(VertexBatcher& vb, int screenW, int screenH,
                       const Settings& settings, const PerformanceMonitor& perfMon,
                       float uiScale);

    void renderSpeedrunHud(VertexBatcher& vb, const Settings& settings, float uiScale);

    void renderEditLayoutOverlay(VertexBatcher& vb, int screenW, int screenH,
                                const Settings& settings, const LayoutManager& layoutMgr);

    void renderToast(VertexBatcher& vb, int screenW, int screenH,
                     const std::string& message, float timer, float uiScale);
};

}  // namespace mobile_ui
