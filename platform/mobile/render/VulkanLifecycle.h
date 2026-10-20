#pragma once

#include <android/native_window.h>
#include "gx/vk_renderer.h"
#include "../display/ViewportManager.h"

namespace mobile_render {

class VulkanLifecycle {
public:
    // Initializes Vulkan 1.0 renderer on window and allocates offscreen target
    static bool bringUp(ANativeWindow* window, int& screenW, int& screenH,
                        gpu::VkTarget& target, mobile_display::ViewportManager& vpMgr);

    // Handles device loss: attempts to recreate Vulkan device safely
    static bool handleDeviceLoss(ANativeWindow* window, int& screenW, int& screenH,
                                gpu::VkTarget& target, mobile_display::ViewportManager& vpMgr,
                                bool& vkReady);

    // Prepares on-disk caches (SPIR-V shader binary and Vulkan pipeline cache)
    static void initCaches(const std::string& internalPath);
};

}  // namespace mobile_render
