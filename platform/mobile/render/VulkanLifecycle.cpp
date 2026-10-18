#include "VulkanLifecycle.h"

#include <android/log.h>
#include <string>

#include "../ui/UIOverlay.h"
#include "gx/gl_renderer.h"
#include "gx/vk_shadergen.h"
#include "port/port.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GalaxyVulkan", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "GalaxyVulkan", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GalaxyVulkan", __VA_ARGS__)

namespace mobile_render {

bool VulkanLifecycle::bringUp(ANativeWindow* window, int& screenW, int& screenH,
                              gpu::VkTarget& target, mobile_display::ViewportManager& vpMgr) {
    if (!window) return false;
    if (!gpu::VkRenderer::get().init(window)) return false;

    mobile_ui::UIOverlay::get().setActiveBackend(1);
    mobile_ui::UIOverlay::get().setGpuRendererName(gpu::VkRenderer::get().deviceName());
    screenW = ANativeWindow_getWidth(window);
    screenH = ANativeWindow_getHeight(window);

    const auto& settings = mobile_ui::UIOverlay::get().settings();
    int targetW = 0, targetH = 0;
    mobile_display::ViewportManager::computeTargetSize(
        screenW, screenH, settings.renderScale, settings.batterySaver, settings.fullscreen,
        targetW, targetH);

    if (target.framebuffer) {
        gpu::VkRenderer::get().destroyTarget(target);
    }
    target = gpu::VkRenderer::get().createTarget(targetW, targetH);
    vpMgr.update(screenW, screenH, settings.fullscreen);

    LOGI("Vulkan 1.0 native renderer initialized (%dx%d), offscreen target: %dx%d",
         screenW, screenH, targetW, targetH);
    return (target.framebuffer != VK_NULL_HANDLE);
}

bool VulkanLifecycle::handleDeviceLoss(ANativeWindow* window, int& screenW, int& screenH,
                                      gpu::VkTarget& target, mobile_display::ViewportManager& vpMgr,
                                      EglContext& egl, bool& vkReady) {
    static int64_t sLastRecreateNs = 0;
    static int sQuickRecreates = 0;
    int64_t nowNs = port_host_time_ns();
    sQuickRecreates = (sLastRecreateNs && nowNs - sLastRecreateNs < 15000000000ll) ? (sQuickRecreates + 1) : 0;
    sLastRecreateNs = nowNs;

    LOGW("Vulkan renderer failed at runtime; recreating Vulkan device (attempt %d)", sQuickRecreates + 1);
    if (target.framebuffer) {
        gpu::VkRenderer::get().destroyTarget(target);
    }
    target = gpu::VkTarget{};
    gpu::VkRenderer::get().shutdown();
    vkReady = false;

    if (sQuickRecreates < 2 && bringUp(window, screenW, screenH, target, vpMgr)) {
        LOGI("Vulkan device recreated; session continues on Vulkan");
        vkReady = true;
        return true;
    }

    LOGE("Vulkan was lost again immediately (or could not be recreated); falling back to OpenGL ES 3.2");
    sQuickRecreates = 0;
    if (egl.init() && egl.createSurface(window, screenW, screenH)) {
        gpu::renderer().init();
        mobile_ui::UIOverlay::get().setActiveBackend(0);
        LOGI("OpenGL ES 3.2 fallback renderer active");
        return true;
    }

    LOGE("OpenGL ES fallback failed too; no rendering backend is available");
    return false;
}

void VulkanLifecycle::initCaches(const std::string& internalPath) {
    gpu::setShaderCachePath((internalPath + "/shaders.bin").c_str());
    gpu::setVkPipelineCachePath((internalPath + "/vk_pipeline_cache.bin").c_str());
    gpu::setVulkanShaderCachePath((internalPath + "/vk_shaders.bin").c_str());
    gpu::prewarmCommonShaders();
}

}  // namespace mobile_render
