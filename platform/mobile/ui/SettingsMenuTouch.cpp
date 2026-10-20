#include "SettingsMenuHandler.h"
#include "SettingsMenuActions.h"
#include "PerformanceMonitor.h"
#include "SaveManager.h"

#include <android/log.h>
#include <math.h>
#include <string.h>
#include <algorithm>

namespace mobile_ui {

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

    // 1. Handle Active Dropdown Overlay Touch
    if (mActiveDropdownAction != ACT_NONE) {
        if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            float px = AMotionEvent_getX(event, pointerIndex);
            float py = AMotionEvent_getY(event, pointerIndex);
            if (px >= mDropdownX && px <= mDropdownX + mDropdownW &&
                py >= mDropdownY && py <= mDropdownY + mDropdownH) {
                float itemH = 36.0f * uiScale;
                int chosen = (int)((py - (mDropdownY + 4.0f * uiScale)) / itemH);
                chosen = std::max(0, std::min(mDropdownCount - 1, chosen));
                SettingsMenuActions::applyDropdownSelection(mActiveDropdownAction, chosen, settings, saveMgr, outToastMsg);
                mActiveDropdownAction = ACT_NONE;
                if (!savePath.empty()) settings.save(savePath);
                return true;
            } else {
                mActiveDropdownAction = ACT_NONE;
                return true;
            }
        }
        return true;
    }

    // 2. Handle Slider Dragging
    if (action == AMOTION_EVENT_ACTION_MOVE && mActiveSliderAction != ACT_NONE) {
        for (size_t i = 0; i < pointerCount; i++) {
            int32_t pid = AMotionEvent_getPointerId(event, i);
            if (pid == mSliderPointerId) {
                float px = AMotionEvent_getX(event, i);
                float trackPad = 14.0f * uiScale;
                float trackX0 = drawerX + padX + trackPad;
                float trackX1 = drawerX + drawerW - padX - trackPad;
                float trackW = trackX1 - trackX0;
                float t = std::max(0.0f, std::min(1.0f, (px - trackX0) / trackW));

                size_t count = 0;
                const MenuRowDef* rows = getTabRows(mSettingsTab, count);
                for (size_t rIdx = 0; rIdx < count; rIdx++) {
                    if (rows[rIdx].action == mActiveSliderAction) {
                        float val = rows[rIdx].minVal + t * (rows[rIdx].maxVal - rows[rIdx].minVal);
                        SettingsMenuActions::applySliderValue(mActiveSliderAction, val, settings, outToastMsg);
                        break;
                    }
                }
                return true;
            }
        }
    }

    // 3. Handle Normal Drawer Scrolling (Move)
    if (action == AMOTION_EVENT_ACTION_MOVE && mDrawerScrollPointerId >= 0) {
        for (size_t i = 0; i < pointerCount; i++) {
            int32_t pid = AMotionEvent_getPointerId(event, i);
            if (pid == mDrawerScrollPointerId) {
                float py = AMotionEvent_getY(event, i);
                float deltaY = py - mDrawerTouchStartY;
                if (fabsf(deltaY) > 6.0f * uiScale || mDrawerIsScrolling) {
                    mDrawerIsScrolling = true;
                    mDrawerScrollTarget = std::max(0.0f, std::min(mDrawerScrollStartOffset - deltaY, maxScroll));
                    mDrawerScrollY = mDrawerScrollTarget;
                }
                return true;
            }
        }
    }

    // 4. Handle Touch Release (Up / Cancel)
    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        int32_t liftedPid = AMotionEvent_getPointerId(event, pointerIndex);
        if (liftedPid == mSliderPointerId) {
            mActiveSliderAction = ACT_NONE;
            mSliderPointerId = -1;
            if (!savePath.empty()) settings.save(savePath);
            return true;
        }

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

                        if (r.type == ROW_SWITCH || r.type == ROW_DUAL_BTN || r.type == ROW_SINGLE_BTN || r.type == ROW_QUICK_CARDS) {
                            bool closeSettings = false;
                            SettingsMenuActions::dispatch(r.action, px, midX, relX, contentW,
                                                          settings, saveMgr, outToastMsg,
                                                          outEditMode, outResetLayout,
                                                          closeSettings, savePath);
                            if (closeSettings) mSettingsOpen = false;
                            if (!savePath.empty()) settings.save(savePath);
                        }
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

    // 5. Handle Touch Down
    if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        float px = AMotionEvent_getX(event, pointerIndex);
        float py = AMotionEvent_getY(event, pointerIndex);
        int32_t pid = AMotionEvent_getPointerId(event, pointerIndex);

        // Outside drawer dismiss
        if (px > drawerX + drawerW) {
            mSettingsOpen = false;
            if (!savePath.empty()) settings.save(savePath);
            return true;
        }

        // Close button
        float closeBtnX = drawerX + drawerW - 32.0f * uiScale;
        float closeBtnY = 32.0f * uiScale;
        float dx = px - closeBtnX;
        float dy = py - closeBtnY;
        if (dx * dx + dy * dy <= 28.0f * uiScale * 28.0f * uiScale) {
            mSettingsOpen = false;
            if (!savePath.empty()) settings.save(savePath);
            return true;
        }

        // Tab selection
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

        // Bottom "Close" action bar
        if (py >= contentBottom) {
            mSettingsOpen = false;
            if (!savePath.empty()) settings.save(savePath);
            return true;
        }

        // Content rows
        if (py >= contentTop && py < contentBottom) {
            float tapRelY = py - contentTop + mDrawerScrollY;
            size_t count = 0;
            const MenuRowDef* rows = getTabRows(mSettingsTab, count);
            float curY = 8.0f * uiScale;

            for (size_t i = 0; i < count; i++) {
                const MenuRowDef& r = rows[i];
                float rowH = r.heightDp * uiScale;

                if (tapRelY >= curY && tapRelY < curY + rowH) {
                    if (r.type == ROW_SLIDER) {
                        mActiveSliderAction = r.action;
                        mSliderPointerId = pid;
                        float trackPad = 14.0f * uiScale;
                        float trackX0 = drawerX + padX + trackPad;
                        float trackX1 = drawerX + drawerW - padX - trackPad;
                        float trackW = trackX1 - trackX0;
                        float t = std::max(0.0f, std::min(1.0f, (px - trackX0) / trackW));
                        float val = r.minVal + t * (r.maxVal - r.minVal);
                        SettingsMenuActions::applySliderValue(r.action, val, settings, outToastMsg);
                        return true;
                    } else if (r.type == ROW_DROPDOWN) {
                        float boxW = 150.0f * uiScale;
                        float boxH = 30.0f * uiScale;
                        float rowDrawY = contentTop + curY - mDrawerScrollY;
                        float boxX0 = drawerX + drawerW - padX - 10.0f * uiScale - boxW;
                        float boxY0 = rowDrawY + (rowH - boxH) * 0.5f;

                        mActiveDropdownAction = r.action;
                        mDropdownCount = r.optCount;
                        for (int k = 0; k < r.optCount; k++) {
                            mDropdownLabels[k] = r.optLabels[k];
                        }

                        int curActive = 0;
                        switch (r.action) {
                        case ACT_JOYSTICK_MODE: curActive = settings.dynamicStick ? 0 : 1; break;
                        case ACT_RIGHT_STICK_MODE: curActive = (settings.rightStickMode == 1) ? 1 : 0; break;
                        case ACT_DPAD_SIDE: curActive = settings.dpadOnRight ? 1 : 0; break;
                        case ACT_RENDER_SCALE: curActive = std::max(0, std::min(3, settings.renderScale)); break;
                        case ACT_DISPLAY_REFRESH:
                            curActive = (settings.preferredRefreshRate >= 144) ? 3 : (settings.preferredRefreshRate >= 120) ? 2 : (settings.preferredRefreshRate >= 90) ? 1 : 0;
                            break;
                        case ACT_ORIENTATION: curActive = std::max(0, std::min(2, settings.screenOrientationChoice)); break;
                        case ACT_PERF_HUD: curActive = std::max(0, std::min(2, settings.perfHudMode)); break;
                        case ACT_SELECT_SLOT: curActive = std::max(0, std::min(3, saveMgr.currentSlot() - 1)); break;
                        case ACT_LANGUAGE: curActive = std::max(0, std::min(5, settings.languageChoice)); break;
                        default: break;
                        }
                        mDropdownActiveIdx = curActive;

                        float itemH = 36.0f * uiScale;
                        float popH = r.optCount * itemH + 8.0f * uiScale;
                        float popW = std::max(boxW + 20.0f * uiScale, 175.0f * uiScale);
                        float popX = std::max(drawerX + padX, drawerX + drawerW - padX - popW);
                        float popY = boxY0 + boxH + 4.0f * uiScale;
                        if (popY + popH > contentBottom) {
                            popY = boxY0 - popH - 4.0f * uiScale;
                        }

                        mDropdownX = popX;
                        mDropdownY = popY;
                        mDropdownW = popW;
                        mDropdownH = popH;
                        return true;
                    }
                }
                curY += rowH + 6.0f * uiScale;
            }

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
