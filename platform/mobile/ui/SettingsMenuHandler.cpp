#include "SettingsMenuHandler.h"
#include "SettingsMenuActions.h"
#include "PerformanceMonitor.h"
#include "SaveManager.h"

#include <android/log.h>
#include <math.h>
#include <algorithm>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MobileUI", __VA_ARGS__)

namespace mobile_ui {

SettingsMenuHandler::SettingsMenuHandler() = default;

void SettingsMenuHandler::openSettings() {
    mSettingsOpen = true;
}

void SettingsMenuHandler::closeSettings(const Settings& settings, const std::string& savePath) {
    mSettingsOpen = false;
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
}

bool SettingsMenuHandler::handleTouch(const AInputEvent* event, int screenW, int screenH,
                                     Settings& settings, SaveManager& saveMgr,
                                     const PerformanceMonitor& perfMon,
                                     std::string& outToastMsg, bool& outEditMode,
                                     bool& outResetLayout, const std::string& savePath) {
    (void)perfMon;
    if (!mSettingsOpen && mDrawerAnim <= 0.05f) return false;

    int32_t actionWithIndex = AMotionEvent_getAction(event);
    int32_t action = actionWithIndex & AMOTION_EVENT_ACTION_MASK;
    int32_t pointerIndex = (actionWithIndex & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    size_t pointerCount = AMotionEvent_getPointerCount(event);

    float baseDpi = (float)screenH / 650.0f;
    float uiScale = baseDpi;
    float w = (float)screenW;
    float h = (float)screenH;

    float drawerW = std::min(w * 0.44f, 390.0f * uiScale);
    float drawerX = (mDrawerAnim - 1.0f) * drawerW;
    float padX = 20.0f * uiScale;
    float contentW = drawerW - padX * 2.0f;
    float contentTop = 106.0f * uiScale;
    float contentBottom = h - 54.0f * uiScale;
    float totalContentH = getTabContentHeight(mSettingsTab, uiScale);
    float maxScroll = std::max(0.0f, totalContentH - (contentBottom - contentTop));

    if (action == AMOTION_EVENT_ACTION_MOVE && mDrawerScrollPointerId >= 0) {
        for (size_t i = 0; i < pointerCount; i++) {
            int32_t pid = AMotionEvent_getPointerId(event, i);
            if (pid == mDrawerScrollPointerId) {
                float py = AMotionEvent_getY(event, i);
                float deltaY = py - mDrawerTouchStartY;
                if (fabsf(deltaY) > 6.0f * uiScale || mDrawerIsScrolling) {
                    mDrawerIsScrolling = true;
                    mDrawerScrollTarget = std::max(0.0f, std::min(mDrawerScrollStartOffset - deltaY, maxScroll));
                }
                return true;
            }
        }
    }

    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        int32_t liftedPid = AMotionEvent_getPointerId(event, pointerIndex);
        if (liftedPid == mDrawerScrollPointerId) {
            if (!mDrawerIsScrolling && action != AMOTION_EVENT_ACTION_CANCEL) {
                float px = AMotionEvent_getX(event, pointerIndex);
                float py = AMotionEvent_getY(event, pointerIndex);
                float tapRelY = py - contentTop + mDrawerScrollY;

                size_t count = 0;
                const MenuRowDef* rows = getTabRows(mSettingsTab, count);
                float curY = 8.0f * uiScale;
                for (size_t i = 0; i < count; i++) {
                    const MenuRowDef& r = rows[i];
                    float rowH = r.heightDp * uiScale;
                    if (tapRelY >= curY && tapRelY < curY + rowH) {
                        float midX = drawerX + padX + contentW * 0.5f;
                        float relX = px - (drawerX + padX);

                        bool closeSettings = false;
                        SettingsMenuActions::dispatch(r.action, px, midX, relX, contentW,
                                                      settings, saveMgr, outToastMsg,
                                                      outEditMode, outResetLayout,
                                                      closeSettings, savePath);
                        if (closeSettings) mSettingsOpen = false;
                        if (!savePath.empty()) settings.save(savePath);
                        break;
                    }

                    curY += rowH + 6.0f * uiScale;
                }
            }
            mDrawerScrollPointerId = -1;
            mDrawerIsScrolling = false;
            return true;
        }
    }

    if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        float px = AMotionEvent_getX(event, pointerIndex);
        float py = AMotionEvent_getY(event, pointerIndex);
        int32_t pid = AMotionEvent_getPointerId(event, pointerIndex);

        if (px > drawerX + drawerW) {
            mSettingsOpen = false;
            if (!savePath.empty()) settings.save(savePath);
            return true;
        }

        float closeBtnX = drawerX + drawerW - 32.0f * uiScale;
        float closeBtnY = 32.0f * uiScale;
        float dx = px - closeBtnX;
        float dy = py - closeBtnY;
        if (dx * dx + dy * dy <= 28.0f * uiScale * 28.0f * uiScale) {
            mSettingsOpen = false;
            if (!savePath.empty()) settings.save(savePath);
            return true;
        }

        if (py >= 52.0f * uiScale && py <= 98.0f * uiScale) {
            float segW = contentW / 4.0f;
            int newTab = (int)((px - (drawerX + padX)) / segW);
            newTab = std::max(0, std::min(3, newTab));
            if (newTab != mSettingsTab) {
                mSettingsTab = newTab;
                mDrawerScrollY = 0.0f;
                mDrawerScrollTarget = 0.0f;
            }
            return true;
        }

        if (py >= contentBottom) {
            mSettingsOpen = false;
            if (!savePath.empty()) settings.save(savePath);
            return true;
        }

        if (py >= contentTop && py < contentBottom) {
            mDrawerScrollPointerId = pid;
            mDrawerTouchStartY = py;
            mDrawerScrollStartOffset = mDrawerScrollY;
            mDrawerIsScrolling = false;
            return true;
        }

        return true;
    }

    return true;
}

}  // namespace mobile_ui
