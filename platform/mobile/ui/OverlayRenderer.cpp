#include "OverlayRenderer.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>

extern "C" {
struct PortSpeedrunTelemetry {
    float posX, posY, posZ;
    float velH, velY;
    float angleYaw;
    int health, maxHealth;
    uint32_t stageTimerMs;
};
void port_get_speedrun_telemetry(PortSpeedrunTelemetry* out);
}

namespace mobile_ui {

OverlayRenderer::OverlayRenderer() = default;

void OverlayRenderer::generateVertices(int screenW, int screenH,
                                      const Settings& settings,
                                      const LayoutManager& layoutMgr,
                                      const TouchInputHandler& touchHandler,
                                      const PerformanceMonitor& perfMon,
                                      SettingsMenuHandler& menuHandler,
                                      const SaveManager& saveMgr,
                                      int activeBackend,
                                      const std::string& toastMessage,
                                      float toastTimer,
                                      std::vector<UIVertex>& outVerts) {
    outVerts.reserve(1024);
    VertexBatcher vb(outVerts);

    float baseDpi = (float)screenH / 650.0f;
    float uiScale = baseDpi;
    float w = (float)screenW;

    float drawerAnim = menuHandler.drawerAnim();
    float hudOp = settings.opacity * (1.0f - drawerAnim * 0.75f);

    // 1. On-Screen Controls HUD
    if (settings.showControls && drawerAnim < 0.98f) {
        renderControls(vb, screenW, screenH, settings, layoutMgr, touchHandler, hudOp);
    }

    // 2. Realtime Hardware Telemetry Performance HUD
    if (settings.perfHudMode > 0 && !layoutMgr.isEditMode()) {
        renderPerfHud(vb, screenW, screenH, settings, perfMon, uiScale);
    }

    // 3. Speedrun & Practice Telemetry HUD
    if (settings.speedrunHud && !layoutMgr.isEditMode()) {
        renderSpeedrunHud(vb, settings, uiScale);
    }

    // 4. Settings Cogwheel Tab Dock & Quick Save Button
    if (drawerAnim < 0.95f) {
        float tabW = 56.0f * uiScale;
        float tabH = 48.0f * uiScale;
        float tabY = 22.0f * uiScale;
        float tabPr = touchHandler.btnCfgPressed() ? 0.92f : 1.0f;
        float cogRot = drawerAnim * 1.5707963f;

        vb.drawRoundRect(-10.0f, tabY, tabW * tabPr, tabY + tabH, 24.0f * uiScale, rgba(0.149f, 0.157f, 0.180f, 0.94f));
        vb.drawRoundRect(-10.0f, tabY, tabW * tabPr, tabY + tabH, 24.0f * uiScale, rgba(0.267f, 0.278f, 0.310f, 0.60f));
        vb.drawCogwheel(tabW * 0.50f, tabY + tabH * 0.5f, 15.0f * uiScale, rgba(0.659f, 0.780f, 0.980f, 1.0f), cogRot);

        float qsX = tabW + 16.0f * uiScale;
        float qsW = 58.0f * uiScale;
        vb.drawRoundRect(qsX, tabY + 4.0f * uiScale, qsX + qsW, tabY + tabH - 4.0f * uiScale, 16.0f * uiScale, rgba(0.12f, 0.14f, 0.18f, 0.88f));
        vb.drawRoundRect(qsX, tabY + 4.0f * uiScale, qsX + qsW, tabY + tabH - 4.0f * uiScale, 16.0f * uiScale, rgba(0.25f, 0.45f, 0.78f, 0.65f));
        vb.drawText("SAVE", qsX + qsW * 0.5f, tabY + tabH * 0.5f + 4.0f * uiScale, 0.28f * uiScale, rgba(0.92f, 0.96f, 1.0f, 0.95f), 1);
    }

    // 5. Layout Edit Drag Overlay
    if (layoutMgr.isEditMode()) {
        renderEditLayoutOverlay(vb, screenW, screenH, settings, layoutMgr);
    }

    // 6. Modern Side-Sheet Drawer
    menuHandler.renderDrawer(vb, screenW, screenH, settings, saveMgr, perfMon, activeBackend);

    // 7. Save Notification Toast
    if (toastTimer > 0.0f) {
        renderToast(vb, screenW, screenH, toastMessage, toastTimer, uiScale);
    }
}

void OverlayRenderer::renderControls(VertexBatcher& vb, int screenW, int screenH,
                                    const Settings& settings, const LayoutManager& layoutMgr,
                                    const TouchInputHandler& touchHandler, float opacity) {
    float baseDpi = (float)screenH / 650.0f;
    float scale = baseDpi * settings.scale;
    float w = (float)screenW;
    float h = (float)screenH;

    // Movement Stick
    float defaultStickX = 175.0f * scale + settings.stickOffsetX;
    float defaultStickY = h - 175.0f * scale + settings.stickOffsetY;
    float baseX = touchHandler.isStickActive() ? touchHandler.stickBaseX() : defaultStickX;
    float baseY = touchHandler.isStickActive() ? touchHandler.stickBaseY() : defaultStickY;
    float knobX = touchHandler.isStickActive() ? touchHandler.stickKnobX() : baseX;
    float knobY = touchHandler.isStickActive() ? touchHandler.stickKnobY() : baseY;
    float baseR = 95.0f * scale;
    float knobR = 38.0f * scale;
    bool stickActive = touchHandler.isStickActive();

    vb.drawCircle(baseX, baseY, baseR, rgba(0.05f, 0.07f, 0.11f, opacity * 0.28f));
    vb.drawRing(baseX, baseY, baseR, 0.96f, rgba(0.60f, 0.80f, 1.0f, opacity * 0.45f));

    float tickIn = baseR * 0.84f, tickOut = baseR * 0.95f, tickW = 2.0f * scale;
    Color tickCol = rgba(0.75f, 0.88f, 1.0f, opacity * 0.50f);
    vb.drawLine(baseX, baseY - tickOut, baseX, baseY - tickIn, tickW, tickCol);
    vb.drawLine(baseX, baseY + tickIn, baseX, baseY + tickOut, tickW, tickCol);
    vb.drawLine(baseX - tickOut, baseY, baseX - tickIn, baseY, tickW, tickCol);
    vb.drawLine(baseX + tickIn, baseY, baseX + tickOut, baseY, tickW, tickCol);

    vb.drawRing(baseX, baseY, 13.0f * scale, 0.86f, rgba(0.50f, 0.70f, 1.0f, opacity * 0.35f));
    vb.drawCircle(baseX, baseY, 2.5f * scale, rgba(1.0f, 1.0f, 1.0f, opacity * 0.60f));

    float sDx = knobX - baseX, sDy = knobY - baseY;
    if (sqrtf(sDx * sDx + sDy * sDy) > 5.0f * scale) {
        vb.drawLine(baseX, baseY, knobX, knobY, 3.2f * scale, rgba(0.22f, 0.74f, 0.97f, opacity * 0.65f));
        vb.drawLine(baseX, baseY, knobX, knobY, 1.4f * scale, rgba(1.0f, 1.0f, 1.0f, opacity * 0.85f));
    }

    float kpR = stickActive ? (knobR * 1.04f) : knobR;
    vb.drawCircle(knobX, knobY, kpR, rgba(0.09f, 0.13f, 0.22f, opacity * (stickActive ? 0.85f : 0.65f)));
    Color knobRim = stickActive ? rgba(0.22f, 0.74f, 0.97f, opacity * 0.95f) : rgba(0.60f, 0.80f, 1.0f, opacity * 0.65f);
    vb.drawRing(knobX, knobY, kpR, 0.90f, knobRim);
    vb.drawRing(knobX, knobY, kpR * 0.68f, 0.93f, rgba(1.0f, 1.0f, 1.0f, opacity * 0.35f));
    vb.drawCircle(knobX, knobY, 3.0f * scale, stickActive ? rgba(0.22f, 0.74f, 0.97f, opacity * 0.95f) : rgba(1.0f, 1.0f, 1.0f, opacity * 0.50f));

    // Star Joystick
    if (settings.starStick) {
        StarStickLayout starL = layoutMgr.calculateStarStickLayout(screenW, screenH, settings);
        float sBaseX = touchHandler.isStarStickActive() ? touchHandler.starStickBaseX() : starL.cx;
        float sBaseY = touchHandler.isStarStickActive() ? touchHandler.starStickBaseY() : starL.cy;
        float sKnobX = touchHandler.isStarStickActive() ? touchHandler.starStickKnobX() : sBaseX;
        float sKnobY = touchHandler.isStarStickActive() ? touchHandler.starStickKnobY() : sBaseY;
        float sBaseR = starL.r;
        float sKnobR = 30.0f * scale;
        bool sActive = touchHandler.isStarStickActive();

        vb.drawCircle(sBaseX, sBaseY, sBaseR, rgba(0.12f, 0.10f, 0.05f, opacity * 0.35f));
        vb.drawRing(sBaseX, sBaseY, sBaseR, 0.94f, rgba(1.0f, 0.85f, 0.20f, opacity * (sActive ? 0.75f : 0.40f)));
        vb.drawCircle(sBaseX, sBaseY, 3.0f * scale, rgba(1.0f, 0.90f, 0.30f, opacity * 0.70f));

        float dx = sKnobX - sBaseX, dy = sKnobY - sBaseY;
        if (sqrtf(dx * dx + dy * dy) > 4.0f * scale) {
            vb.drawLine(sBaseX, sBaseY, sKnobX, sKnobY, 2.5f * scale, rgba(1.0f, 0.80f, 0.15f, opacity * 0.70f));
        }

        float spR = sActive ? (sKnobR * 1.05f) : sKnobR;
        vb.drawCircle(sKnobX, sKnobY, spR, rgba(0.20f, 0.16f, 0.06f, opacity * (sActive ? 0.90f : 0.70f)));
        vb.drawRing(sKnobX, sKnobY, spR, 0.88f, rgba(1.0f, 0.90f, 0.30f, opacity * 0.95f));
        vb.drawText("*", sKnobX, sKnobY + 6.0f * scale, 0.45f * scale, rgba(1.0f, 1.0f, 1.0f, opacity * 0.95f), 1);
    }

    // Buttons
    ButtonLayout bl = layoutMgr.calculateButtonLayout(screenW, screenH, settings);
    renderButtons(vb, bl, scale, opacity, touchHandler, settings);

    // D-Pad
    if (settings.showDpad) {
        float baseDpadX = settings.dpadOnRight ? (w - 140.0f * scale) : (130.0f * scale);
        float baseDpadY = settings.dpadOnRight ? (170.0f * scale) : (160.0f * scale);
        renderDpad(vb, baseDpadX + settings.dpadOffsetX, baseDpadY + settings.dpadOffsetY, scale, opacity, touchHandler);
    }
}

void OverlayRenderer::renderButtons(VertexBatcher& vb, const ButtonLayout& bl, float scale,
                                    float op, const TouchInputHandler& touchHandler,
                                    const Settings& settings) {
    (void)settings;
    auto drawBtn = [&](float cx, float cy, float r, const char* label, uint32_t hex, bool pressed, int specialGlyph = 0) {
        float pr = pressed ? (r * 0.93f) : r;
        Color baseBg = pressed ? hexColor(hex, op * 0.88f) : rgba(0.06f, 0.08f, 0.13f, op * 0.36f);
        vb.drawCircle(cx, cy, pr, baseBg);

        Color outerRing = pressed ? rgba(1.0f, 1.0f, 1.0f, op * 0.98f) : hexColor(hex, op * 0.65f);
        vb.drawRing(cx, cy, pr, 0.92f, outerRing);
        vb.drawRing(cx, cy, pr * 0.80f, 0.95f, rgba(1.0f, 1.0f, 1.0f, pressed ? op * 0.85f : op * 0.25f));

        Color glyphCol = pressed ? rgba(1.0f, 1.0f, 1.0f, op) : rgba(0.92f, 0.95f, 1.0f, op * 0.92f);
        if (specialGlyph == 1) {
            vb.drawCyclone(cx, cy - 5.0f * scale, pr * 0.40f, glyphCol, 0.0f);
            vb.drawText("Y • SPIN", cx, cy + pr * 0.58f, 0.22f * scale, glyphCol, 1);
        } else if (specialGlyph == 2) {
            float arm = pr * 0.38f;
            vb.drawLine(cx - arm, cy, cx + arm, cy, 2.4f * scale, glyphCol);
            vb.drawLine(cx, cy - arm, cx, cy + arm, 2.4f * scale, glyphCol);
        } else {
            float tSize = (r > 48.0f * scale) ? (0.64f * scale) : (0.54f * scale);
            vb.drawText(label, cx, cy + 11.0f * scale, tSize, glyphCol, 1);
        }
    };

    drawBtn(bl.bx, bl.by, bl.br, "B", 0xF59E0B, touchHandler.btnBPressed());
    drawBtn(bl.ax, bl.ay, bl.ar, "A", 0x38BDF8, touchHandler.btnAPressed());
    drawBtn(bl.spinX, bl.spinY, bl.spinR, "Y", 0x34D399, touchHandler.btnSpinPressed(), 1);
    drawBtn(bl.zx, bl.zy, bl.zr, "ZL", 0xA78BFA, touchHandler.btnZPressed());
}

void OverlayRenderer::renderDpad(VertexBatcher& vb, float dpadX, float dpadY, float scale,
                                float op, const TouchInputHandler& touchHandler) {
    float dialR = 56.0f * scale, armDist = 36.0f * scale, chevronR = 14.0f * scale;

    vb.drawCircle(dpadX, dpadY, dialR, rgba(0.06f, 0.08f, 0.13f, op * 0.32f));
    vb.drawRing(dpadX, dpadY, dialR, 0.95f, rgba(0.55f, 0.75f, 1.0f, op * 0.42f));

    float dIn = 20.0f * scale, dOut = dialR * 0.88f, diag = 0.7071f;
    Color divCol = rgba(1.0f, 1.0f, 1.0f, op * 0.16f);
    vb.drawLine(dpadX + dIn * diag, dpadY + dIn * diag, dpadX + dOut * diag, dpadY + dOut * diag, 1.4f * scale, divCol);
    vb.drawLine(dpadX - dIn * diag, dpadY + dIn * diag, dpadX - dOut * diag, dpadY + dOut * diag, 1.4f * scale, divCol);
    vb.drawLine(dpadX + dIn * diag, dpadY - dIn * diag, dpadX + dOut * diag, dpadY - dOut * diag, 1.4f * scale, divCol);
    vb.drawLine(dpadX - dIn * diag, dpadY - dIn * diag, dpadX - dOut * diag, dpadY - dOut * diag, 1.4f * scale, divCol);

    auto drawSector = [&](float cx, float cy, float rot, bool pressed) {
        if (pressed) {
            vb.drawCircle(cx, cy, 20.0f * scale, rgba(0.22f, 0.74f, 0.97f, op * 0.80f));
            vb.drawRing(cx, cy, 20.0f * scale, 0.88f, rgba(1.0f, 1.0f, 1.0f, op * 0.95f));
        }
        Color cCol = pressed ? rgba(1.0f, 1.0f, 1.0f, op) : rgba(0.85f, 0.92f, 1.0f, op * 0.75f);
        vb.drawChevron(cx, cy, pressed ? chevronR * 1.15f : chevronR, cCol, rot);
    };

    drawSector(dpadX - armDist, dpadY, 3.14159265f, touchHandler.btnDpadLeft());
    drawSector(dpadX + armDist, dpadY, 0.0f, touchHandler.btnDpadRight());
    drawSector(dpadX, dpadY - armDist, -1.5707963f, touchHandler.btnDpadUp());
    drawSector(dpadX, dpadY + armDist, 1.5707963f, touchHandler.btnDpadDown());

    float hubR = 18.0f * scale;
    vb.drawCircle(dpadX, dpadY, hubR, rgba(0.09f, 0.13f, 0.22f, op * 0.80f));
    vb.drawRing(dpadX, dpadY, hubR, 0.88f, rgba(0.55f, 0.75f, 1.0f, op * 0.55f));
    vb.drawCameraIcon(dpadX, dpadY, 11.0f * scale, rgba(0.85f, 0.93f, 1.0f, op * 0.90f));
}



void OverlayRenderer::renderEditLayoutOverlay(VertexBatcher& vb, int screenW, int screenH,
                                             const Settings& settings, const LayoutManager& layoutMgr) {
    float baseDpi = (float)screenH / 650.0f;
    float scale = baseDpi * settings.scale;
    float w = (float)screenW, h = (float)screenH;

    vb.drawRoundRect(0, 0, w, h, 0.0f, rgba(0.0f, 0.0f, 0.0f, 0.45f));
    vb.drawRoundRect(w * 0.5f - 240.0f, 30.0f, w * 0.5f + 240.0f, 95.0f, 20.0f, rgba(0.12f, 0.14f, 0.18f, 0.96f));
    vb.drawText("DRAG ANY CONTROL TO MOVE IT", w * 0.5f, 55.0f, 0.46f, rgba(0.90f, 0.92f, 0.96f, 1.0f), 1);

    float sBtnX = w * 0.5f - 110.0f;
    vb.drawRoundRect(sBtnX, 65.0f, sBtnX + 100.0f, 90.0f, 12.0f, rgba(0.043f, 0.341f, 0.816f, 0.95f));
    vb.drawText("SAVE", sBtnX + 50.0f, 83.0f, 0.42f, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

    float rBtnX = w * 0.5f + 10.0f;
    vb.drawRoundRect(rBtnX, 65.0f, rBtnX + 100.0f, 90.0f, 12.0f, rgba(0.35f, 0.18f, 0.20f, 0.95f));
    vb.drawText("RESET", rBtnX + 50.0f, 83.0f, 0.42f, rgba(1.0f, 1.0f, 1.0f, 1.0f), 1);

    float defStickX = 175.0f * scale + settings.stickOffsetX;
    float defStickY = h - 175.0f * scale + settings.stickOffsetY;
    vb.drawRing(defStickX, defStickY, 120.0f * scale, 0.96f, rgba(0.40f, 0.65f, 0.98f, 0.85f));
    vb.drawText("JOYSTICK", defStickX, defStickY - 130.0f * scale, 0.45f, rgba(0.40f, 0.65f, 0.98f, 0.95f), 1);

    ButtonLayout editBl = layoutMgr.calculateButtonLayout(screenW, screenH, settings);
    struct EBtn { float cx, cy, r; const char* lbl; } eBtns[4] = {
        {editBl.ax, editBl.ay, editBl.ar, "A"}, {editBl.bx, editBl.by, editBl.br, "B"},
        {editBl.zx, editBl.zy, editBl.zr, "ZL"}, {editBl.spinX, editBl.spinY, editBl.spinR, "SPIN"}
    };
    for (const auto& b : eBtns) {
        vb.drawCircle(b.cx, b.cy, b.r, rgba(0.06f, 0.08f, 0.13f, 0.55f));
        vb.drawRing(b.cx, b.cy, b.r, 0.92f, rgba(0.40f, 0.65f, 0.98f, 0.95f));
        vb.drawText(b.lbl, b.cx, b.cy + 8.0f * scale, 0.42f * scale, rgba(0.92f, 0.95f, 1.0f, 0.95f), 1);
    }
}

void OverlayRenderer::renderToast(VertexBatcher& vb, int screenW, int screenH,
                                  const std::string& message, float timer, float uiScale) {
    (void)screenH;
    float alpha = std::min(1.0f, timer * 2.0f);
    float toastW = 320.0f * uiScale, toastH = 44.0f * uiScale;
    float toastX = ((float)screenW - toastW) * 0.5f, toastY = 56.0f * uiScale;

    vb.drawRoundRect(toastX, toastY, toastX + toastW, toastY + toastH, 22.0f * uiScale, rgba(0.04f, 0.15f, 0.11f, 0.95f * alpha));
    vb.drawRoundRect(toastX, toastY, toastX + toastW, toastY + toastH, 22.0f * uiScale, rgba(0.20f, 0.82f, 0.55f, 0.85f * alpha));
    vb.drawCircle(toastX + 24.0f * uiScale, toastY + toastH * 0.5f, 9.0f * uiScale, rgba(0.18f, 0.85f, 0.55f, alpha));
    vb.drawCircle(toastX + 24.0f * uiScale, toastY + toastH * 0.5f, 4.0f * uiScale, rgba(1.0f, 1.0f, 1.0f, alpha));
    vb.drawText(message.empty() ? "Progress Saved!" : message.c_str(), toastX + 42.0f * uiScale, toastY + toastH * 0.5f + 5.0f * uiScale, 0.35f * uiScale, rgba(0.95f, 1.0f, 0.97f, alpha), 0);
}

}  // namespace mobile_ui
