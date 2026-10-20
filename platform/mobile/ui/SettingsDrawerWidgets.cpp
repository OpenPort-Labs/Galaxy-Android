#include "SettingsMenuHandler.h"
#include "PerformanceMonitor.h"
#include "SaveManager.h"

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <algorithm>

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

}  // namespace mobile_ui
