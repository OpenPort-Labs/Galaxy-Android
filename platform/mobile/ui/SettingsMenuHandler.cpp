#include "SettingsMenuHandler.h"
#include <math.h>

#include "port/port.h"

namespace mobile_ui {

SettingsMenuHandler::SettingsMenuHandler() = default;

void SettingsMenuHandler::openSettings() {
    mSettingsOpen = true;
    mActiveDropdownAction = ACT_NONE;
    mActiveSliderAction = ACT_NONE;
    port_perf_menu_event(1);
}

void SettingsMenuHandler::closeSettings(const Settings& settings, const std::string& savePath) {
    mSettingsOpen = false;
    mActiveDropdownAction = ACT_NONE;
    mActiveSliderAction = ACT_NONE;
    port_perf_menu_event(0);
    if (!savePath.empty()) {
        settings.save(savePath);
    }
}

void SettingsMenuHandler::updateAnimation(float dt) {
    float target = mSettingsOpen ? 1.0f : 0.0f;
    if (fabsf(mDrawerAnim - target) > 0.0005f) {
        float speed = 14.0f;
        mDrawerAnim += (target - mDrawerAnim) * (1.0f - expf(-speed * dt));
        if (fabsf(mDrawerAnim - target) < 0.001f) {
            mDrawerAnim = target;
        }
    }
    if (fabsf(mDrawerScrollY - mDrawerScrollTarget) > 0.01f) {
        float speed = 20.0f;
        mDrawerScrollY += (mDrawerScrollTarget - mDrawerScrollY) * (1.0f - expf(-speed * dt));
        if (fabsf(mDrawerScrollY - mDrawerScrollTarget) < 0.5f) {
            mDrawerScrollY = mDrawerScrollTarget;
        }
    }
}

}  // namespace mobile_ui
