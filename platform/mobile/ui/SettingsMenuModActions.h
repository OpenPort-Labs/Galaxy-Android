#pragma once

#include <string>
#include "Settings.h"
#include "SettingsMenuDefs.h"
#include "SaveManager.h"

namespace mobile_ui {

class SettingsMenuModActions {
public:
    // Handles mods, speed, character selection, savestates, cheats, and save slots
    static bool dispatch(MenuActionId action, float px, float midX, float relX, float contentW,
                         Settings& settings, SaveManager& saveMgr,
                         std::string& outToastMsg, bool& outCloseSettings);
};

}  // namespace mobile_ui
