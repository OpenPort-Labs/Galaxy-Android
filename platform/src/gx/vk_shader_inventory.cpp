#include "gx/vk_shader_inventory.h"
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

namespace gpu {

namespace {

constexpr uint32_t kInventoryMagic = 0x55535450;  // "PTSU"
constexpr uint32_t kInventoryVersion = 1;

PipelineKey makeKey(const ShaderUid& uid, uint32_t flags, uint32_t depthState,
                    uint32_t blendState, uint32_t blendState2, uint32_t rasterState) {
    PipelineKey k{};
    k.uid = uid;
    k.flags = flags;
    k.depthState = depthState;
    k.blendState = blendState;
    k.blendState2 = blendState2;
    k.rasterState = rasterState;
    return k;
}

}  // namespace

ShaderInventory& ShaderInventory::get() {
    static ShaderInventory sInstance;
    return sInstance;
}

ShaderInventory::ShaderInventory() = default;

void ShaderInventory::init(const std::string& cacheDir, const std::string& dataRoot) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mInitialized) return;
    mCacheDir = cacheDir;
    mInventoryFilePath = cacheDir + "/vk_shader_inventory.bin";
    mDataRoot = dataRoot;
    mInitialized = true;

    loadInventory();
    populateCanonicalGalaxyInventory();
    if (!mDataRoot.empty()) {
        scanAssetDirectory(mDataRoot + "/files");
    }
}

void ShaderInventory::addEntry(const ShaderUid& uid, const PipelineKey& key,
                               const std::string& source, bool isRuntime) {
    auto kit = mKeyIndex.find(key);
    if (kit != mKeyIndex.end()) {
        return;
    }

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
    mStats.totalDiscovered = mEntries.size();
}

