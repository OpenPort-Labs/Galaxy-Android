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

        if (target.width != desiredW || target.height != desiredH) {
            vkr.destroyTarget(target);
            target = vkr.createTarget(desiredW, desiredH);
            vpMgr.update(screenW, screenH, settings.fullscreen);
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
        int64_t nowNs = port_host_time_ns();

        bool highRefreshSlot = framePacer.shouldPresentSlot(targetFps, port_get_game_speed(), nowNs) && vkr.hasFrame();
        bool uiRedrawNeeded = mobile_ui::UIOverlay::get().needsRedraw() && vkr.hasFrame() &&
                              (!gameBooted || port_is_paused() || mobile_ui::UIOverlay::get().isSettingsOpen());

        if (highRefreshSlot || uiRedrawNeeded) {
            framePacer.sync(app, targetFps, 1.0f);
            vpMgr.update(screenW, screenH, settings.fullscreen);
            vkr.present(target, vpMgr.x(), vpMgr.y(), vpMgr.w(), vpMgr.h());
            mobile_ui::UIOverlay::get().reportFramePresented();
        } else {
            usleep(1000);
        }
    }
}

void ScenePresenter::presentGles(gpu::Renderer& r, gpu::EfbTarget& target, bool& targetReady,
                                int screenW, int screenH, bool gameBooted,
                                mobile_display::ViewportManager& vpMgr,
                                mobile_display::FramePacer& framePacer,
                                EglContext& egl, android_app* app) {
    bool newFrame = r.update();
    const auto& settings = mobile_ui::UIOverlay::get().settings();

    if (newFrame) {
        gPresentedFrames.store(r.frameNumber());

        int desiredW = 0, desiredH = 0;
        mobile_display::ViewportManager::computeTargetSize(
            screenW, screenH, settings.renderScale, settings.batterySaver, settings.fullscreen,
            desiredW, desiredH);

        if (!targetReady || target.width != desiredW || target.height != desiredH) {
            gpu::EfbTarget newTarget = r.createTarget(desiredW, desiredH);
            if (newTarget.fbo != 0) {
                if (targetReady) r.destroyTarget(target);
                target = newTarget;
                targetReady = true;
                vpMgr.update(screenW, screenH, settings.fullscreen);
            }
        }

        int64_t renderStartNs = port_host_time_ns();
        r.render(target, nullptr, nullptr, gpu::HudMode::Inline);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screenW, screenH);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        vpMgr.update(screenW, screenH, settings.fullscreen);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, target.width, target.height,
                          vpMgr.x(), vpMgr.y(), vpMgr.x() + vpMgr.w(), vpMgr.y() + vpMgr.h(),
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);

        mobile_ui::UIOverlay::get().render(screenW, screenH);
        egl.swapBuffers();
        if (gameBooted) port_vi_retrace();

        int64_t renderEndNs = port_host_time_ns();
        port_perf_render(renderEndNs - renderStartNs, 0);
        mobile_ui::UIOverlay::get().reportGpuRenderTime(renderEndNs - renderStartNs);
        mobile_ui::UIOverlay::get().reportFramePresented();

        int targetFps = settings.targetFps;
        if (settings.batterySaver) targetFps = 30;
        framePacer.sync(app, targetFps, port_get_game_speed());
    } else {
        usleep(1000);
    }
}

}  // namespace mobile_render
