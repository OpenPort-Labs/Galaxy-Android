#pragma once

#include <string>
#include "Settings.h"
#include "SettingsMenuDefs.h"
#include "SaveManager.h"

namespace mobile_ui {

class SettingsMenuActions {
public:
    // Dispatches a tap on a settings drawer row action
    static bool dispatch(MenuActionId action, float px, float midX, float relX, float contentW,
                         Settings& settings, SaveManager& saveMgr,
                         std::string& outToastMsg, bool& outEditMode, bool& outResetLayout,
                         bool& outCloseSettings, const std::string& savePath);
};

}  // namespace mobile_ui