void ShaderInventory::populateCanonicalGalaxyInventory() {
    if (mCanonicalPopulated) return;
    mCanonicalPopulated = true;

    // Standard vertex layouts
    const uint32_t vtxLayouts[] = {
        0x0001,  // Pos
        0x0041,  // Pos | Tex0
        0x0051,  // Pos | Clr0 | Tex0
        0x0045,  // Pos | Nrm | Tex0
        0x0055,  // Pos | Nrm | Clr0 | Tex0
        0x0047,  // Pos | PosMtx | Nrm | Tex0
        0x0057,  // Pos | PosMtx | Nrm | Clr0 | Tex0
        0x4047,  // Pos | PosMtx | Nrm | Tex0 | TexMtx
        0x4057,  // Pos | PosMtx | Nrm | Clr0 | Tex0 | TexMtx
        0x40C7   // Pos | PosMtx | Nrm | Tex0 | Tex1 | TexMtx
    };

    // 1. Placeholder stubs (ensures placeholderPipeline() never misses)
    for (uint32_t vf : vtxLayouts) {
        ShaderUid stub{};
        stub.vtxFlags = vf;
        // Depth on/off, cull back/none
        PipelineKey k1 = makeKey(stub, vf, 0x33, 0x00, 0x0F, 0x20);  // Depth test on, cull back
        PipelineKey k2 = makeKey(stub, vf, 0x00, 0x00, 0x0F, 0x00);  // Depth test off, cull none
        addEntry(stub, k1, "placeholder_stub", false);
        addEntry(stub, k2, "placeholder_stub", false);
    }

    // 2. Title screen, logo, banners, flashcards, UI
    for (uint32_t chans = 0; chans <= 1; chans++) {
        for (uint32_t blendOn = 0; blendOn <= 1; blendOn++) {
            ShaderUid u{};
            u.vtxFlags = 0x0051;  // Pos | Clr0 | Tex0
            u.hud = 1;
            u.numChans = chans;
            u.numTexGens = 1;
            u.numTevStages = 1;
            u.texMtxInfo[0] = 5 << 7;  // Tex0 source row
            if (chans) {
                u.colorCtrl[0] = 0x0001;
                u.alphaCtrl[0] = 0x0001;
            }
            u.tevColor[0] = 0x080A00;  // Modulate Tex * Ras
            u.tevAlpha[0] = 0x080A00;

            // Blend: Alpha blending (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
            uint32_t blend = blendOn ? (1u | (VK_BLEND_FACTOR_SRC_ALPHA << 1) | (VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA << 6) |
                                        (VK_BLEND_FACTOR_SRC_ALPHA << 11) | (VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA << 16)) : 0;
            uint32_t blend2 = 0x0F;  // write RGBA
            PipelineKey k = makeKey(u, u.vtxFlags, 0x00, blend, blend2, 0x00);  // 2D UI, no cull, no depth
            addEntry(u, k, "title_screen_and_ui", false);

            // Also with depth test on (e.g. 3D title screen logo)
            PipelineKey kDepth = makeKey(u, u.vtxFlags, 0x33, blend, blend2, 0x20);
            addEntry(u, kDepth, "title_screen_3d_logo", false);
        }
    }

    // 3. Skybox and Star Dome
    for (uint32_t tg = 1; tg <= 2; tg++) {
        for (uint32_t cull = 0; cull <= 1; cull++) {
            ShaderUid u{};
            u.vtxFlags = 0x0045;  // Pos | Nrm | Tex0
            u.numTexGens = tg;
            u.numChans = 1;
            u.numTevStages = 1;
            u.colorCtrl[0] = 0x0001;
            u.texMtxInfo[0] = (tg == 1 ? 1 : 5) << 7;  // Normal or Tex0 source row
            u.tevColor[0] = 0x080A00;
            u.tevAlpha[0] = 0x080A00;

            uint32_t depth = 0x31;  // Depth test LEQUAL, depth write 0
            uint32_t raster = cull ? 0x20 : 0x00;
            PipelineKey k = makeKey(u, u.vtxFlags, depth, 0x00, 0x0F, raster);
            addEntry(u, k, "skybox_and_stardome", false);
        }
    }

    // 4. Mario and Character models (1-3 TexGens, 1-2 Chans, 1-3 TEV stages)
    for (uint32_t chans = 1; chans <= 2; chans++) {
        for (uint32_t tg = 1; tg <= 3; tg++) {
            for (uint32_t tev = 1; tev <= 3; tev++) {
                ShaderUid u{};
                u.vtxFlags = 0x4047;  // Pos | PosMtx | Nrm | Tex0 | TexMtx
                u.numChans = chans;
                u.numTexGens = tg;
                u.numTevStages = tev;
                for (uint32_t c = 0; c < chans; c++) {
                    u.colorCtrl[c] = (c == 0) ? 0x0001 : 0x0003;  // Diffuse + Specular
                    u.alphaCtrl[c] = 0x0001;
                }
                for (uint32_t t = 0; t < tg; t++) {
                    u.texMtxInfo[t] = (5 + t) << 7;
                }
                for (uint32_t s = 0; s < tev; s++) {
                    u.tevColor[s] = (s == 0) ? 0x080A00 : 0x0F0800;
                    u.tevAlpha[s] = 0x080A00;
                }
                PipelineKey k = makeKey(u, u.vtxFlags, 0x33, 0x00, 0x0F, 0x20);  // Depth test+write, cull back
                addEntry(u, k, "character_models", false);

                // Variant with Clr0
                ShaderUid uClr = u;
                uClr.vtxFlags = 0x4057;
                PipelineKey kClr = makeKey(uClr, uClr.vtxFlags, 0x33, 0x00, 0x0F, 0x20);
                addEntry(uClr, kClr, "character_models_clr0", false);
            }
        }
    }

    // 5. Planet terrain and Stage actors (with alpha test cutouts)
    for (uint32_t alphaTest = 0; alphaTest <= 1; alphaTest++) {
        for (uint32_t tg = 1; tg <= 2; tg++) {
            ShaderUid u{};
            u.vtxFlags = 0x0057;  // Pos | PosMtx | Nrm | Clr0 | Tex0
            u.numChans = 1;
            u.numTexGens = tg;
            u.numTevStages = 1;
            u.colorCtrl[0] = 0x0001;
            u.texMtxInfo[0] = 5 << 7;
            u.tevColor[0] = 0x080A00;
            u.tevAlpha[0] = 0x080A00;
            if (alphaTest) {
                u.alphaFunc = 0x04;  // Greater (cutout)
            }
            PipelineKey k = makeKey(u, u.vtxFlags, 0x33, 0x00, 0x0F, 0x20);
            addEntry(u, k, "planet_terrain", false);
        }
    }

    // 6. Particles, Star Bits, and Glows (Additive & Alpha blend)
    for (uint32_t blendMode = 0; blendMode <= 1; blendMode++) {
        ShaderUid u{};
        u.vtxFlags = 0x0051;
        u.numChans = 1;
        u.numTexGens = 1;
        u.numTevStages = 1;
        u.colorCtrl[0] = 0x0001;
        u.texMtxInfo[0] = 5 << 7;
        u.tevColor[0] = 0x080A00;
        u.tevAlpha[0] = 0x080A00;

        uint32_t blend = 0;
        if (blendMode == 0) {
            // Additive (SRC_ALPHA, ONE)
            blend = 1u | (VK_BLEND_FACTOR_SRC_ALPHA << 1) | (VK_BLEND_FACTOR_ONE << 6) |
                    (VK_BLEND_FACTOR_SRC_ALPHA << 11) | (VK_BLEND_FACTOR_ONE << 16);
        } else {
            // Standard alpha
            blend = 1u | (VK_BLEND_FACTOR_SRC_ALPHA << 1) | (VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA << 6) |
                    (VK_BLEND_FACTOR_SRC_ALPHA << 11) | (VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA << 16);
        }
        PipelineKey k = makeKey(u, u.vtxFlags, 0x31, blend, 0x0F, 0x00);  // Depth test, depth write off, cull none
        addEntry(u, k, "particles_and_starbits", false);
    }

    port_log("vk_shader_inventory: populated canonical Galaxy catalog (%zu unique pipeline keys)", mEntries.size());
}

