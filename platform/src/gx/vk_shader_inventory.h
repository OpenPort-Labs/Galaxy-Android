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
    size_t totalUniqueUids = 0;
    size_t totalUniqueKeys = 0;
    size_t canonicalEntries = 0;
    size_t assetDerivedEntries = 0;
    size_t runtimeDiscoveredEntries = 0;
    size_t runtimeShaderUidMisses = 0;
    size_t runtimePipelineKeyMisses = 0;

    size_t spirvCacheHits = 0;
    size_t spirvCompilationsCompleted = 0;
    size_t spirvFailures = 0;

    size_t realPipelinesReused = 0;
    size_t realPipelinesCreated = 0;
    size_t pipelineCreationFailures = 0;
    size_t placeholderPipelineUses = 0;

    size_t entriesNotYetCompiled = 0;
    size_t entriesNotYetPrewarmed = 0;
    size_t explicitInventoryGaps = 0;

    double timeSpirvPhaseMs = 0.0;
    double timePipelinePhaseMs = 0.0;

    bool isAssetManifestLoaded = false;
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

    // Load binary asset manifest cooked offline or during installation
    bool loadAssetManifest(const std::string& manifestPath);

    // Separate tracking for runtime ShaderUid misses vs PipelineKey misses
    void recordRuntimeShaderUidMiss(const ShaderUid& uid, const PipelineKey& key, const char* source = "runtime_uid_miss");
    void recordRuntimePipelineKeyMiss(const ShaderUid& uid, const PipelineKey& key, const char* source = "runtime_key_miss");

    // Increment placeholder pipeline uses (diagnostics only, never counted as prewarmed)
    void incrementPlaceholderUses();

    // Stage A: Compile all discovered UIDs to SPIR-V in parallel
    void compileAllSpirv(int maxWorkers = 4, std::function<void(int done, int total, const char* msg)> progress = nullptr);

    // Stage B: Prewarm graphics pipelines with active Vulkan device and render pass
    void prewarmPipelines(VkRenderer& renderer, std::function<void(int done, int total, const char* msg)> progress = nullptr);

    // Startup readiness verification (ensures title screen and essential stubs are prewarmed)
    bool verifyStartupReadiness() const;

    // Query stats and diagnostics
    ShaderInventoryStats stats() const;
    std::string getDiagnostics() const;
    const std::string& activeManifestPath() const { return mActiveManifestPath; }
    uint32_t activeManifestBuildVersion() const { return mActiveManifestBuildVersion; }

    // Durability: persist and reload inventory metadata with stable field-by-field serialization
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
    std::string mActiveManifestPath;
    uint32_t mActiveManifestBuildVersion = 0;

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
