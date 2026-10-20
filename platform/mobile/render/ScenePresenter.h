#pragma once

#include <android_native_app_glue.h>
#include "gx/vk_renderer.h"
#include "../display/FramePacer.h"
#include "../display/ViewportManager.h"

namespace mobile_render {

class ScenePresenter {
public:
    // Renders a frame using the native Vulkan 1.0 pipeline, presents to swapchain, and paces
    static void presentVulkan(gpu::VkRenderer& vkr, gpu::VkTarget& target,
                              int screenW, int screenH, bool gameBooted,
                              mobile_display::ViewportManager& vpMgr,
                              mobile_display::FramePacer& framePacer,
                              android_app* app);
};

}  // namespace mobile_render
