#pragma once

#include <android_native_app_glue.h>
#include "gx/gl_renderer.h"
#include "gx/vk_renderer.h"
#include "../display/FramePacer.h"
#include "../display/ViewportManager.h"
#include "EglContext.h"

namespace mobile_render {

class ScenePresenter {
public:
    // Renders a frame using the native Vulkan 1.0 pipeline, presents to swapchain, and paces
    static void presentVulkan(gpu::VkRenderer& vkr, gpu::VkTarget& target,
                              int screenW, int screenH, bool gameBooted,
                              mobile_display::ViewportManager& vpMgr,
                              mobile_display::FramePacer& framePacer,
                              android_app* app);

    // Renders a frame using the OpenGL ES 3.2 fallback pipeline and blits to window surface
    static void presentGles(gpu::Renderer& r, gpu::EfbTarget& target, bool& targetReady,
                            int screenW, int screenH, bool gameBooted,
                            mobile_display::ViewportManager& vpMgr,
                            mobile_display::FramePacer& framePacer,
                            EglContext& egl, android_app* app);
};

}  // namespace mobile_render