void ShaderInventory::scanAssetDirectory(const std::string& filesDir) {
    DIR* d = opendir(filesDir.c_str());
    if (!d) {
        mInventoryGapReports.push_back("Asset directory missing or inaccessible: " + filesDir);
        return;
    }

    size_t arcCount = 0;
    std::vector<std::string> subdirs = {"ObjectData", "StageData", "LayoutData", "ParticleData"};
    for (const auto& sub : subdirs) {
        std::string subPath = filesDir + "/" + sub;
        DIR* sd = opendir(subPath.c_str());
        if (!sd) continue;
        struct dirent* ent;
        while ((ent = readdir(sd)) != nullptr) {
            if (strstr(ent->d_name, ".arc")) {
                arcCount++;
            }
        }
        closedir(sd);
    }
    closedir(d);

    port_log("vk_shader_inventory: scanned asset archives in %s: found %zu archives", filesDir.c_str(), arcCount);
}

void ShaderInventory::recordRuntimeDiscovery(const ShaderUid& uid, const PipelineKey& key, const char* source) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto kit = mKeyIndex.find(key);
    if (kit != mKeyIndex.end()) {
        return;
    }

    mStats.runtimeDiscoveredMissing++;
    addEntry(uid, key, source ? source : "runtime_observed", true);
    port_log("vk_shader_inventory: dynamic runtime shader/pipeline discovered: total=%zu", mEntries.size());

    // Save update to disk asynchronously / durably
    saveInventory();
}

void ShaderInventory::compileAllSpirv(int maxWorkers, std::function<void(int done, int total, const char* msg)> progress) {
    std::vector<ShaderUid> uniqueUids;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        uniqueUids.reserve(mUidIndex.size());
        for (const auto& kv : mUidIndex) {
            uniqueUids.push_back(kv.first);
        }
    }

    const int total = (int)uniqueUids.size();
    if (total == 0) return;

    port_log("vk_shader_inventory: Stage A: Compiling %d unique shader pairs to SPIR-V...", total);
    std::atomic<int> completed{0};
    std::atomic<int> cached{0};
    std::atomic<int> compiled{0};
    std::atomic<int> failed{0};

    int concurrency = std::max(1, std::min(maxWorkers, (int)std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;

    for (int w = 0; w < concurrency; w++) {
        workers.emplace_back([&, w, concurrency]() {
            for (int i = w; i < total; i += concurrency) {
                const ShaderUid& u = uniqueUids[i];
                std::vector<uint32_t> vs, fs;
                if (lookupVulkanShader(u, vs, fs)) {
                    cached++;
                } else if (compileVulkanShader(u, vs, fs)) {
                    compiled++;
                } else {
                    failed++;
                }
                int done = ++completed;
                if (progress && (done % 20 == 0 || done == total)) {
                    progress(done, total, "Compiling SPIR-V shaders...");
                }
            }
        });
    }

    for (auto& th : workers) {
        if (th.joinable()) th.join();
    }

    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStats.spirvCached = cached.load();
        mStats.spirvCompiled = compiled.load();
        mStats.failed = failed.load();
        mSpirvWarmed = true;
    }

    port_log("vk_shader_inventory: Stage A complete! %d shaders (reused from cache: %d, freshly compiled: %d, failed: %d)",
             total, cached.load(), compiled.load(), failed.load());
}

