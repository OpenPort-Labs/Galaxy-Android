#include "SettingsMenuHandler.h"

namespace mobile_ui {

void SettingsMenuHandler::drawDrawerChrome(
    VertexBatcher& vb, float drawerX, float drawerW, float padX,
    float contentW, float contentTop, float contentBottom, float h,
    float uiScale, float cogRot, Color themeColor, int activeBackend) {

    // Header bar & Cogwheel
    vb.drawRoundRect(drawerX - 60.0f, 0.0f, drawerX + drawerW, contentTop, 0.0f, rgba(0.07f, 0.08f, 0.11f, 1.0f));
    vb.drawCogwheel(drawerX + padX + 14.0f * uiScale, 28.0f * uiScale, 14.0f * uiScale, rgba(0.20f, 0.85f, 1.0f, 1.0f), cogRot);
    vb.drawText("SUPER MARIO GALAXY", drawerX + padX + 36.0f * uiScale, 26.0f * uiScale, 0.40f * uiScale, rgba(0.95f, 0.96f, 0.98f, 1.0f), 0);
    vb.drawText("v0.0.4-beta TESTER BUILD • PLUS", drawerX + padX + 36.0f * uiScale, 38.0f * uiScale, 0.23f * uiScale, rgba(0.20f, 0.85f, 1.0f, 0.90f), 0);

    // Active Backend tag pill
    const char* backendTag = (activeBackend == 1) ? "VULKAN 1.0" : "GLES 3.2";
    float tagW = 68.0f * uiScale;
    float tagX = drawerX + drawerW - 60.0f * uiScale - tagW;
    vb.drawRoundRect(tagX, 18.0f * uiScale, tagX + tagW, 36.0f * uiScale, 9.0f * uiScale, rgba(0.12f, 0.16f, 0.22f, 0.90f));
    vb.drawRoundRect(tagX, 18.0f * uiScale, tagX + tagW, 36.0f * uiScale, 9.0f * uiScale, rgba(0.30f, 0.45f, 0.65f, 0.50f));
    vb.drawText(backendTag, tagX + tagW * 0.5f, 29.0f * uiScale, 0.24f * uiScale, rgba(0.70f, 0.85f, 1.0f, 0.95f), 1);

    // Close circular button
    float closeBtnX = drawerX + drawerW - 32.0f * uiScale;
    float closeBtnY = 27.0f * uiScale;
    vb.drawCircle(closeBtnX, closeBtnY, 15.0f * uiScale, rgba(0.16f, 0.18f, 0.22f, 0.95f));
    vb.drawCloseIcon(closeBtnX, closeBtnY, 8.0f * uiScale, rgba(0.76f, 0.80f, 0.88f, 0.95f));

    // Tab segment container
    float tabBoxY0 = 54.0f * uiScale;
    float tabBoxY1 = 92.0f * uiScale;
    vb.drawRoundRect(drawerX + padX, tabBoxY0, drawerX + drawerW - padX, tabBoxY1, 14.0f * uiScale, rgba(0.12f, 0.14f, 0.18f, 0.95f));
    vb.drawRoundRect(drawerX + padX, tabBoxY0, drawerX + drawerW - padX, tabBoxY1, 14.0f * uiScale, rgba(0.24f, 0.28f, 0.36f, 0.50f));

    float tabSegW = contentW / 4.0f;
    const char* tabNames[4] = {"Controls", "Graphics", "Mods", "Save/Sys"};

    float hX0 = drawerX + padX + mSettingsTab * tabSegW;
    float hX1 = hX0 + tabSegW;
    vb.drawRoundRect(hX0 + 2.0f * uiScale, tabBoxY0 + 2.0f * uiScale, hX1 - 2.0f * uiScale, tabBoxY1 - 2.0f * uiScale, 12.0f * uiScale, rgba(0.18f, 0.26f, 0.42f, 0.95f));
    vb.drawRoundRect(hX0 + 2.0f * uiScale, tabBoxY0 + 2.0f * uiScale, hX1 - 2.0f * uiScale, tabBoxY1 - 2.0f * uiScale, 12.0f * uiScale, themeColor);

    for (int t = 0; t < 4; t++) {
        float tx0 = drawerX + padX + t * tabSegW;
        float tx1 = tx0 + tabSegW;
        bool active = (t == mSettingsTab);
        Color tCol = active ? rgba(1.0f, 1.0f, 1.0f, 1.0f) : rgba(0.65f, 0.70f, 0.78f, 0.75f);
        vb.drawText(tabNames[t], (tx0 + tx1) * 0.5f, tabBoxY0 + 23.0f * uiScale, 0.30f * uiScale, tCol, 1);
    }

    // Dividers and bottom bar
    vb.drawRoundRect(drawerX + padX, contentTop - 1.0f, drawerX + drawerW - padX, contentTop, 0.5f, rgba(0.267f, 0.278f, 0.310f, 0.45f));
    vb.drawRoundRect(drawerX - 60.0f, contentBottom, drawerX + drawerW, h, 0.0f, rgba(0.07f, 0.08f, 0.11f, 1.0f));
    vb.drawRoundRect(drawerX + padX, contentBottom, drawerX + drawerW - padX, contentBottom + 1.0f, 0.5f, rgba(0.267f, 0.278f, 0.310f, 0.45f));

    // Bottom "CLOSE MENU" action button
    float doneY = h - 46.0f * uiScale;
    float doneX0 = drawerX + padX;
    float doneX1 = drawerX + drawerW - padX;
    vb.drawRoundRect(doneX0, doneY, doneX1, doneY + 36.0f * uiScale, 18.0f * uiScale, rgba(0.043f, 0.341f, 0.816f, 0.95f));
    vb.drawRoundRect(doneX0, doneY, doneX1, doneY + 36.0f * uiScale, 18.0f * uiScale, rgba(0.35f, 0.70f, 1.0f, 0.60f));
    vb.drawText("CLOSE MENU", (doneX0 + doneX1) * 0.5f, doneY + 23.0f * uiScale, 0.36f * uiScale, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);
}

}  // namespace mobile_ui
