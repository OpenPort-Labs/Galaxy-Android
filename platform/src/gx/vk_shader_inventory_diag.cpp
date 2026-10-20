// Inventory diagnostics, kept out of the lifecycle/loader file for size reasons.
//
// The renderer already reports each runtime-created key with its vertex layout
// (LOGI "VkRuntimeKey"); these helpers summarise the prewarmed side so a device
// log alone answers "did prewarm cover the layouts the game actually draws?".

#include "gx/vk_shader_inventory.h"
#include "port/port.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace gpu {

size_t ShaderInventory::distinctVertexLayouts() const {
    std::lock_guard<std::mutex> lock(mMutex);
    std::vector<uint32_t> layouts;
    for (const auto& e : mEntries) {
        if (std::find(layouts.begin(), layouts.end(), e.key.flags) == layouts.end()) {
            layouts.push_back(e.key.flags);
        }
    }
    return layouts.size();
}

size_t ShaderInventory::nearestUidDiff(const ShaderUid& uid, uint32_t* wordsOut, size_t maxWords) const {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mEntries.empty()) return SIZE_MAX;
    const uint32_t* probe = (const uint32_t*)&uid;
    const size_t words = sizeof(ShaderUid) / sizeof(uint32_t);
    size_t best = SIZE_MAX;
    for (const auto& e : mEntries) {
        const uint32_t* cand = (const uint32_t*)&e.uid;
        size_t diff = 0;
        uint32_t local[8];
        size_t localCount = 0;
        for (size_t i = 0; i < words; i++) {
            if (cand[i] == probe[i]) continue;
            diff++;
            if (localCount < sizeof(local) / sizeof(local[0])) local[localCount++] = (uint32_t)i;
        }
        if (diff < best) {
            best = diff;
            if (wordsOut) {
                size_t n = localCount < maxWords ? localCount : maxWords;
                for (size_t i = 0; i < n; i++) wordsOut[i] = local[i];
            }
        }
        if (best == 0) break;
    }
    return best;
}

std::string ShaderInventory::getDiagnostics() const {
    std::lock_guard<std::mutex> lock(mMutex);
    char buf[512];
    snprintf(buf, sizeof(buf),
             "Vulkan Inventory: %zu keys, %zu UIDs, %zu asset records, "
             "prewarmed %zu created/%zu reused, runtime misses %zu uid/%zu key, %zu failures",
             mKeyIndex.size(), mUidIndex.size(), mStats.assetDerivedEntries,
             mStats.realPipelinesCreated, mStats.realPipelinesReused,
             mStats.runtimeShaderUidMisses, mStats.runtimePipelineKeyMisses,
             mStats.pipelineCreationFailures);
    return std::string(buf);
}

}  // namespace gpu
