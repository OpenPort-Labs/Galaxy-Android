#include "ScenePresenter.h"

#include <atomic>
#include <unistd.h>
#include "../ui/UIOverlay.h"
#include "port/port.h"

extern std::atomic<uint64_t> gPresentedFrames;

namespace mobile_render {

void ScenePresenter::presentVulkan(gpu::VkRenderer& vkr, gpu::VkTarget& target,
                                  int screenW, int screenH, bool gameBooted,
                                  mobile_display::ViewportManager& vpMgr,
                                  mobile_display::FramePacer& framePacer,
                                  android_app* app) {
    int64_t updateStartNs = port_host_time_ns();
    bool newFrame = vkr.update();
    port_perf_update(port_host_time_ns() - updateStartNs);

    const auto& settings = mobile_ui::UIOverlay::get().settings();
    if (newFrame) {
        gPresentedFrames.store(vkr.frameNumber());

        int desiredW = 0, desiredH = 0;
        mobile_display::ViewportManager::computeTargetSize(
            screenW, screenH, settings.renderScale, settings.batterySaver, settings.fullscreen,
            desiredW, desiredH);

        if (target.framebuffer == VK_NULL_HANDLE || target.width != desiredW || target.height != desiredH) {
            gpu::VkTarget newTarget = vkr.createTarget(desiredW, desiredH);
            if (newTarget.framebuffer != VK_NULL_HANDLE) {
                if (target.framebuffer != VK_NULL_HANDLE) {
                    vkr.destroyTarget(target);
                }
                target = newTarget;
                vpMgr.update(screenW, screenH, settings.fullscreen);
            } else {
                port_log("ScenePresenter: failed to create Vulkan target (%dx%d), preserving existing", desiredW, desiredH);
            }
        }

        int64_t renderStartNs = port_host_time_ns();
        vkr.render(target, nullptr, nullptr, gpu::HudMode::Inline);
        int64_t renderEndNs = port_host_time_ns();
        port_perf_render(renderEndNs - renderStartNs, 0);
        mobile_ui::UIOverlay::get().reportGpuRenderTime(renderEndNs - renderStartNs);

        vpMgr.update(screenW, screenH, settings.fullscreen);
        int64_t presentStartNs = port_host_time_ns();
        vkr.present(target, vpMgr.x(), vpMgr.y(), vpMgr.w(), vpMgr.h());
        port_perf_present(port_host_time_ns() - presentStartNs);
        mobile_ui::UIOverlay::get().reportFramePresented();
        if (gameBooted) port_vi_retrace();

        int targetFps = settings.targetFps;
        if (settings.batterySaver) targetFps = 30;
        framePacer.sync(app, targetFps, port_get_game_speed());
    } else {
        int targetFps = settings.targetFps;
        if (settings.batterySaver) targetFps = 30;
        bool uiRedrawNeeded = mobile_ui::UIOverlay::get().needsRedraw() && vkr.hasFrame() &&
                              (!gameBooted || port_is_paused() || mobile_ui::UIOverlay::get().isSettingsOpen());

        if (uiRedrawNeeded) {
            framePacer.sync(app, targetFps, 1.0f);
            vpMgr.update(screenW, screenH, settings.fullscreen);
            vkr.present(target, vpMgr.x(), vpMgr.y(), vpMgr.w(), vpMgr.h());
            mobile_ui::UIOverlay::get().reportFramePresented();
        } else {
            usleep(1000);
        }
    }
}

}  // namespace mobile_render
