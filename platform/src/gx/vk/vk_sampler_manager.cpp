#include "gx/vk/vk_common.h"

namespace gpu {

VkSampler VkRenderer::getSampler(uint32_t mode0, uint32_t mode1, uint32_t levels) {
    uint64_t key = ((uint64_t)(mode0 & 0xFFFFFF) << 32) | ((uint64_t)(mode1 & 0xFFFF) << 8) | (levels & 0xFF);
    auto it = mSamplers.find(key);
    if (it != mSamplers.end()) return it->second;

    static const VkSamplerAddressMode wrap[4] = {
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT
    };

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.addressModeU = wrap[mode0 & 3];
    sci.addressModeV = wrap[(mode0 >> 2) & 3];
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.magFilter = ((mode0 >> 4) & 1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

    uint32_t minf = (mode0 >> 5) & 7;
    bool mips = levels > 1;
    switch (minf) {
    case 0:
    case 1:
        sci.minFilter = VK_FILTER_NEAREST;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case 2:
        sci.minFilter = VK_FILTER_NEAREST;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
    case 5:
        sci.minFilter = VK_FILTER_LINEAR;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case 6:
        sci.minFilter = VK_FILTER_LINEAR;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
    default:
        sci.minFilter = VK_FILTER_LINEAR;
        sci.mipmapMode = mips ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    }

    if (!mips) {
        sci.minLod = 0.0f;
        sci.maxLod = 0.0f;
    } else {
        sci.minLod = std::min((float)(levels - 1), (mode1 & 0xFF) / 16.0f);
        sci.maxLod = std::min((float)(levels - 1), ((mode1 >> 8) & 0xFF) / 16.0f);
        if (sci.maxLod < sci.minLod) sci.maxLod = sci.minLod;
    }

    VkSampler s = VK_NULL_HANDLE;
    VkResult res = vkCreateSampler(mDevice, &sci, nullptr, &s);
    if (res != VK_SUCCESS || s == VK_NULL_HANDLE) {
        LOGE("getSampler: vkCreateSampler failed (res=%d)", (int)res);
        return mDummySampler;
    }
    mSamplers[key] = s;
    return s;
}

}  // namespace gpu
