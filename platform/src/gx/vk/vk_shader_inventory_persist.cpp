// Runtime-discovered pipeline keys, cached between launches.
//
// The cooked manifest can only describe what the asset data knows.  Keys the
// renderer actually built are ground truth, so they are appended to the
// inventory cache and prewarmed on the next launch.  The file uses the manifest
// on-disk format, which already carries a schema/build version and per-record
// CRCs, so a stale cache is rejected instead of misread.

#include "gx/vk_shader_inventory.h"
#include "gx/vk_shader_manifest.h"
#include "port/port.h"

#include <vector>

namespace gpu {

namespace {
constexpr size_t kMaxCachedKeys = 4096;
constexpr size_t kSaveEveryNthNewKey = 32;
}  // namespace

void ShaderInventory::saveInventory() {
    std::vector<ManifestStagedEntry> staged;
    size_t discovered = 0;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mInventoryFilePath.empty()) return;
        if (mEntries.empty()) return;
        if (mStats.runtimeDiscoveredEntries < mSavedRuntimeEntries + kSaveEveryNthNewKey) return;
        staged.reserve(mEntries.size());
        for (const auto& e : mEntries) {
            if (!e.isRuntimeDiscovered) continue;
            if (staged.size() >= kMaxCachedKeys) break;
            ManifestStagedEntry s{};
            s.uid = e.uid;
            s.key = e.key;
            s.source = e.sourceAsset;
            staged.push_back(std::move(s));
            discovered++;
        }
        mSavedRuntimeEntries = mStats.runtimeDiscoveredEntries;
    }
    if (staged.empty()) return;

    const std::string path = mInventoryFilePath;
    if (!writeAssetManifest(path, staged, kBuildVersion)) {
        LOGW("vk_shader_inventory: could not cache %zu runtime keys to %s", staged.size(), path.c_str());
        return;
    }
    port_log("vk_shader_inventory: cached %zu runtime keys for next launch", staged.size());
}

void ShaderInventory::loadInventory() {
    std::vector<ManifestStagedEntry> staged;
    uint32_t build = 0;
    if (mInventoryFilePath.empty()) return;
    if (!parseAssetManifest(mInventoryFilePath, staged, build) || staged.empty()) return;

    std::lock_guard<std::mutex> lock(mMutex);
    size_t published = 0;
    for (const auto& item : staged) {
        if (mKeyIndex.find(item.key) != mKeyIndex.end()) continue;
        addEntryLocked(item.uid, item.key, item.source, true);
        published++;
    }
    mSavedRuntimeEntries = mStats.runtimeDiscoveredEntries;
    port_log("vk_shader_inventory: restored %zu runtime keys from %s", published, mInventoryFilePath.c_str());
}

}  // namespace gpu