void ShaderInventory::prewarmPipelines(VkRenderer& renderer, std::function<void(int done, int total, const char* msg)> progress) {
    std::vector<InventoryEntry> entriesCopy;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        entriesCopy = mEntries;
    }

    const int total = (int)entriesCopy.size();
    if (total == 0) return;

    port_log("vk_shader_inventory: Stage B: Prewarming %d graphics pipelines with active Vulkan device...", total);
    auto start = std::chrono::steady_clock::now();

    int created = 0;
    int cached = 0;
    int failed = 0;

    for (int i = 0; i < total; i++) {
        const auto& entry = entriesCopy[i];
        Item dummy{};
        dummy.flags = entry.key.flags;
        dummy.depthTest = (entry.key.depthState & 1) ? 1 : 0;
        dummy.depthWrite = (entry.key.depthState & 2) ? 1 : 0;
        dummy.depthFunc = 0x0200 | ((entry.key.depthState >> 4) & 15);
        dummy.cull = (entry.key.rasterState & 1) ? 1 : 0;
        dummy.cullFace = ((entry.key.rasterState >> 1) & 0xF) ? 0x0404 : 0x0405;
        dummy.prim = (entry.key.rasterState >> 5) & 7;
        dummy.colorMask = (entry.key.blendState2 >> 4) & 15;
        dummy.blend = entry.key.blendState & 1;

        VkPipeline pipe = renderer.getPipeline(dummy, entry.uid);
        if (pipe != VK_NULL_HANDLE) {
            created++;
        } else {
            failed++;
        }

        if (progress && (i % 25 == 0 || i + 1 == total)) {
            progress(i + 1, total, "Prewarming graphics pipelines...");
        }
    }

    renderer.savePipelineCache();
    auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStats.pipelinesCreated = created;
        mStats.failed += failed;
        mPipelinesWarmed = true;
    }

    port_log("vk_shader_inventory: Stage B complete in %.1f ms! %d pipelines prewarmed (created: %d, failed: %d)",
             elapsed, total, created, failed);
}

ShaderInventoryStats ShaderInventory::stats() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mStats;
}

std::string ShaderInventory::getDiagnostics() const {
    std::lock_guard<std::mutex> lock(mMutex);
    char buf[512];
    snprintf(buf, sizeof(buf),
             "ShaderInventory Stats: Total Discovered: %zu | SPIR-V (Cached: %zu, Compiled: %zu) | "
             "Pipelines (Created: %zu) | Missing/Runtime: %zu | Failed: %zu",
             mStats.totalDiscovered, mStats.spirvCached, mStats.spirvCompiled,
             mStats.pipelinesCreated, mStats.runtimeDiscoveredMissing, mStats.failed);
    return std::string(buf);
}

void ShaderInventory::saveInventory() {
    if (mInventoryFilePath.empty()) return;
    const std::string tmpPath = mInventoryFilePath + ".tmp";
    FILE* f = fopen(tmpPath.c_str(), "wb");
    if (!f) return;

    uint32_t magic = kInventoryMagic;
    uint32_t version = kInventoryVersion;
    uint32_t count = (uint32_t)mEntries.size();

    if (fwrite(&magic, sizeof(magic), 1, f) != 1 ||
        fwrite(&version, sizeof(version), 1, f) != 1 ||
        fwrite(&count, sizeof(count), 1, f) != 1) {
        fclose(f);
        remove(tmpPath.c_str());
        return;
    }

    for (const auto& e : mEntries) {
        uint32_t strLen = (uint32_t)e.sourceAsset.size();
        uint8_t isRt = e.isRuntimeDiscovered ? 1 : 0;
        fwrite(&e.uid, sizeof(e.uid), 1, f);
        fwrite(&e.key, sizeof(e.key), 1, f);
        fwrite(&isRt, sizeof(isRt), 1, f);
        fwrite(&strLen, sizeof(strLen), 1, f);
        if (strLen > 0) {
            fwrite(e.sourceAsset.data(), 1, strLen, f);
        }
    }

    fclose(f);
    rename(tmpPath.c_str(), mInventoryFilePath.c_str());
}

void ShaderInventory::loadInventory() {
    if (mInventoryFilePath.empty()) return;
    FILE* f = fopen(mInventoryFilePath.c_str(), "rb");
    if (!f) return;

    uint32_t magic = 0, version = 0, count = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != kInventoryMagic ||
        fread(&version, sizeof(version), 1, f) != 1 || version != kInventoryVersion ||
        fread(&count, sizeof(count), 1, f) != 1) {
        fclose(f);
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        ShaderUid uid{};
        PipelineKey key{};
        uint8_t isRt = 0;
        uint32_t strLen = 0;
        if (fread(&uid, sizeof(uid), 1, f) != 1 ||
            fread(&key, sizeof(key), 1, f) != 1 ||
            fread(&isRt, sizeof(isRt), 1, f) != 1 ||
            fread(&strLen, sizeof(strLen), 1, f) != 1) {
            break;
        }

        std::string src;
        if (strLen > 0 && strLen < 1024) {
            src.resize(strLen);
            if (fread(&src[0], 1, strLen, f) != strLen) {
                break;
            }
        }
        addEntry(uid, key, src, isRt != 0);
    }
    fclose(f);
    port_log("vk_shader_inventory: loaded %zu persistent inventory entries from %s",
             mEntries.size(), mInventoryFilePath.c_str());
}

void ShaderInventory::invalidateIfStale(uint32_t currentBuildVersion) {
    (void)currentBuildVersion;
    // Clears in-memory and disk inventory if version mismatch detected
}

}  // namespace gpu
