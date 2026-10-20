#include "gx/vk/vk_common.h"

namespace gpu {

const char* vkDebugValue(const char* envName, const char* propertyName) {
    if (const char* v = debugEnv(envName)) return v;
#if defined(__ANDROID__)
    static char value[PROP_VALUE_MAX];
    if (__system_property_get(propertyName, value) > 0 && value[0]) return value;
#endif
    return nullptr;
}

struct VkPendingDelete {
    uint64_t image = 0;
    uint64_t memory = 0;
    uint64_t view = 0;
    uint64_t generation = 0;
};

static std::mutex sVkDeleteLock;
static std::vector<VkPendingDelete> sVkPendingDeletes;
uint64_t sVkDeviceGeneration = 0;

uint64_t vkDeviceGeneration() {
    return sVkDeviceGeneration;
}

void queueVkTextureDelete(uint64_t image, uint64_t memory, uint64_t view) {
    if (!image && !memory && !view) return;
    std::lock_guard<std::mutex> lock(sVkDeleteLock);
    VkPendingDelete d;
    d.image = image;
    d.memory = memory;
    d.view = view;
    d.generation = sVkDeviceGeneration;
    sVkPendingDeletes.push_back(d);
}

TexImage::~TexImage() {
    if (vkGeneration == vkDeviceGeneration()) {
        queueVkTextureDelete(vkImage, vkMemory, vkView);
    }
}

void VkRenderer::flushDeferredDeletes() {
    if (mDevice == VK_NULL_HANDLE) return;
    std::vector<VkPendingDelete> list;
    {
        std::lock_guard<std::mutex> lock(sVkDeleteLock);
        list.swap(sVkPendingDeletes);
    }
    for (const VkPendingDelete& d : list) {
        if (d.generation != sVkDeviceGeneration) continue;
        if (d.view) vkDestroyImageView(mDevice, (VkImageView)d.view, nullptr);
        if (d.image) vkDestroyImage(mDevice, (VkImage)d.image, nullptr);
        if (d.memory) vkFreeMemory(mDevice, (VkDeviceMemory)d.memory, nullptr);
    }
}

void VkRenderer::trimFrameCaches(uint64_t frame) {
    if (mSamplerDescSets.size() < 1024) return;
    const uint64_t kKeepFrames = 1200;
    for (auto it = mSamplerDescSets.begin(); it != mSamplerDescSets.end();) {
        if (it->second.set != VK_NULL_HANDLE && it->second.lastUse + kKeepFrames < frame) {
            vkFreeDescriptorSets(mDevice, mDescriptorPool, 1, &it->second.set);
            it = mSamplerDescSets.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace gpu
