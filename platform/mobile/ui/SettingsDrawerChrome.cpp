#include "SettingsMenuHandler.h"

namespace mobile_ui {

void SettingsMenuHandler::drawDrawerChrome(
    VertexBatcher& vb, float drawerX, float drawerW, float padX,
    float contentW, float contentTop, float contentBottom, float h,
    float uiScale, float cogRot, Color themeColor, int activeBackend) {

    // Header bar & Cogwheel
    vb.drawRoundRect(drawerX - 60.0f, 0.0f, drawerX + drawerW, contentTop, 0.0f, rgba(0.06f, 0.07f, 0.09f, 0.98f));
    vb.drawCogwheel(drawerX + padX + 14.0f * uiScale, 28.0f * uiScale, 13.0f * uiScale, rgba(0.82f, 0.86f, 0.92f, 0.90f), cogRot);
    vb.drawText("SUPER MARIO GALAXY", drawerX + padX + 36.0f * uiScale, 26.0f * uiScale, 0.38f * uiScale, rgba(0.96f, 0.97f, 0.98f, 1.0f), 0);
    vb.drawText("v0.0.4-gamma TESTER BUILD • PLUS", drawerX + padX + 36.0f * uiScale, 38.0f * uiScale, 0.22f * uiScale, rgba(0.55f, 0.60f, 0.68f, 0.85f), 0);

    // Active Backend tag pill (minimalist outline)
    const char* backendTag = "VULKAN 1.1";
    float tagW = 66.0f * uiScale;
    float tagX = drawerX + drawerW - 56.0f * uiScale - tagW;
    vb.drawRoundRect(tagX, 18.0f * uiScale, tagX + tagW, 36.0f * uiScale, 8.0f * uiScale, rgba(0.12f, 0.14f, 0.17f, 0.90f));
    vb.drawRoundRect(tagX, 18.0f * uiScale, tagX + tagW, 36.0f * uiScale, 8.0f * uiScale, rgba(0.80f, 0.85f, 0.92f, 0.20f));
    vb.drawText(backendTag, tagX + tagW * 0.5f, 29.0f * uiScale, 0.22f * uiScale, rgba(0.82f, 0.86f, 0.92f, 0.90f), 1);

    // Close circular button (clean minimalist button)
    float closeBtnX = drawerX + drawerW - 30.0f * uiScale;
    float closeBtnY = 27.0f * uiScale;
    vb.drawCircle(closeBtnX, closeBtnY, 14.0f * uiScale, rgba(0.14f, 0.16f, 0.19f, 0.90f));
    vb.drawCircle(closeBtnX, closeBtnY, 14.0f * uiScale, rgba(0.80f, 0.85f, 0.92f, 0.15f));
    vb.drawCloseIcon(closeBtnX, closeBtnY, 7.5f * uiScale, rgba(0.78f, 0.82f, 0.88f, 0.90f));

    // Tab segment container (minimalist pill bar)
    float tabBoxY0 = 54.0f * uiScale;
    float tabBoxY1 = 90.0f * uiScale;
    vb.drawRoundRect(drawerX + padX, tabBoxY0, drawerX + drawerW - padX, tabBoxY1, 12.0f * uiScale, rgba(0.10f, 0.12f, 0.14f, 0.90f));
    vb.drawRoundRect(drawerX + padX, tabBoxY0, drawerX + drawerW - padX, tabBoxY1, 12.0f * uiScale, rgba(0.80f, 0.85f, 0.92f, 0.12f));

    float tabSegW = contentW / 4.0f;
    const char* tabNames[4] = {"Controls", "Graphics", "Mods", "Save/Sys"};

    float hX0 = drawerX + padX + mSettingsTab * tabSegW;
    float hX1 = hX0 + tabSegW;
    vb.drawRoundRect(hX0 + 2.0f * uiScale, tabBoxY0 + 2.0f * uiScale, hX1 - 2.0f * uiScale, tabBoxY1 - 2.0f * uiScale, 10.0f * uiScale, rgba(0.20f, 0.23f, 0.28f, 0.95f));
    vb.drawRoundRect(hX0 + 2.0f * uiScale, tabBoxY0 + 2.0f * uiScale, hX1 - 2.0f * uiScale, tabBoxY1 - 2.0f * uiScale, 10.0f * uiScale, rgba(0.80f, 0.85f, 0.92f, 0.25f));

    for (int t = 0; t < 4; t++) {
        float tx0 = drawerX + padX + t * tabSegW;
        float tx1 = tx0 + tabSegW;
        bool active = (t == mSettingsTab);
        Color tCol = active ? rgba(0.98f, 0.99f, 1.0f, 1.0f) : rgba(0.60f, 0.64f, 0.70f, 0.75f);
        vb.drawText(tabNames[t], (tx0 + tx1) * 0.5f, tabBoxY0 + 22.0f * uiScale, 0.28f * uiScale, tCol, 1);
    }

    // Dividers and bottom bar
    vb.drawRoundRect(drawerX + padX, contentTop - 1.0f, drawerX + drawerW - padX, contentTop, 0.5f, rgba(0.80f, 0.85f, 0.92f, 0.10f));
    vb.drawRoundRect(drawerX - 60.0f, contentBottom, drawerX + drawerW, h, 0.0f, rgba(0.06f, 0.07f, 0.09f, 0.98f));
    vb.drawRoundRect(drawerX + padX, contentBottom, drawerX + drawerW - padX, contentBottom + 1.0f, 0.5f, rgba(0.80f, 0.85f, 0.92f, 0.10f));

    // Bottom "CLOSE MENU" action button (minimalist dark capsule)
    float doneY = h - 44.0f * uiScale;
    float doneX0 = drawerX + padX;
    float doneX1 = drawerX + drawerW - padX;
    vb.drawRoundRect(doneX0, doneY, doneX1, doneY + 34.0f * uiScale, 10.0f * uiScale, rgba(0.16f, 0.19f, 0.24f, 0.95f));
    vb.drawRoundRect(doneX0, doneY, doneX1, doneY + 34.0f * uiScale, 10.0f * uiScale, rgba(0.80f, 0.85f, 0.92f, 0.25f));
    vb.drawText("CLOSE MENU", (doneX0 + doneX1) * 0.5f, doneY + 22.0f * uiScale, 0.32f * uiScale, rgba(0.95f, 0.96f, 0.98f, 1.0f), 1);
}

}  // namespace mobile_ui
