#include "gx/vk_shader_inventory.h"
#include "gx/vk_shader_manifest.h"
#include "gx/vk_shader_catalog.h"
#include "gx/vk_shadergen.h"
#include "gx/vk_renderer.h"
#include "port/port.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <thread>
#include <atomic>
#include <algorithm>

namespace gpu {

ShaderInventory& ShaderInventory::get() {
    static ShaderInventory sInstance;
    return sInstance;
}

ShaderInventory::ShaderInventory() = default;

void ShaderInventory::init(const std::string& cacheDir, const std::string& dataRoot) {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mInitialized) return;
        mCacheDir = cacheDir;
        mInventoryFilePath = cacheDir + "/vk_shader_inventory.bin";
        mDataRoot = dataRoot;
        mInitialized = true;
    }

    invalidateIfStale(kBuildVersion);
    loadInventory();
    populateCanonicalGalaxyInventory();
    if (!dataRoot.empty()) {
        scanAssetDirectory(dataRoot + "/files");
    }
}

void ShaderInventory::addEntry(const ShaderUid& uid, const PipelineKey& key,
                               const std::string& source, bool isRuntime) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mKeyIndex.find(key) != mKeyIndex.end()) return;

    InventoryEntry e{};
    e.uid = uid;
    e.key = key;
    e.sourceAsset = source;
    e.isRuntimeDiscovered = isRuntime;

    size_t idx = mEntries.size();
    mEntries.push_back(std::move(e));
    mKeyIndex[key] = idx;
    if (mUidIndex.find(uid) == mUidIndex.end()) {
        mUidIndex[uid] = idx;
    }
    mStats.totalUniqueKeys = mKeyIndex.size();
    mStats.totalUniqueUids = mUidIndex.size();
}

void ShaderInventory::populateCanonicalGalaxyInventory() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mCanonicalPopulated) return;
        mCanonicalPopulated = true;
    }
    populateCanonicalGalaxyCatalog(*this);
    port_log("vk_shader_inventory: populated canonical Galaxy catalog (%zu keys)", mEntries.size());
}

bool ShaderInventory::loadAssetManifest(const std::string& manifestPath) {
    std::vector<ManifestStagedEntry> staged;
    uint32_t buildVer = 0;
    if (!parseAssetManifest(manifestPath, staged, buildVer)) {
        return false;
    }

    size_t published = 0;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto& item : staged) {
            addEntry(item.uid, item.key, item.source, false);
            published++;
        }
        mStats.assetDerivedEntries = published;
        mStats.isAssetManifestLoaded = (published > 0);
        mActiveManifestPath = manifestPath;
        mActiveManifestBuildVersion = buildVer;
    }
    port_log("vk_shader_inventory: atomically published %zu records from %s", published, manifestPath.c_str());
    return published > 0;
}

void ShaderInventory::scanAssetDirectory(const std::string& filesDir) {
    auto candidates = getManifestCandidates(mCacheDir, mDataRoot, filesDir);
    for (const auto& cand : candidates) {
        if (!cand.empty() && loadAssetManifest(cand)) {
            port_log("vk_shader_inventory: active manifest set to %s", cand.c_str());
            return;
        }
    }

    DIR* d = opendir(filesDir.c_str());
    if (!d) return;

    size_t arcCount = 0;
    std::vector<std::string> subdirs = {"ObjectData", "StageData", "LayoutData", "ParticleData"};
    for (const auto& sub : subdirs) {
        DIR* sd = opendir((filesDir + "/" + sub).c_str());
        if (!sd) continue;
        struct dirent* ent;
        while ((ent = readdir(sd)) != nullptr) {
            if (strstr(ent->d_name, ".arc")) arcCount++;
        }
        closedir(sd);
    }
    closedir(d);

    std::lock_guard<std::mutex> lock(mMutex);
    mStats.explicitInventoryGaps = arcCount;
    mStats.isAssetManifestLoaded = false;
}

void ShaderInventory::recordRuntimeShaderUidMiss(const ShaderUid& uid, const PipelineKey& key, const char* source) {
    addEntry(uid, key, source ? source : "runtime_uid_miss", true);
    std::lock_guard<std::mutex> lock(mMutex);
    mStats.runtimeShaderUidMisses++;
    mStats.runtimeDiscoveredEntries++;
    saveInventory();
}

void ShaderInventory::recordRuntimePipelineKeyMiss(const ShaderUid& uid, const PipelineKey& key, const char* source) {
    addEntry(uid, key, source ? source : "runtime_key_miss", true);
    std::lock_guard<std::mutex> lock(mMutex);
    mStats.runtimePipelineKeyMisses++;
    mStats.runtimeDiscoveredEntries++;
    saveInventory();
}

void ShaderInventory::incrementPlaceholderUses() {
    std::lock_guard<std::mutex> lock(mMutex);
    mStats.placeholderPipelineUses++;
}

