#include "gx/vk/vk_common.h"

namespace gpu {

VkDescriptorSet VkRenderer::resolveDrawTextureSet(const Frame& f, const Item& it, DrawTexStats& stats) {
    std::array<VkImageView, 8> views;
    std::array<VkSampler, 8> samplers;
    views.fill(mDummyTexture.view);
    samplers.fill(mDummySampler);

    forEachActiveTexUnit(it.texMask, [&](uint32_t u) {
        uint32_t tr = it.tex[u];
        if (tr == ~0u) return;
        if (tr & 0x80000000u) {
            // 0x80000000 | EFB copy address >> 5 (see Builder CMD_TEX_EFB).
            uint32_t addr = (tr & 0x7FFFFFFF) << 5;
            auto cit = mCopies.find(copyKey(mCopySlot, addr));
            if (cit == mCopies.end() && mCopySlot != 0) {
                cit = mCopies.find(copyKey(0, addr));
                if (cit != mCopies.end()) stats.copyFallbacks++;
            } else if (cit != mCopies.end()) {
                stats.copyHits++;
            }
            if (cit != mCopies.end() && cit->second.view != VK_NULL_HANDLE) {
                views[u] = cit->second.view;
                samplers[u] = getSampler(it.texMode0[u], it.texMode1[u], 1);
            } else {
                // A copy that has not been recorded yet samples the dummy view
                // rather than a stale or uninitialised descriptor.
                stats.copyMisses++;
            }
        } else if (tr < f.textures.size()) {
            const std::shared_ptr<TexImage>& ti = f.textures[tr];
            if (ti && ti->vkView != 0 && ti->vkVersion == ti->version &&
                ti->vkGeneration == sVkDeviceGeneration) {
                views[u] = (VkImageView)ti->vkView;
                samplers[u] = getSampler(it.texMode0[u], it.texMode1[u], ti->levels);
                stats.realTex++;
            } else {
                stats.dummyTex++;
            }
        } else {
            stats.dummyTex++;
        }
    });

    return getSamplerDescriptorSet(views, samplers);
}

}  // namespace gpu
