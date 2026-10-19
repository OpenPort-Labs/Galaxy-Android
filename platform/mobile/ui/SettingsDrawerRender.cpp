#include "SettingsMenuHandler.h"
#include "PerformanceMonitor.h"
#include "SaveManager.h"

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <algorithm>

extern "C" int port_is_post_processing_enabled(void);

namespace mobile_ui {

void SettingsMenuHandler::drawSegmentedGroup(VertexBatcher& vb, float x, float y, float grpW, float grpH,
                                             const char* const* labels, int count, int activeIdx, float uiScale) {
    float r = 10.0f * uiScale;
    vb.drawRoundRect(x, y, x + grpW, y + grpH, r, rgba(0.10f, 0.12f, 0.14f, 0.90f));
    vb.drawRoundRect(x, y, x + grpW, y + grpH, r, rgba(0.80f, 0.85f, 0.92f, 0.12f));

    float segW = grpW / (float)count;
    for (int i = 0; i < count; i++) {
        float sx0 = x + i * segW;
        float sx1 = sx0 + segW;
        bool active = (i == activeIdx);
        if (active) {
            vb.drawRoundRect(sx0 + 2.0f, y + 2.0f, sx1 - 2.0f, y + grpH - 2.0f, r - 2.0f, rgba(0.22f, 0.26f, 0.32f, 0.95f));
            vb.drawRoundRect(sx0 + 2.0f, y + 2.0f, sx1 - 2.0f, y + grpH - 2.0f, r - 2.0f, rgba(0.80f, 0.85f, 0.92f, 0.20f));
            vb.drawText(labels[i], (sx0 + sx1) * 0.5f, y + grpH * 0.63f, 0.32f * uiScale, rgba(0.98f, 0.99f, 1.0f, 1.0f), 1);
        } else {
            vb.drawText(labels[i], (sx0 + sx1) * 0.5f, y + grpH * 0.63f, 0.32f * uiScale, rgba(0.60f, 0.64f, 0.70f, 0.85f), 1);
            if (i > 0 && (i - 1) != activeIdx) {
                vb.drawRoundRect(sx0 - 0.5f, y + 6.0f, sx0 + 0.5f, y + grpH - 6.0f, 0.0f, rgba(0.80f, 0.85f, 0.92f, 0.08f));
            }
        }
    }
}

void SettingsMenuHandler::drawSliderRow(VertexBatcher& vb, const MenuRowDef& r, float rowX0, float rowX1,
                                       float drawY, float rowH, float uiScale, const Settings& settings) {
    vb.drawRoundRect(rowX0, drawY + 2.0f * uiScale, rowX1, drawY + rowH - 2.0f * uiScale, 10.0f * uiScale, rgba(0.10f, 0.12f, 0.15f, 0.75f));
    vb.drawRoundRect(rowX0, drawY + 2.0f * uiScale, rowX1, drawY + rowH - 2.0f * uiScale, 10.0f * uiScale, rgba(0.80f, 0.85f, 0.92f, 0.08f));

    vb.drawText(r.title, rowX0 + 12.0f * uiScale, drawY + 18.0f * uiScale, 0.33f * uiScale, rgba(0.94f, 0.95f, 0.97f, 1.0f), 0);

    float curVal = r.minVal;
    char valBuf[32];
    valBuf[0] = '\0';
    switch (r.action) {
    case ACT_CONTROLS_OPACITY:
        curVal = settings.opacity;
        snprintf(valBuf, sizeof(valBuf), "%.0f%%", curVal * 100.0f);
        break;
    case ACT_BUTTON_SIZE:
        curVal = settings.scale;
        snprintf(valBuf, sizeof(valBuf), "%.0f%%", curVal * 100.0f);
        break;
    case ACT_STICK_SENSITIVITY:
        curVal = settings.stickSensitivity;
        snprintf(valBuf, sizeof(valBuf), "%.0f%%", curVal * 100.0f);
        break;
    case ACT_STAR_STICK_SENSITIVITY:
        curVal = settings.starStickSensitivity;
        snprintf(valBuf, sizeof(valBuf), "%.0f%%", curVal * 100.0f);
        break;
    case ACT_TARGET_FPS:
        curVal = (float)settings.targetFps;
        snprintf(valBuf, sizeof(valBuf), "%d FPS", settings.targetFps);
        break;
    case ACT_GAME_SPEED: {
        float sp = (settings.gameSpeed == 2) ? 1.5f : (settings.gameSpeed == 3) ? 2.0f :
                   (settings.gameSpeed == 4) ? 3.0f : (settings.gameSpeed == 5) ? 4.0f : 1.0f;
        curVal = sp;
        snprintf(valBuf, sizeof(valBuf), "%.1fx", sp);
        break;
    }
    default:
        break;
    }

    float badgeW = 66.0f * uiScale;
    float badgeH = 20.0f * uiScale;
    float badgeX = rowX1 - 12.0f * uiScale - badgeW;
    float badgeY = drawY + 8.0f * uiScale;
    vb.drawRoundRect(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH, 6.0f * uiScale, rgba(0.18f, 0.22f, 0.28f, 0.90f));
    vb.drawRoundRect(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH, 6.0f * uiScale, rgba(0.60f, 0.75f, 0.95f, 0.20f));
    vb.drawText(valBuf, badgeX + badgeW * 0.5f, badgeY + badgeH * 0.68f, 0.27f * uiScale, rgba(0.85f, 0.92f, 1.0f, 1.0f), 1);

    float trackPad = 14.0f * uiScale;
    float trackX0 = rowX0 + trackPad;
    float trackX1 = rowX1 - trackPad;
    float trackW = trackX1 - trackX0;
    float trackY = drawY + 36.0f * uiScale;
    float trackH = 5.0f * uiScale;

    float range = (r.maxVal > r.minVal) ? (r.maxVal - r.minVal) : 1.0f;
    float norm = std::max(0.0f, std::min(1.0f, (curVal - r.minVal) / range));
    float thumbX = trackX0 + norm * trackW;

    vb.drawRoundRect(trackX0, trackY, trackX1, trackY + trackH, 2.5f * uiScale, rgba(0.18f, 0.21f, 0.26f, 0.90f));
    if (thumbX > trackX0 + 1.0f) {
        vb.drawRoundRect(trackX0, trackY, thumbX, trackY + trackH, 2.5f * uiScale, rgba(0.55f, 0.78f, 0.98f, 0.95f));
    }

    float thumbR = 8.0f * uiScale;
    vb.drawCircle(thumbX, trackY + trackH * 0.5f, thumbR + 1.5f * uiScale, rgba(0.08f, 0.10f, 0.14f, 0.40f));
    vb.drawCircle(thumbX, trackY + trackH * 0.5f, thumbR, rgba(0.95f, 0.97f, 1.0f, 1.0f));
    vb.drawRing(thumbX, trackY + trackH * 0.5f, thumbR, 0.70f, rgba(0.65f, 0.80f, 1.0f, 0.90f));
}

void SettingsMenuHandler::drawDropdownRow(VertexBatcher& vb, const MenuRowDef& r, float rowX0, float rowX1,
                                         float drawY, float rowH, float uiScale, const Settings& settings,
                                         const SaveManager& saveMgr) {
    vb.drawRoundRect(rowX0, drawY + 2.0f * uiScale, rowX1, drawY + rowH - 2.0f * uiScale, 10.0f * uiScale, rgba(0.10f, 0.12f, 0.15f, 0.75f));
    vb.drawRoundRect(rowX0, drawY + 2.0f * uiScale, rowX1, drawY + rowH - 2.0f * uiScale, 10.0f * uiScale, rgba(0.80f, 0.85f, 0.92f, 0.08f));

    vb.drawText(r.title, rowX0 + 12.0f * uiScale, drawY + 19.0f * uiScale, 0.33f * uiScale, rgba(0.94f, 0.95f, 0.97f, 1.0f), 0);
    if (r.subtitle && r.subtitle[0]) {
        vb.drawText(r.subtitle, rowX0 + 12.0f * uiScale, drawY + 33.0f * uiScale, 0.22f * uiScale, rgba(0.58f, 0.62f, 0.68f, 0.80f), 0);
    }

    int activeIdx = 0;
    switch (r.action) {
    case ACT_JOYSTICK_MODE: activeIdx = settings.dynamicStick ? 0 : 1; break;
    case ACT_RIGHT_STICK_MODE: activeIdx = (settings.rightStickMode == 1) ? 1 : 0; break;
    case ACT_DPAD_SIDE: activeIdx = settings.dpadOnRight ? 1 : 0; break;
    case ACT_GRAPHICS_BACKEND: activeIdx = (settings.graphicsBackend == 0) ? 1 : 0; break;
    case ACT_RENDER_SCALE: activeIdx = std::max(0, std::min(3, settings.renderScale)); break;
    case ACT_DISPLAY_REFRESH:
        activeIdx = (settings.preferredRefreshRate >= 144) ? 3 : (settings.preferredRefreshRate >= 120) ? 2 : (settings.preferredRefreshRate >= 90) ? 1 : 0;
        break;
    case ACT_ORIENTATION: activeIdx = std::max(0, std::min(2, settings.screenOrientationChoice)); break;
    case ACT_PERF_HUD: activeIdx = std::max(0, std::min(2, settings.perfHudMode)); break;
    case ACT_SELECT_SLOT: activeIdx = std::max(0, std::min(3, saveMgr.currentSlot() - 1)); break;
    case ACT_LANGUAGE: activeIdx = std::max(0, std::min(5, settings.languageChoice)); break;
    default: break;
    }

    const char* activeLabel = (activeIdx >= 0 && activeIdx < r.optCount && r.optLabels[activeIdx]) ? r.optLabels[activeIdx] : "";

    float boxW = 150.0f * uiScale;
    float boxH = 30.0f * uiScale;
    float boxX0 = rowX1 - 10.0f * uiScale - boxW;
    float boxY0 = drawY + (rowH - boxH) * 0.5f;
    float boxX1 = boxX0 + boxW;
    float boxY1 = boxY0 + boxH;

    bool isOpen = (mActiveDropdownAction == r.action);
    Color boxBg = isOpen ? rgba(0.20f, 0.24f, 0.32f, 0.95f) : rgba(0.14f, 0.16f, 0.20f, 0.85f);
    Color boxBorder = isOpen ? rgba(0.60f, 0.78f, 1.0f, 0.40f) : rgba(0.80f, 0.85f, 0.92f, 0.16f);

    vb.drawRoundRect(boxX0, boxY0, boxX1, boxY1, 8.0f * uiScale, boxBg);
    vb.drawRoundRect(boxX0, boxY0, boxX1, boxY1, 8.0f * uiScale, boxBorder);

    char trunc[28];
    strncpy(trunc, activeLabel, sizeof(trunc) - 1);
    trunc[sizeof(trunc) - 1] = '\0';
    if (strlen(activeLabel) > 20) {
        trunc[18] = '.'; trunc[19] = '.'; trunc[20] = '\0';
    }
    vb.drawText(trunc, boxX0 + 10.0f * uiScale, boxY0 + boxH * 0.65f, 0.26f * uiScale, rgba(0.92f, 0.95f, 0.98f, 1.0f), 0);

    float chevX = boxX1 - 14.0f * uiScale;
    float chevY = boxY0 + boxH * 0.5f;
    float rot = isOpen ? 3.14159f : 0.0f;
    vb.drawChevron(chevX, chevY, 5.0f * uiScale, rgba(0.75f, 0.82f, 0.92f, 0.85f), rot);
}

void SettingsMenuHandler::drawActiveDropdownOverlay(VertexBatcher& vb, float uiScale) {
    if (mActiveDropdownAction == ACT_NONE || mDropdownCount <= 0) return;

    float itemH = 36.0f * uiScale;
    float totalH = mDropdownCount * itemH + 8.0f * uiScale;
    float popX0 = mDropdownX;
    float popX1 = popX0 + mDropdownW;
    float popY0 = mDropdownY;
    float popY1 = popY0 + totalH;

    vb.drawRoundRect(popX0 - 2.0f, popY0 - 2.0f, popX1 + 2.0f, popY1 + 3.0f, 12.0f * uiScale, rgba(0.0f, 0.0f, 0.0f, 0.55f));
    vb.drawRoundRect(popX0, popY0, popX1, popY1, 10.0f * uiScale, rgba(0.09f, 0.11f, 0.14f, 0.98f));
    vb.drawRoundRect(popX0, popY0, popX1, popY1, 10.0f * uiScale, rgba(0.60f, 0.75f, 0.95f, 0.30f));

    for (int i = 0; i < mDropdownCount; i++) {
        float itY0 = popY0 + 4.0f * uiScale + i * itemH;
        float itY1 = itY0 + itemH;
        bool selected = (i == mDropdownActiveIdx);
        if (selected) {
            vb.drawRoundRect(popX0 + 4.0f * uiScale, itY0, popX1 - 4.0f * uiScale, itY1, 6.0f * uiScale, rgba(0.20f, 0.26f, 0.36f, 0.95f));
            vb.drawCircle(popX0 + 14.0f * uiScale, (itY0 + itY1) * 0.5f, 3.5f * uiScale, rgba(0.55f, 0.80f, 1.0f, 1.0f));
            vb.drawText(mDropdownLabels[i], popX0 + 26.0f * uiScale, (itY0 + itY1) * 0.5f + 4.0f * uiScale, 0.28f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 0);
        } else {
            vb.drawText(mDropdownLabels[i], popX0 + 16.0f * uiScale, (itY0 + itY1) * 0.5f + 4.0f * uiScale, 0.28f * uiScale, rgba(0.70f, 0.75f, 0.82f, 0.90f), 0);
        }
        if (i < mDropdownCount - 1) {
            vb.drawRoundRect(popX0 + 8.0f * uiScale, itY1 - 0.5f, popX1 - 8.0f * uiScale, itY1 + 0.5f, 0.0f, rgba(0.80f, 0.85f, 0.92f, 0.08f));
        }
    }
}

void SettingsMenuHandler::renderDrawer(VertexBatcher& vb, int screenW, int screenH,
                                      const Settings& settings, const SaveManager& saveMgr,
                                      const PerformanceMonitor& perfMon, int activeBackend) {
    if (mDrawerAnim <= 0.001f) return;

    float baseDpi = (float)screenH / 650.0f;
    float uiScale = baseDpi;
    float w = (float)screenW;
    float h = (float)screenH;

    float drawerW = std::min(w * 0.44f, 390.0f * uiScale);
    float drawerX = (mDrawerAnim - 1.0f) * drawerW;
    float cogRot = mDrawerAnim * 1.5707963f;

    vb.drawRoundRect(0, 0, w, h, 0.0f, rgba(0.0f, 0.0f, 0.0f, mDrawerAnim * 0.55f));
    vb.drawRoundRect(drawerX - 60.0f, 0.0f, drawerX + drawerW, h, 20.0f * uiScale, rgba(0.07f, 0.08f, 0.10f, 0.98f));
    vb.drawRoundRect(drawerX + drawerW - 1.0f, 0.0f, drawerX + drawerW, h, 0.0f, rgba(0.80f, 0.85f, 0.92f, 0.15f));

    float padX = 20.0f * uiScale;
    float contentW = drawerW - padX * 2.0f;
    float contentTop = 106.0f * uiScale;
    float contentBottom = h - 54.0f * uiScale;
    float totalContentH = getTabContentHeight(mSettingsTab, uiScale);
    float maxScroll = std::max(0.0f, totalContentH - (contentBottom - contentTop));

    mDrawerScrollY = std::max(0.0f, std::min(mDrawerScrollY, maxScroll));
    float swW = 48.0f * uiScale, swH = 26.0f * uiScale;

    Color themeColor = rgba(0.85f, 0.88f, 0.94f, 1.0f);

    size_t rowCount = 0;
    const MenuRowDef* rows = getTabRows(mSettingsTab, rowCount);
    float curY = 8.0f * uiScale;

    for (size_t i = 0; i < rowCount; i++) {
        const MenuRowDef& r = rows[i];
        float rowH = r.heightDp * uiScale;
        float drawY = contentTop + curY - mDrawerScrollY;
        curY += rowH + 6.0f * uiScale;

        if (drawY + rowH < contentTop - 15.0f || drawY > contentBottom + 15.0f) {
            continue;
        }

        float rowX0 = drawerX + padX;
        float rowX1 = drawerX + drawerW - padX;

        if (r.type == ROW_SECTION) {
            vb.drawRoundRect(rowX0, drawY + 8.0f * uiScale, rowX0 + 3.0f * uiScale, drawY + 24.0f * uiScale, 1.5f * uiScale, themeColor);
            vb.drawText(r.title, rowX0 + 8.0f * uiScale, drawY + 18.0f * uiScale, 0.32f * uiScale, themeColor, 0);
            if (r.subtitle && r.subtitle[0]) {
                vb.drawText(r.subtitle, rowX0 + 8.0f * uiScale, drawY + 30.0f * uiScale, 0.22f * uiScale, rgba(0.55f, 0.60f, 0.68f, 0.80f), 0);
            }
        } else if (r.type == ROW_SWITCH) {
            vb.drawRoundRect(rowX0, drawY + 2.0f, rowX1, drawY + rowH - 2.0f, 10.0f * uiScale, rgba(0.11f, 0.12f, 0.15f, 0.75f));
            vb.drawRoundRect(rowX0, drawY + 2.0f, rowX1, drawY + rowH - 2.0f, 10.0f * uiScale, rgba(0.80f, 0.85f, 0.92f, 0.08f));
            vb.drawText(r.title, rowX0 + 12.0f * uiScale, drawY + 19.0f * uiScale, 0.34f * uiScale, rgba(0.94f, 0.95f, 0.97f, 1.0f), 0);
            if (r.subtitle && r.subtitle[0]) {
                vb.drawText(r.subtitle, rowX0 + 12.0f * uiScale, drawY + 32.0f * uiScale, 0.23f * uiScale, rgba(0.60f, 0.64f, 0.70f, 0.80f), 0);
            }

            bool swOn = false;
            switch (r.action) {
            case ACT_SHOW_CONTROLS: swOn = settings.showControls; break;
            case ACT_TOUCH_CAMERA: swOn = settings.touchCamera; break;
            case ACT_STAR_STICK: swOn = settings.starStick; break;
            case ACT_SHOW_DPAD: swOn = settings.showDpad; break;
            case ACT_GYRO_AIMING: swOn = settings.gyroAiming; break;
            case ACT_RELATIVE_POINTER: swOn = settings.relativePointer; break;
            case ACT_FULL_TOUCH_MODE: swOn = settings.fullTouchMode; break;
            case ACT_HAPTICS: swOn = settings.haptics; break;
            case ACT_BATTERY_SAVER: swOn = settings.batterySaver; break;
            case ACT_VULKAN_POSTPROCESS: swOn = (port_is_post_processing_enabled() != 0); break;
            case ACT_FULLSCREEN: swOn = settings.fullscreen; break;
            case ACT_SPEEDRUN_HUD: swOn = settings.speedrunHud; break;
            case ACT_SHIELD_TEXTURES: swOn = settings.modShieldTextures; break;
            case ACT_SHIELD_CUTSCENES: swOn = settings.modShieldCutscenes; break;
            case ACT_SHIELD_CONTROLS: swOn = settings.modShieldControls; break;
            case ACT_CLASSIC_GAMEPAD: swOn = settings.modClassicGamepadUI; break;
            case ACT_FLY_MODE: swOn = settings.modFly; break;
            case ACT_SKIP_CUTSCENES: swOn = settings.modSkipCutscenes; break;
            case ACT_INFINITE_HEALTH: swOn = settings.modInfiniteHealth; break;
            case ACT_INFINITE_LIVES: swOn = settings.modInfiniteLives; break;
            case ACT_INFINITE_STARBITS: swOn = settings.modInfiniteStarBits; break;
            case ACT_RIIVOLUTION: swOn = settings.modRiivolution; break;
            default: break;
            }
            vb.drawM3Switch(rowX1 - swW - 10.0f * uiScale, drawY + (rowH - swH) * 0.5f, swW, swH, swOn);
        } else if (r.type == ROW_SLIDER) {
            drawSliderRow(vb, r, rowX0, rowX1, drawY, rowH, uiScale, settings);
        } else if (r.type == ROW_DROPDOWN) {
            drawDropdownRow(vb, r, rowX0, rowX1, drawY, rowH, uiScale, settings, saveMgr);
        } else if (r.type == ROW_SEGMENTED) {
            vb.drawText(r.title, rowX0 + 2.0f * uiScale, drawY + 14.0f * uiScale, 0.35f * uiScale, rgba(0.88f, 0.90f, 0.95f, 1.0f), 0);
            drawSegmentedGroup(vb, rowX0, drawY + 22.0f * uiScale, contentW, 32.0f * uiScale, r.optLabels, r.optCount, 0, uiScale);
        } else if (r.type == ROW_DUAL_BTN) {
            float bW = (contentW - 8.0f * uiScale) * 0.5f;
            float btnY = drawY + 4.0f * uiScale;
            float btnH = rowH - 8.0f * uiScale;
            float b0_x0 = rowX0, b0_x1 = b0_x0 + bW;
            float b1_x0 = b0_x1 + 8.0f * uiScale, b1_x1 = rowX1;

            if (r.action == ACT_LAYOUT_BUTTONS) {
                vb.drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.14f, 0.24f, 0.40f, 0.95f));
                vb.drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.35f, 0.65f, 0.98f, 0.60f));
                vb.drawText("Reposition Layout", (b0_x0 + b0_x1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(0.85f, 0.94f, 1.0f, 1.0f), 1);

                vb.drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.35f, 0.15f, 0.18f, 0.95f));
                vb.drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.85f, 0.30f, 0.35f, 0.60f));
                vb.drawText("Reset Defaults", (b1_x0 + b1_x1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(1.0f, 0.85f, 0.85f, 1.0f), 1);
            } else if (r.action == ACT_REPOSITION_STAR_STICK) {
                vb.drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.24f, 0.18f, 0.06f, 0.95f));
                vb.drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(1.0f, 0.85f, 0.20f, 0.60f));
                vb.drawText("Reposition Star Stick", (b0_x0 + b0_x1) * 0.5f, btnY + btnH * 0.62f, 0.32f * uiScale, rgba(1.0f, 0.95f, 0.80f, 1.0f), 1);

                vb.drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.25f, 0.18f, 0.12f, 0.95f));
                vb.drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.85f, 0.65f, 0.30f, 0.50f));
                vb.drawText("Reset Above Buttons", (b1_x0 + b1_x1) * 0.5f, btnY + btnH * 0.62f, 0.32f * uiScale, rgba(0.95f, 0.90f, 0.85f, 1.0f), 1);
            } else if (r.action == ACT_GYRO_INVERT_BUTTONS) {
                Color c0 = settings.gyroInvertX ? rgba(0.043f, 0.341f, 0.816f, 0.95f) : rgba(0.16f, 0.18f, 0.24f, 0.85f);
                Color c1 = settings.gyroInvertY ? rgba(0.043f, 0.341f, 0.816f, 0.95f) : rgba(0.16f, 0.18f, 0.24f, 0.85f);
                vb.drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, c0);
                vb.drawText(settings.gyroInvertX ? "Invert X: ON" : "Invert X: OFF", (b0_x0 + b0_x1) * 0.5f, btnY + btnH * 0.62f, 0.32f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

                vb.drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, c1);
                vb.drawText(settings.gyroInvertY ? "Invert Y: ON" : "Invert Y: OFF", (b1_x0 + b1_x1) * 0.5f, btnY + btnH * 0.62f, 0.32f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
            } else if (r.action == ACT_SAVESTATE_ACTIONS) {
                vb.drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.08f, 0.32f, 0.45f, 0.95f));
                vb.drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.25f, 0.85f, 1.0f, 0.65f));
                vb.drawText("Save State (0ms)", (b0_x0 + b0_x1) * 0.5f, btnY + btnH * 0.62f, 0.32f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

                vb.drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.35f, 0.22f, 0.08f, 0.95f));
                vb.drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(1.0f, 0.80f, 0.25f, 0.65f));
                vb.drawText("Load State (0ms)", (b1_x0 + b1_x1) * 0.5f, btnY + btnH * 0.62f, 0.32f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
            } else if (r.action == ACT_SLOT_ACTIONS) {
                vb.drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.06f, 0.38f, 0.28f, 0.95f));
                vb.drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.25f, 0.85f, 0.65f, 0.65f));
                char sTxt[32];
                if (saveMgr.currentSlot() == 4) snprintf(sTxt, sizeof(sTxt), "Save Quick Slot");
                else snprintf(sTxt, sizeof(sTxt), "Save to Slot %d", saveMgr.currentSlot());
                vb.drawText(sTxt, (b0_x0 + b0_x1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

                vb.drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.12f, 0.28f, 0.55f, 0.95f));
                vb.drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.35f, 0.65f, 0.98f, 0.65f));
                char rTxt[32];
                if (saveMgr.currentSlot() == 4) snprintf(rTxt, sizeof(rTxt), "Reload Quick Slot");
                else snprintf(rTxt, sizeof(rTxt), "Reload Slot %d", saveMgr.currentSlot());
                vb.drawText(rTxt, (b1_x0 + b1_x1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
            } else if (r.action == ACT_SD_BACKUP_ACTIONS) {
                vb.drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.18f, 0.22f, 0.30f, 0.95f));
                vb.drawRoundRect(b0_x0, btnY, b0_x1, btnY + btnH, 12.0f * uiScale, rgba(0.40f, 0.55f, 0.75f, 0.60f));
                vb.drawText("Export to SD", (b0_x0 + b0_x1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(0.90f, 0.94f, 1.0f, 1.0f), 1);

                vb.drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.18f, 0.25f, 0.24f, 0.95f));
                vb.drawRoundRect(b1_x0, btnY, b1_x1, btnY + btnH, 12.0f * uiScale, rgba(0.40f, 0.75f, 0.60f, 0.60f));
                vb.drawText("Import from SD", (b1_x0 + b1_x1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(0.90f, 1.0f, 0.94f, 1.0f), 1);
            }
        } else if (r.type == ROW_SINGLE_BTN) {
            float btnY = drawY + 4.0f * uiScale;
            float btnH = rowH - 8.0f * uiScale;
            if (r.action == ACT_VIEW_LOGS) {
                vb.drawRoundRect(rowX0, btnY, rowX1, btnY + btnH, 12.0f * uiScale, rgba(0.12f, 0.24f, 0.40f, 0.95f));
                vb.drawRoundRect(rowX0, btnY, rowX1, btnY + btnH, 12.0f * uiScale, rgba(0.35f, 0.65f, 0.98f, 0.55f));
                vb.drawText("View System Logs Console", (rowX0 + rowX1) * 0.5f, btnY + btnH * 0.62f, 0.35f * uiScale, rgba(0.88f, 0.95f, 1.0f, 1.0f), 1);
            } else if (r.action == ACT_EXIT_LAUNCHER) {
                vb.drawRoundRect(rowX0, btnY, rowX1, btnY + btnH, 12.0f * uiScale, rgba(0.38f, 0.12f, 0.15f, 0.95f));
                vb.drawRoundRect(rowX0, btnY, rowX1, btnY + btnH, 12.0f * uiScale, rgba(0.85f, 0.25f, 0.30f, 0.60f));
                vb.drawText("Exit to Setup Launcher", (rowX0 + rowX1) * 0.5f, btnY + btnH * 0.62f, 0.35f * uiScale, rgba(1.0f, 0.88f, 0.88f, 1.0f), 1);
            } else if (r.action == ACT_RELOAD_MODS) {
                vb.drawRoundRect(rowX0, btnY, rowX1, btnY + btnH, 12.0f * uiScale, rgba(0.16f, 0.22f, 0.30f, 0.95f));
                vb.drawRoundRect(rowX0, btnY, rowX1, btnY + btnH, 12.0f * uiScale, rgba(0.38f, 0.55f, 0.80f, 0.60f));
                vb.drawText("SCAN & RELOAD MOD DIRECTORY", (rowX0 + rowX1) * 0.5f, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(0.88f, 0.94f, 1.0f, 1.0f), 1);
            }
        } else if (r.type == ROW_QUICK_CARDS) {
            float bW = (contentW - 10.0f * uiScale) * 0.5f;
            float btnY = drawY + 4.0f * uiScale;
            float btnH = rowH - 8.0f * uiScale;
            float sX0 = rowX0, sX1 = sX0 + bW;
            float lX0 = sX1 + 10.0f * uiScale, lX1 = rowX1;

            vb.drawRoundRect(sX0, btnY, sX1, btnY + btnH, 14.0f * uiScale, rgba(0.05f, 0.38f, 0.28f, 0.95f));
            vb.drawRoundRect(sX0, btnY, sX1, btnY + btnH, 14.0f * uiScale, rgba(0.25f, 0.85f, 0.65f, 0.70f));
            vb.drawCircle(sX0 + 18.0f * uiScale, btnY + btnH * 0.5f, 5.0f * uiScale, rgba(0.35f, 0.98f, 0.75f, 1.0f));
            vb.drawText("QUICK SAVE NOW", (sX0 + sX1) * 0.5f + 6.0f * uiScale, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

            vb.drawRoundRect(lX0, btnY, lX1, btnY + btnH, 14.0f * uiScale, rgba(0.10f, 0.26f, 0.52f, 0.95f));
            vb.drawRoundRect(lX0, btnY, lX1, btnY + btnH, 14.0f * uiScale, rgba(0.35f, 0.68f, 0.98f, 0.70f));
            vb.drawCircle(lX0 + 18.0f * uiScale, btnY + btnH * 0.5f, 5.0f * uiScale, rgba(0.45f, 0.82f, 1.0f, 1.0f));
            vb.drawText("QUICK LOAD NOW", (lX0 + lX1) * 0.5f + 6.0f * uiScale, btnY + btnH * 0.62f, 0.33f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
        } else if (r.type == ROW_INFO_CARD) {
            float cardH = rowH - 6.0f * uiScale;
            vb.drawRoundRect(rowX0, drawY + 3.0f * uiScale, rowX1, drawY + 3.0f * uiScale + cardH, 12.0f * uiScale, rgba(0.10f, 0.12f, 0.16f, 0.92f));
            vb.drawRoundRect(rowX0, drawY + 3.0f * uiScale, rowX1, drawY + 3.0f * uiScale + cardH, 12.0f * uiScale, rgba(0.22f, 0.28f, 0.38f, 0.50f));

            if (r.action == ACT_DEVICE_INFO_CARD) {
                char gpuLine[128], resLine[128], apiLine[128];
                snprintf(gpuLine, sizeof(gpuLine), "GPU: %s", perfMon.gpuRendererName().c_str());
                const char* resStr = (settings.renderScale == 0) ? "480p" : (settings.renderScale == 1) ? "720p" : (settings.renderScale == 2) ? "1080p" : "1440p";
                snprintf(resLine, sizeof(resLine), "Display: %dx%d (%s Target • Hor+)", (int)w, (int)h, resStr);
                snprintf(apiLine, sizeof(apiLine), "Active Engine: %s • Native ARM64", (activeBackend == 1) ? "Vulkan 1.0" : "OpenGL ES 3.2 (fallback)");

                vb.drawText(gpuLine, rowX0 + 12.0f * uiScale, drawY + 20.0f * uiScale, 0.28f * uiScale, rgba(0.40f, 0.75f, 1.0f, 0.95f), 0);
                vb.drawText(resLine, rowX0 + 12.0f * uiScale, drawY + 37.0f * uiScale, 0.26f * uiScale, rgba(0.80f, 0.84f, 0.90f, 0.85f), 0);
                vb.drawText(apiLine, rowX0 + 12.0f * uiScale, drawY + 54.0f * uiScale, 0.24f * uiScale, rgba(0.55f, 0.60f, 0.68f, 0.80f), 0);
            } else if (r.action == ACT_STORAGE_INFO_CARD) {
                vb.drawText("NAND SAVE SYSTEM & STORAGE", rowX0 + 12.0f * uiScale, drawY + 18.0f * uiScale, 0.30f * uiScale, rgba(1.0f, 0.78f, 0.25f, 0.95f), 0);
                vb.drawText("Directory: /sdcard/GalaxyData/saves (Dolphin compatible)", rowX0 + 12.0f * uiScale, drawY + 36.0f * uiScale, 0.25f * uiScale, rgba(0.85f, 0.88f, 0.94f, 0.90f), 0);
                vb.drawText("TESTERS: Report bugs & logs on Discord:", rowX0 + 12.0f * uiScale, drawY + 54.0f * uiScale, 0.25f * uiScale, rgba(0.20f, 0.85f, 1.0f, 1.0f), 0);
                vb.drawText("https://discord.gg/H3qdNWYu2", rowX0 + 12.0f * uiScale, drawY + 72.0f * uiScale, 0.26f * uiScale, rgba(0.40f, 0.75f, 1.0f, 1.0f), 0);
            }
        }
    }

    if (maxScroll > 1.0f) {
        float trackH = contentBottom - contentTop;
        float thumbH = std::max(28.0f * uiScale, trackH * (trackH / totalContentH));
        float thumbY = contentTop + (mDrawerScrollY / maxScroll) * (trackH - thumbH);
        vb.drawRoundRect(drawerX + drawerW - 5.0f * uiScale, thumbY, drawerX + drawerW - 2.0f * uiScale, thumbY + thumbH, 1.5f * uiScale, rgba(0.85f, 0.92f, 1.0f, 0.40f));
    }

    drawDrawerChrome(vb, drawerX, drawerW, padX, contentW, contentTop, contentBottom,
                     h, uiScale, cogRot, themeColor, activeBackend);

    drawActiveDropdownOverlay(vb, uiScale);
}

}  // namespace mobile_ui