void ShaderInventory::compileAllSpirv(int maxWorkers, std::function<void(int done, int total, const char* msg)> progress) {
    std::vector<ShaderUid> uniqueUids;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        uniqueUids.reserve(mUidIndex.size());
        for (const auto& kv : mUidIndex) uniqueUids.push_back(kv.first);
    }

    const int total = (int)uniqueUids.size();
    if (total == 0) return;

    port_log("vk_shader_inventory: Stage A: Compiling %d unique shader pairs to SPIR-V...", total);
    auto start = std::chrono::steady_clock::now();
    std::atomic<int> completed{0}, cached{0}, compiled{0}, failed{0};
    int concurrency = std::max(1, std::min(maxWorkers, (int)std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;

    for (int w = 0; w < concurrency; w++) {
        workers.emplace_back([&, w, concurrency]() {
            for (int i = w; i < total; i += concurrency) {
                const ShaderUid& u = uniqueUids[i];
                std::vector<uint32_t> vs, fs;
                if (lookupVulkanShader(u, vs, fs)) cached++;
                else if (compileVulkanShader(u, vs, fs)) compiled++;
                else failed++;
                int done = ++completed;
                if (progress && (done % 20 == 0 || done == total)) {
                    progress(done, total, "Compiling SPIR-V shaders...");
                }
            }
        });
    }

    for (auto& th : workers) if (th.joinable()) th.join();
    auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStats.spirvCacheHits = cached.load();
        mStats.spirvCompilationsCompleted = compiled.load();
        mStats.spirvFailures = failed.load();
        mStats.timeSpirvPhaseMs = elapsed;
        mSpirvWarmed = true;
        for (auto& e : mEntries) {
            std::vector<uint32_t> vs, fs;
            if (lookupVulkanShader(e.uid, vs, fs)) e.spirvReady = true;
        }
    }
}

void ShaderInventory::prewarmPipelines(VkRenderer& renderer, std::function<void(int done, int total, const char* msg)> progress) {
    std::vector<InventoryEntry> entriesCopy;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        entriesCopy = mEntries;
    }
    const int total = (int)entriesCopy.size();
    if (total == 0) return;

    port_log("vk_shader_inventory: Stage B: Prewarming %d graphics pipelines...", total);
    auto start = std::chrono::steady_clock::now();
    int created = 0, reused = 0, spirvMiss = 0, pipeFail = 0, unsupported = 0;

    for (int i = 0; i < total; i++) {
        PrewarmResult res = renderer.prewarmExactPipeline(entriesCopy[i].key, entriesCopy[i].uid);
        if (res == PrewarmResult::Created) { created++; entriesCopy[i].pipelineReady = true; }
        else if (res == PrewarmResult::Reused) { reused++; entriesCopy[i].pipelineReady = true; }
        else if (res == PrewarmResult::SpirvUnavailable) { spirvMiss++; entriesCopy[i].failed = true; }
        else if (res == PrewarmResult::CreationFailed) { pipeFail++; entriesCopy[i].failed = true; }
        else { unsupported++; entriesCopy[i].failed = true; }
        if (progress && (i % 25 == 0 || i + 1 == total)) {
            progress(i + 1, total, "Prewarming graphics pipelines...");
        }
    }

    renderer.savePipelineCache();
    auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    {
        std::lock_guard<std::mutex> lock(mMutex);
        mEntries = std::move(entriesCopy);
        mStats.realPipelinesCreated = created;
        mStats.realPipelinesReused = reused;
        mStats.pipelineCreationFailures = pipeFail + unsupported;
        mStats.spirvFailures += spirvMiss;
        mStats.timePipelinePhaseMs = elapsed;
        mPipelinesWarmed = true;
    }
    saveInventory();
}

bool ShaderInventory::verifyStartupReadiness() const {
    std::lock_guard<std::mutex> lock(mMutex);
    size_t readyCount = 0;
    for (const auto& e : mEntries) {
        if (e.pipelineReady) readyCount++;
        bool isEssential = (e.sourceAsset == "placeholder_stub" || e.sourceAsset == "title_screen_and_ui");
        if (isEssential && (!e.spirvReady || !e.pipelineReady)) return false;
    }
    return readyCount > 0;
}

ShaderInventoryStats ShaderInventory::stats() const {
    std::lock_guard<std::mutex> lock(mMutex);
    ShaderInventoryStats s = mStats;
    s.totalUniqueKeys = mKeyIndex.size();
    s.totalUniqueUids = mUidIndex.size();
    return s;
}

std::string ShaderInventory::getDiagnostics() const {
    std::lock_guard<std::mutex> lock(mMutex);
    char buf[512];
    snprintf(buf, sizeof(buf), "Vulkan Inventory: %zu keys, %zu UIDs, %zu asset records",
             mKeyIndex.size(), mUidIndex.size(), mStats.assetDerivedEntries);
    return std::string(buf);
}

void ShaderInventory::saveInventory() {
    // Inventory metadata persistence handled via manifest
}

void ShaderInventory::loadInventory() {
    // Inventory reload handled via manifest
}

void ShaderInventory::invalidateIfStale(uint32_t currentBuildVersion) {
    if (isInventoryFileStale(mInventoryFilePath, currentBuildVersion)) {
        remove(mInventoryFilePath.c_str());
        std::lock_guard<std::mutex> lock(mMutex);
        mEntries.clear();
        mUidIndex.clear();
        mKeyIndex.clear();
        mStats = ShaderInventoryStats{};
        mCanonicalPopulated = false;
    }
}

}  // namespace gpu
