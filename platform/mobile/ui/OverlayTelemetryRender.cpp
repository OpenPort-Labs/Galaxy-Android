#include "OverlayRenderer.h"
#include <algorithm>
#include <math.h>
#include <stdio.h>

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

void OverlayRenderer::renderPerfHud(VertexBatcher& vb, int screenW, int screenH,
                                    const Settings& settings, const PerformanceMonitor& perfMon,
                                    float uiScale) {
    (void)screenH;
    char buf[144];
    if (settings.perfHudMode == 1) {
        snprintf(buf, sizeof(buf), "%.1f FPS  •  %.1f ms", perfMon.currentFps(), perfMon.avgFrameTimeMs());
    } else {
        if (perfMon.hasHardwareGpuUsage()) {
            snprintf(buf, sizeof(buf), "%.1f FPS  •  %.1f ms  •  CPU %.0f%%  •  GPU %.0f%%  •  RAM %.0f MB",
                     perfMon.currentFps(), perfMon.avgFrameTimeMs(),
                     perfMon.cpuUsagePercent(), perfMon.gpuUsagePercent(), perfMon.ramUsageMB());
        } else if (perfMon.gpuUsagePercent() >= 0.0f) {
            snprintf(buf, sizeof(buf), "%.1f FPS  •  %.1f ms  •  CPU %.0f%%  •  GPU Duty %.0f%%  •  RAM %.0f MB",
                     perfMon.currentFps(), perfMon.avgFrameTimeMs(),
                     perfMon.cpuUsagePercent(), perfMon.gpuUsagePercent(), perfMon.ramUsageMB());
        } else {
            snprintf(buf, sizeof(buf), "%.1f FPS  •  %.1f ms  •  CPU %.0f%%  •  GPU N/A  •  RAM %.0f MB",
                     perfMon.currentFps(), perfMon.avgFrameTimeMs(),
                     perfMon.cpuUsagePercent(), perfMon.ramUsageMB());
        }
    }

    float textScale = 0.28f * uiScale;
    float textW = vb.measureText(buf, textScale);
    float padX = 14.0f * uiScale;
    float hudW = textW + padX * 2.0f;
    float hudH = 26.0f * uiScale;
    float hudX = (float)screenW * 0.5f - hudW * 0.5f;
    float hudY = 14.0f * uiScale;

    vb.drawRoundRect(hudX, hudY, hudX + hudW, hudY + hudH, 13.0f * uiScale, rgba(0.06f, 0.08f, 0.12f, 0.85f));
    vb.drawRoundRect(hudX + 1.0f, hudY + 1.0f, hudX + hudW - 1.0f, hudY + hudH - 1.0f, 12.0f * uiScale, rgba(0.24f, 0.32f, 0.44f, 0.55f));

    Color led = (perfMon.currentFps() >= 55.0f) ? rgba(0.30f, 0.90f, 0.50f, 1.0f) :
                (perfMon.currentFps() >= 30.0f) ? rgba(1.0f, 0.75f, 0.20f, 1.0f) :
                                                  rgba(0.95f, 0.30f, 0.25f, 1.0f);
    vb.drawCircle(hudX + 10.0f * uiScale, hudY + hudH * 0.5f, 3.5f * uiScale, led);
    vb.drawText(buf, hudX + 18.0f * uiScale, hudY + hudH * 0.5f + 4.0f * uiScale, textScale, rgba(0.92f, 0.95f, 1.0f, 0.95f), 0);
}

void OverlayRenderer::renderSpeedrunHud(VertexBatcher& vb, const Settings& settings, float uiScale) {
    PortSpeedrunTelemetry telem{};
    port_get_speedrun_telemetry(&telem);

    char line1[128], line2[128];
    snprintf(line1, sizeof(line1), "POS: [%.1f, %.1f, %.1f]  •  YAW: %.0f°", telem.posX, telem.posY, telem.posZ, telem.angleYaw);
    uint32_t totalSec = telem.stageTimerMs / 1000;
    snprintf(line2, sizeof(line2), "H-SPD: %.2f  •  V-SPD: %.2f  •  HP: %d/%d  •  %02u:%02u.%02u",
             telem.velH, telem.velY, telem.health, telem.maxHealth, totalSec / 60, totalSec % 60, (telem.stageTimerMs % 1000) / 10);

    float sScale = 0.26f * uiScale;
    float sBoxW = std::max(vb.measureText(line1, sScale), vb.measureText(line2, sScale)) + 24.0f * uiScale;
    float sBoxH = 40.0f * uiScale;
    float sBoxX = 16.0f * uiScale;
    float sBoxY = (settings.perfHudMode > 0) ? (46.0f * uiScale) : (14.0f * uiScale);

    vb.drawRoundRect(sBoxX, sBoxY, sBoxX + sBoxW, sBoxY + sBoxH, 10.0f * uiScale, rgba(0.04f, 0.08f, 0.14f, 0.88f));
    vb.drawRoundRect(sBoxX + 1.0f, sBoxY + 1.0f, sBoxX + sBoxW - 1.0f, sBoxY + sBoxH - 1.0f, 9.0f * uiScale, rgba(0.20f, 0.75f, 0.95f, 0.50f));
    vb.drawCircle(sBoxX + 9.0f * uiScale, sBoxY + 14.0f * uiScale, 3.0f * uiScale, rgba(0.20f, 0.95f, 0.65f, 1.0f));
    vb.drawText(line1, sBoxX + 16.0f * uiScale, sBoxY + 16.0f * uiScale, sScale, rgba(0.85f, 0.95f, 1.0f, 0.95f), 0);
    vb.drawText(line2, sBoxX + 16.0f * uiScale, sBoxY + 31.0f * uiScale, sScale, rgba(1.0f, 0.88f, 0.40f, 0.95f), 0);
}

}  // namespace mobile_ui
