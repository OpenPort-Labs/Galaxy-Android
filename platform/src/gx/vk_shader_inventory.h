#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <functional>
#include "gl_shadergen.h"
#include "gx/vk/vk_types.h"

namespace gpu {

class VkRenderer;

struct ShaderInventoryStats {
    size_t totalDiscovered = 0;
    size_t spirvCompiled = 0;
    size_t spirvCached = 0;
    size_t pipelinesCreated = 0;
    size_t pipelinesCached = 0;
    size_t failed = 0;
    size_t runtimeDiscoveredMissing = 0;
    size_t inventoryGaps = 0;
};

struct InventoryEntry {
    ShaderUid uid;
    PipelineKey key;
    std::string sourceAsset;
    bool isRuntimeDiscovered = false;
    bool spirvReady = false;
    bool pipelineReady = false;
    bool failed = false;
    std::string failureReason;
};

class ShaderInventory {
public:
    static ShaderInventory& get();

    ShaderInventory();
    ~ShaderInventory() = default;

    // Initialize with paths for inventory metadata and extracted game files
    void init(const std::string& cacheDir, const std::string& dataRoot = "");

    // Populate pre-baked canonical Mario Galaxy material permutations
    void populateCanonicalGalaxyInventory();

    // Scan extracted game data directory (ObjectData, StageData, LayoutData)
    void scanAssetDirectory(const std::string& filesDir);

    // Record a runtime-discovered shader or pipeline key
    void recordRuntimeDiscovery(const ShaderUid& uid, const PipelineKey& key, const char* source = "runtime");

    // Stage A: Compile all discovered UIDs to SPIR-V in parallel
    void compileAllSpirv(int maxWorkers = 4, std::function<void(int done, int total, const char* msg)> progress = nullptr);

    // Stage B: Prewarm graphics pipelines with active Vulkan device and render pass
    void prewarmPipelines(VkRenderer& renderer, std::function<void(int done, int total, const char* msg)> progress = nullptr);

    // Query stats and diagnostics
    ShaderInventoryStats stats() const;
    std::string getDiagnostics() const;

    // Durability: persist and reload inventory metadata
    void saveInventory();
    void loadInventory();

    // Invalidate inventory if version or checksum mismatches
    void invalidateIfStale(uint32_t currentBuildVersion);

private:
    void addEntry(const ShaderUid& uid, const PipelineKey& key, const std::string& source, bool isRuntime);

    mutable std::mutex mMutex;
    std::string mCacheDir;
    std::string mInventoryFilePath;
    std::string mDataRoot;

    std::vector<InventoryEntry> mEntries;
    std::unordered_map<ShaderUid, size_t, ShaderUidHash> mUidIndex;
    std::unordered_map<PipelineKey, size_t, PipelineKeyHash> mKeyIndex;
    std::vector<std::string> mInventoryGapReports;

    ShaderInventoryStats mStats;
    bool mInitialized = false;
    bool mCanonicalPopulated = false;
    bool mSpirvWarmed = false;
    bool mPipelinesWarmed = false;
};

}  // namespace gpu
