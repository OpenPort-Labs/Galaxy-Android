#include "SettingsMenuHandler.h"
#include "PerformanceMonitor.h"
#include "SaveManager.h"

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <algorithm>

extern "C" int port_is_post_processing_enabled(void);

namespace mobile_ui {

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
                snprintf(apiLine, sizeof(apiLine), "Active Engine: Vulkan 1.1 • Native ARM64");

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
