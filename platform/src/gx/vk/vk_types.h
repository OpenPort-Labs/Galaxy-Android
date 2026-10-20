#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cstddef>
#include "gx/gx_record.h"
#include "gx/vk_shadergen.h"
#include "gx/vk/vk_canonical_state.h"
#include "gx/vk/vk_pipeline_key.h"

namespace gpu {

struct VkTarget {
    int width = 0;
    int height = 0;
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkImageLayout colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool hasValidContent = false;
};

struct VkCopyTex {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    int w = 0;
    int h = 0;
    uint64_t frame = 0;
};

struct SamplerDescKey {
    std::array<VkImageView, 8> views;
    std::array<VkSampler, 8> samplers;

    bool operator==(const SamplerDescKey& o) const {
        return views == o.views && samplers == o.samplers;
    }
};

struct SamplerDescKeyHash {
    size_t operator()(const SamplerDescKey& k) const {
        size_t h = 1469598103934665603ull;
        for (int i = 0; i < 8; i++) {
            h = (h ^ (uintptr_t)k.views[i]) * 1099511628211ull;
            h = (h ^ (uintptr_t)k.samplers[i]) * 1099511628211ull;
        }
        return h;
    }
};

struct SamplerSetEntry {
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint64_t lastUse = 0;
};

struct FrameResource {
    VkBuffer vbo = VK_NULL_HANDLE;
    VkDeviceMemory vboMem = VK_NULL_HANDLE;
    void* vboMapped = nullptr;
    size_t vboCap = 0;

    VkBuffer ibo = VK_NULL_HANDLE;
    VkDeviceMemory iboMem = VK_NULL_HANDLE;
    void* iboMapped = nullptr;
    size_t iboCap = 0;

    VkBuffer rowBuf = VK_NULL_HANDLE;
    VkDeviceMemory rowMem = VK_NULL_HANDLE;
    void* rowMapped = nullptr;
    size_t rowCap = 0;

    VkBuffer recBuf = VK_NULL_HANDLE;
    VkDeviceMemory recMem = VK_NULL_HANDLE;
    void* recMapped = nullptr;
    size_t recCap = 0;

    VkBuffer stBuf = VK_NULL_HANDLE;
    VkDeviceMemory stMem = VK_NULL_HANDLE;
    void* stMapped = nullptr;
    size_t stCap = 0;

    VkBuffer vtxRecBuf = VK_NULL_HANDLE;
    VkDeviceMemory vtxRecMem = VK_NULL_HANDLE;
    void* vtxRecMapped = nullptr;
    size_t vtxRecCap = 0;

    VkBuffer eyeUbo = VK_NULL_HANDLE;
    VkDeviceMemory eyeMem = VK_NULL_HANDLE;
    void* eyeMapped = nullptr;
    size_t eyeCap = 0;

    VkDescriptorSet set0 = VK_NULL_HANDLE;

    // UI dynamic vertex buffer
    VkBuffer uiVbo = VK_NULL_HANDLE;
    VkDeviceMemory uiVboMem = VK_NULL_HANDLE;
    void* uiVboMapped = nullptr;
    size_t uiVboCap = 0;
};

struct VkTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t levels = 1;
    uint32_t version = 0;
};

}  // namespace gpu
