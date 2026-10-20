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
#include <algorithm>

namespace gpu {

namespace {

constexpr uint32_t kInventoryMagic = 0x55535450;      // "PTSU"
constexpr uint32_t kInventoryVersion = 2;
constexpr uint32_t kBuildVersion = 20261019;
constexpr uint32_t kShaderGenVersion = 1;
constexpr size_t kManifestUidWords = 90;

static uint32_t fnv1a32(const void* data, size_t size, uint32_t seed = 2166136261u) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t h = seed;
    for (size_t i = 0; i < size; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

PipelineKey makeCanonicalPipelineKey(const ShaderUid& uid, uint32_t vtxFlags, bool depthTest,
                                     bool depthWrite, VkCompareOp depthOp, bool blendEnable,
                                     VkBlendFactor srcColor, VkBlendFactor dstColor,
                                     VkBlendFactor srcAlpha, VkBlendFactor dstAlpha,
                                     VkCullModeFlags cullMode,
                                     VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) {
    DecodedPipelineKey d{};
    d.uid = uid;
    d.vtxFlags = vtxFlags;
    d.depthTest = depthTest;
    d.depthWrite = depthWrite;
    d.depthCompareOp = toCanonicalDepthFuncFromVk(depthOp);
    d.blendEnable = blendEnable;
    d.srcColorBlendFactor = toCanonicalBlendFactorFromVk(srcColor);
    d.dstColorBlendFactor = toCanonicalBlendFactorFromVk(dstColor);
    d.colorBlendOp = CanonicalBlendOp::Add;
    d.srcAlphaBlendFactor = toCanonicalBlendFactorFromVk(srcAlpha);
    d.dstAlphaBlendFactor = toCanonicalBlendFactorFromVk(dstAlpha);
    d.alphaBlendOp = CanonicalBlendOp::Add;
    d.colorWriteMask = 0x0F;
    d.blendConstantAlpha = 0.0f;
    d.cullMode = toCanonicalCullModeFromVk(cullMode);
    d.frontFace = CanonicalFrontFace::CounterClockwise;
    d.topology = toCanonicalTopologyFromVk(topology);
    return encodePipelineKey(d);
}

}  // namespace

ShaderInventory& ShaderInventory::get() {
    static ShaderInventory sInstance;
    return sInstance;
}

ShaderInventory::ShaderInventory() = default;

void ShaderInventory::init(const std::string& cacheDir, const std::string& dataRoot) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    if (mInitialized) return;
    mCacheDir = cacheDir;
    mInventoryFilePath = cacheDir + "/vk_shader_inventory.bin";
    mDataRoot = dataRoot;
    mInitialized = true;

    // Durability check: invalidate stale cache if build version changed
    invalidateIfStale(kBuildVersion);

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
    mStats.totalUniqueKeys = mKeyIndex.size();
    mStats.totalUniqueUids = mUidIndex.size();
}

void ShaderInventory::populateCanonicalGalaxyInventory() {
    if (mCanonicalPopulated) return;
    mCanonicalPopulated = true;

    // Standard vertex layouts in Mario Galaxy
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
        // Depth on/off, cull back/none with canonical key builder
        PipelineKey k1 = makeCanonicalPipelineKey(stub, vf, true, true, VK_COMPARE_OP_LESS_OR_EQUAL,
                                                 false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                 VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                 VK_CULL_MODE_BACK_BIT);
        PipelineKey k2 = makeCanonicalPipelineKey(stub, vf, false, false, VK_COMPARE_OP_ALWAYS,
                                                 false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                 VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                 VK_CULL_MODE_NONE);
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

            VkBlendFactor srcF = blendOn ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
            VkBlendFactor dstF = blendOn ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO;

            PipelineKey k = makeCanonicalPipelineKey(u, u.vtxFlags, false, false, VK_COMPARE_OP_ALWAYS,
                                                    blendOn != 0, srcF, dstF, srcF, dstF,
                                                    VK_CULL_MODE_NONE);
            addEntry(u, k, "title_screen_and_ui", false);

            PipelineKey kDepth = makeCanonicalPipelineKey(u, u.vtxFlags, true, true, VK_COMPARE_OP_LESS_OR_EQUAL,
                                                         blendOn != 0, srcF, dstF, srcF, dstF,
                                                         VK_CULL_MODE_BACK_BIT);
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

            VkCullModeFlags cm = cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
            PipelineKey k = makeCanonicalPipelineKey(u, u.vtxFlags, true, false, VK_COMPARE_OP_LESS_OR_EQUAL,
                                                    false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                    VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                    cm);
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
                PipelineKey k = makeCanonicalPipelineKey(u, u.vtxFlags, true, true, VK_COMPARE_OP_LESS_OR_EQUAL,
                                                        false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                        VK_CULL_MODE_BACK_BIT);
                addEntry(u, k, "character_models", false);

                // Variant with Clr0
                ShaderUid uClr = u;
                uClr.vtxFlags = 0x4057;
                PipelineKey kClr = makeCanonicalPipelineKey(uClr, uClr.vtxFlags, true, true, VK_COMPARE_OP_LESS_OR_EQUAL,
                                                           false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                           VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                           VK_CULL_MODE_BACK_BIT);
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
            PipelineKey k = makeCanonicalPipelineKey(u, u.vtxFlags, true, true, VK_COMPARE_OP_LESS_OR_EQUAL,
                                                    false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                    VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                    VK_CULL_MODE_BACK_BIT);
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

        VkBlendFactor srcF = VK_BLEND_FACTOR_SRC_ALPHA;
        VkBlendFactor dstF = (blendMode == 0) ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

        PipelineKey k = makeCanonicalPipelineKey(u, u.vtxFlags, true, false, VK_COMPARE_OP_LESS_OR_EQUAL,
                                                true, srcF, dstF, srcF, dstF,
                                                VK_CULL_MODE_NONE);
        addEntry(u, k, "particles_and_starbits", false);
    }

    port_log("vk_shader_inventory: populated canonical Galaxy catalog (%zu unique pipeline keys)", mEntries.size());
}

bool ShaderInventory::loadAssetManifest(const std::string& manifestPath) {
    FILE* f = fopen(manifestPath.c_str(), "rb");
    if (!f) {
        port_log("vk_shader_inventory: manifest file not found at %s", manifestPath.c_str());
        return false;
    }

    struct stat st{};
    if (fstat(fileno(f), &st) != 0) {
        fclose(f);
        port_log("vk_shader_inventory: fstat failed for manifest %s", manifestPath.c_str());
        return false;
    }
    const size_t fileSize = (size_t)st.st_size;
    constexpr size_t kHeaderSize = 5 * sizeof(uint32_t); // 20 bytes
    constexpr size_t kMinRecordSize = (kManifestUidWords + 5) * sizeof(uint32_t) + 1 + 2 + sizeof(uint32_t); // 387 bytes

    if (fileSize < kHeaderSize) {
        fclose(f);
        port_log("vk_shader_inventory: manifest file too small (%zu < %zu) in %s", fileSize, kHeaderSize, manifestPath.c_str());
        return false;
    }

    uint32_t header[5] = {0};
    if (fread(header, sizeof(uint32_t), 5, f) != 5) {
        fclose(f);
        port_log("vk_shader_inventory: failed to read manifest header from %s", manifestPath.c_str());
        return false;
    }

    if (header[0] != kInventoryMagic || header[1] != kInventoryVersion || header[2] != kBuildVersion) {
        fclose(f);
        port_log("vk_shader_inventory: manifest header version mismatch (%s): magic=0x%08x (exp 0x%08x), ver=%u (exp %u), build=%u (exp %u)",
                 manifestPath.c_str(), header[0], kInventoryMagic, header[1], kInventoryVersion, header[2], kBuildVersion);
        return false;
    }

    uint32_t count = header[3];
    uint32_t expectedHeaderCrc = header[4];
    uint32_t actualHeaderCrc = fnv1a32(header, 4 * sizeof(uint32_t));
    if (expectedHeaderCrc != actualHeaderCrc) {
        fclose(f);
        port_log("vk_shader_inventory: manifest header checksum failed (%s): expected 0x%08x, got 0x%08x",
                 manifestPath.c_str(), expectedHeaderCrc, actualHeaderCrc);
        return false;
    }

    if (count > 100000 || (fileSize - kHeaderSize) < (size_t)count * kMinRecordSize) {
        fclose(f);
        port_log("vk_shader_inventory: impossible record count %u (file size %zu) in manifest %s",
                 count, fileSize, manifestPath.c_str());
        return false;
    }

    struct StagedEntry {
        ShaderUid uid;
        PipelineKey key;
        std::string source;
    };
    std::vector<StagedEntry> staged;
    staged.reserve(count);

    bool ok = true;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t uidWords[kManifestUidWords] = {0};
        uint32_t keyFields[5] = {0};
        uint8_t flagsByte = 0;
        uint16_t strLen = 0;

        if (fread(uidWords, sizeof(uint32_t), kManifestUidWords, f) != kManifestUidWords ||
            fread(keyFields, sizeof(uint32_t), 5, f) != 5 ||
            fread(&flagsByte, sizeof(flagsByte), 1, f) != 1 ||
            fread(&strLen, sizeof(strLen), 1, f) != 1) {
            port_log("vk_shader_inventory: manifest record %u truncated in %s", i, manifestPath.c_str());
            ok = false;
            break;
        }

        if (strLen > 512) {
            port_log("vk_shader_inventory: manifest record %u string too long (%u > 512) in %s", i, (unsigned)strLen, manifestPath.c_str());
            ok = false;
            break;
        }

        std::string src;
        if (strLen > 0) {
            src.resize(strLen);
            if (fread(&src[0], 1, strLen, f) != strLen) {
                port_log("vk_shader_inventory: manifest record %u source string truncated in %s", i, manifestPath.c_str());
                ok = false;
                break;
            }
        }

        uint32_t recCrc = 0;
        if (fread(&recCrc, sizeof(recCrc), 1, f) != 1) {
            port_log("vk_shader_inventory: manifest record %u missing CRC in %s", i, manifestPath.c_str());
            ok = false;
            break;
        }

        uint32_t checkH = fnv1a32(uidWords, kManifestUidWords * sizeof(uint32_t));
        checkH = fnv1a32(keyFields, sizeof(keyFields), checkH);
        checkH = fnv1a32(&flagsByte, 1, checkH);
        checkH = fnv1a32(&strLen, 2, checkH);
        if (strLen > 0) checkH = fnv1a32(src.data(), strLen, checkH);
        if (checkH != recCrc) {
            port_log("vk_shader_inventory: manifest record %u CRC mismatch (expected 0x%08x, got 0x%08x) in %s",
                     i, recCrc, checkH, manifestPath.c_str());
            ok = false;
            break;
        }

        ShaderUid uid{};
        memcpy(&uid, uidWords, std::min(sizeof(uid), sizeof(uidWords)));

        PipelineKey key{};
        key.uid = uid;
        key.flags = keyFields[0];
        key.depthState = keyFields[1];
        key.blendState = keyFields[2];
        key.blendState2 = keyFields[3];
        key.rasterState = keyFields[4];

        staged.push_back({uid, key, src.empty() ? "asset_manifest" : ("asset:" + src)});
    }

    if (!ok || staged.size() != count) {
        fclose(f);
        port_log("vk_shader_inventory: validation failed for manifest %s; discarded %zu staged records",
                 manifestPath.c_str(), staged.size());
        return false;
    }

    // Exact EOF check: reject manifests with trailing garbage bytes
    int trailingByte = fgetc(f);
    if (trailingByte != EOF) {
        fclose(f);
        port_log("vk_shader_inventory: unexpected trailing bytes in manifest %s; rejecting entire manifest",
                 manifestPath.c_str());
        return false;
    }

    fclose(f);

    std::lock_guard<std::recursive_mutex> lock(mMutex);
    size_t published = 0;
    for (const auto& item : staged) {
        addEntry(item.uid, item.key, item.source, false);
        published++;
    }

    mStats.assetDerivedEntries = published;
    mStats.isAssetManifestLoaded = (published > 0);
    mActiveManifestPath = manifestPath;
    mActiveManifestBuildVersion = header[2];
    port_log("vk_shader_inventory: successfully validated and atomically published %zu asset-derived materials from manifest %s",
             published, manifestPath.c_str());
    return published > 0;
}

void ShaderInventory::scanAssetDirectory(const std::string& filesDir) {
    // Search candidates in prioritized order
    std::vector<std::string> candidates = {
        mCacheDir + "/galaxy_shader_manifest.bin",
        filesDir + "/galaxy_shader_manifest.bin",
        mDataRoot + "/galaxy_shader_manifest.bin",
        "/sdcard/GalaxyData/galaxy_shader_manifest.bin",
        "assets/galaxy_shader_manifest.bin"
    };

    for (const auto& cand : candidates) {
        if (!cand.empty() && loadAssetManifest(cand)) {
            port_log("vk_shader_inventory: active manifest set to %s", cand.c_str());
            return;
        }
    }

    DIR* d = opendir(filesDir.c_str());
    if (!d) {
        mInventoryGapReports.push_back("Asset directory missing or inaccessible: " + filesDir);
        mStats.explicitInventoryGaps++;
        port_log("vk_shader_inventory: manifest not found; fallback canonical catalog active (%zu entries)", mEntries.size());
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

    mStats.explicitInventoryGaps = arcCount;
    mStats.isAssetManifestLoaded = false;
    char gapBuf[256];
    snprintf(gapBuf, sizeof(gapBuf),
             "Unparsed archives found (%zu .arc files) without cooked manifest; canonical fallback catalog active (%zu entries)",
             arcCount, mEntries.size());
    mInventoryGapReports.push_back(std::string(gapBuf));
    port_log("vk_shader_inventory: %s", gapBuf);
}

void ShaderInventory::recordRuntimeShaderUidMiss(const ShaderUid& uid, const PipelineKey& key, const char* source) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    mStats.runtimeShaderUidMisses++;
    mStats.runtimeDiscoveredEntries++;
    addEntry(uid, key, source ? source : "runtime_uid_miss", true);
    port_log("vk_shader_inventory: runtime ShaderUid miss recorded (uid misses: %zu, total keys: %zu)",
             mStats.runtimeShaderUidMisses, mEntries.size());
    saveInventory();
}

void ShaderInventory::recordRuntimePipelineKeyMiss(const ShaderUid& uid, const PipelineKey& key, const char* source) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    mStats.runtimePipelineKeyMisses++;
    mStats.runtimeDiscoveredEntries++;
    addEntry(uid, key, source ? source : "runtime_key_miss", true);
    port_log("vk_shader_inventory: runtime PipelineKey miss recorded (key misses: %zu, total keys: %zu)",
             mStats.runtimePipelineKeyMisses, mEntries.size());
    saveInventory();
}

void ShaderInventory::incrementPlaceholderUses() {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    mStats.placeholderPipelineUses++;
}

void ShaderInventory::compileAllSpirv(int maxWorkers, std::function<void(int done, int total, const char* msg)> progress) {
    std::vector<ShaderUid> uniqueUids;
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        uniqueUids.reserve(mUidIndex.size());
        for (const auto& kv : mUidIndex) {
            uniqueUids.push_back(kv.first);
        }
    }

    const int total = (int)uniqueUids.size();
    if (total == 0) return;

    port_log("vk_shader_inventory: Stage A: Compiling %d unique shader pairs to SPIR-V...", total);
    auto start = std::chrono::steady_clock::now();

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

    auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mStats.spirvCacheHits = cached.load();
        mStats.spirvCompilationsCompleted = compiled.load();
        mStats.spirvFailures = failed.load();
        mStats.timeSpirvPhaseMs = elapsed;
        mSpirvWarmed = true;

        for (auto& e : mEntries) {
            std::vector<uint32_t> vs, fs;
            if (lookupVulkanShader(e.uid, vs, fs)) {
                e.spirvReady = true;
            }
        }
    }

    port_log("vk_shader_inventory: Stage A complete in %.1f ms! %d shaders (reused from cache: %d, freshly compiled: %d, failed: %d)",
             elapsed, total, cached.load(), compiled.load(), failed.load());
}

void ShaderInventory::prewarmPipelines(VkRenderer& renderer, std::function<void(int done, int total, const char* msg)> progress) {
    std::vector<InventoryEntry> entriesCopy;
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        entriesCopy = mEntries;
    }

    const int total = (int)entriesCopy.size();
    if (total == 0) return;

    port_log("vk_shader_inventory: Stage B: Prewarming %d graphics pipelines with active Vulkan device...", total);
    auto start = std::chrono::steady_clock::now();

    int created = 0;
    int reused = 0;
    int spirvMiss = 0;
    int pipeFail = 0;
    int unsupported = 0;

    for (int i = 0; i < total; i++) {
        const auto& entry = entriesCopy[i];
        PrewarmResult res = renderer.prewarmExactPipeline(entry.key, entry.uid);

        switch (res) {
        case PrewarmResult::Created:
            created++;
            entriesCopy[i].pipelineReady = true;
            break;
        case PrewarmResult::Reused:
            reused++;
            entriesCopy[i].pipelineReady = true;
            break;
        case PrewarmResult::SpirvUnavailable:
            spirvMiss++;
            entriesCopy[i].failed = true;
            entriesCopy[i].failureReason = "SPIR-V unavailable";
            break;
        case PrewarmResult::CreationFailed:
            pipeFail++;
            entriesCopy[i].failed = true;
            entriesCopy[i].failureReason = "vkCreateGraphicsPipelines failed";
            break;
        case PrewarmResult::UnsupportedState:
            unsupported++;
            entriesCopy[i].failed = true;
            entriesCopy[i].failureReason = "Unsupported state or render pass";
            break;
        case PrewarmResult::PlaceholderReturned:
        case PrewarmResult::CompilationPending:
            entriesCopy[i].failed = true;
            entriesCopy[i].failureReason = "Placeholder or pending returned";
            break;
        }

        if (progress && (i % 25 == 0 || i + 1 == total)) {
            progress(i + 1, total, "Prewarming graphics pipelines...");
        }
    }

    renderer.savePipelineCache();
    auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mEntries = std::move(entriesCopy);
        mStats.realPipelinesCreated = created;
        mStats.realPipelinesReused = reused;
        mStats.pipelineCreationFailures = pipeFail + unsupported;
        mStats.spirvFailures += spirvMiss;
        mStats.timePipelinePhaseMs = elapsed;
        mPipelinesWarmed = true;
    }

    port_log("vk_shader_inventory: Stage B complete in %.1f ms! %d pipelines (created: %d, reused: %d, spirvMissing: %d, failed: %d)",
             elapsed, total, created, reused, spirvMiss, pipeFail + unsupported);

    saveInventory();
}

bool ShaderInventory::verifyStartupReadiness() const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    size_t missingEssential = 0;
    size_t totalReadyPipelines = 0;
    std::string firstMissingSource;

    for (const auto& e : mEntries) {
        if (e.pipelineReady) {
            totalReadyPipelines++;
        }
        bool isEssential = (e.sourceAsset == "placeholder_stub" ||
                            e.sourceAsset == "title_screen_and_ui" ||
                            e.sourceAsset.find("mario") != std::string::npos ||
                            e.sourceAsset == "cinematic_intro");
        if (isEssential) {
            if (!e.spirvReady || !e.pipelineReady) {
                missingEssential++;
                if (firstMissingSource.empty()) {
                    firstMissingSource = e.sourceAsset;
                }
            }
        }
    }

    if (missingEssential > 0 || totalReadyPipelines == 0) {
        port_log("vk_shader_inventory: startup readiness check failed: %zu essential startup pipelines not ready (first failure: '%s', total ready: %zu)",
                 missingEssential, firstMissingSource.c_str(), totalReadyPipelines);
        return false;
    }
    port_log("vk_shader_inventory: startup readiness check passed: %zu pipelines validated and ready for immediate gameplay",
             totalReadyPipelines);
    return true;
}

ShaderInventoryStats ShaderInventory::stats() const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    ShaderInventoryStats s = mStats;
    s.totalUniqueKeys = mKeyIndex.size();
    s.totalUniqueUids = mUidIndex.size();

    size_t canonical = 0, assetDerived = 0, runtime = 0;
    size_t notCompiled = 0, notPrewarmed = 0;
    for (const auto& e : mEntries) {
        if (e.isRuntimeDiscovered) runtime++;
        else if (e.sourceAsset.rfind("asset:", 0) == 0) assetDerived++;
        else canonical++;

        if (!e.spirvReady) notCompiled++;
        if (!e.pipelineReady) notPrewarmed++;
    }
    s.canonicalEntries = canonical;
    s.assetDerivedEntries = assetDerived;
    s.runtimeDiscoveredEntries = runtime;
    s.entriesNotYetCompiled = notCompiled;
    s.entriesNotYetPrewarmed = notPrewarmed;
    return s;
}

std::string ShaderInventory::getDiagnostics() const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    char buf[1024];
    snprintf(buf, sizeof(buf),
             "=== Vulkan Shader Inventory & Prewarm Diagnostics ===\n"
             "Total Unique Keys: %zu | Total Unique UIDs: %zu\n"
             "Entries -> Canonical: %zu | Asset-Derived: %zu (Manifest: %s) | Runtime Discovered: %zu\n"
             "Runtime Misses -> ShaderUid Misses: %zu | PipelineKey Misses: %zu\n"
             "SPIR-V Compilation -> Cache Hits: %zu | Freshly Compiled: %zu | Failures: %zu | Time: %.1f ms\n"
             "Pipeline Prewarm -> Reused: %zu | Created: %zu | Creation Failures: %zu | Time: %.1f ms\n"
             "Diagnostics -> Placeholders Used: %zu | Gaps: %zu\n"
             "Coverage Status: %s",
             mKeyIndex.size(), mUidIndex.size(),
             mStats.canonicalEntries, mStats.assetDerivedEntries,
             mStats.isAssetManifestLoaded ? "ACTIVE" : "NOT_LOADED",
             mStats.runtimeDiscoveredEntries,
             mStats.runtimeShaderUidMisses, mStats.runtimePipelineKeyMisses,
             mStats.spirvCacheHits, mStats.spirvCompilationsCompleted, mStats.spirvFailures, mStats.timeSpirvPhaseMs,
             mStats.realPipelinesReused, mStats.realPipelinesCreated, mStats.pipelineCreationFailures, mStats.timePipelinePhaseMs,
             mStats.placeholderPipelineUses, mStats.explicitInventoryGaps,
             mStats.isAssetManifestLoaded ? "Full-Game Asset Manifest Covered" : "Canonical Pre-Baked Coverage (Manifest Cooking Pending)");
    return std::string(buf);
}

void ShaderInventory::saveInventory() {
    if (mInventoryFilePath.empty()) return;
    const std::string tmpPath = mInventoryFilePath + ".tmp";
    FILE* f = fopen(tmpPath.c_str(), "wb");
    if (!f) return;

    uint32_t header[5] = {
        kInventoryMagic,
        kInventoryVersion,
        kBuildVersion,
        (uint32_t)mEntries.size(),
        0  // CRC placeholder
    };
    header[4] = fnv1a32(header, 4 * sizeof(uint32_t));

    if (fwrite(header, sizeof(uint32_t), 5, f) != 5) {
        fclose(f);
        remove(tmpPath.c_str());
        return;
    }

    for (const auto& e : mEntries) {
        uint32_t uidWords[kManifestUidWords] = {0};
        memcpy(uidWords, &e.uid, std::min(sizeof(e.uid), sizeof(uidWords)));

        uint32_t keyFields[5] = {
            e.key.flags,
            e.key.depthState,
            e.key.blendState,
            e.key.blendState2,
            e.key.rasterState
        };
        uint8_t flagsByte = (e.isRuntimeDiscovered ? 1 : 0) |
                            (e.spirvReady ? 2 : 0) |
                            (e.pipelineReady ? 4 : 0) |
                            (e.failed ? 8 : 0);
        uint16_t strLen = (uint16_t)std::min<size_t>(512, e.sourceAsset.size());

        uint32_t recCrc = fnv1a32(uidWords, kManifestUidWords * sizeof(uint32_t));
        recCrc = fnv1a32(keyFields, sizeof(keyFields), recCrc);
        recCrc = fnv1a32(&flagsByte, 1, recCrc);
        recCrc = fnv1a32(&strLen, 2, recCrc);
        if (strLen > 0) recCrc = fnv1a32(e.sourceAsset.data(), strLen, recCrc);

        if (fwrite(uidWords, sizeof(uint32_t), kManifestUidWords, f) != kManifestUidWords ||
            fwrite(keyFields, sizeof(uint32_t), 5, f) != 5 ||
            fwrite(&flagsByte, sizeof(flagsByte), 1, f) != 1 ||
            fwrite(&strLen, sizeof(strLen), 1, f) != 1) {
            fclose(f);
            remove(tmpPath.c_str());
            return;
        }

        if (strLen > 0) {
            if (fwrite(e.sourceAsset.data(), 1, strLen, f) != strLen) {
                fclose(f);
                remove(tmpPath.c_str());
                return;
            }
        }

        if (fwrite(&recCrc, sizeof(recCrc), 1, f) != 1) {
            fclose(f);
            remove(tmpPath.c_str());
            return;
        }
    }

    fflush(f);
    fclose(f);
    rename(tmpPath.c_str(), mInventoryFilePath.c_str());
}

void ShaderInventory::loadInventory() {
    if (mInventoryFilePath.empty()) return;
    FILE* f = fopen(mInventoryFilePath.c_str(), "rb");
    if (!f) return;

    uint32_t header[5] = {0};
    if (fread(header, sizeof(uint32_t), 5, f) != 5) {
        fclose(f);
        return;
    }

    if (header[0] != kInventoryMagic || header[1] != kInventoryVersion || header[2] != kBuildVersion) {
        fclose(f);
        return;
    }

    uint32_t expectedHdrCrc = header[4];
    uint32_t actualHdrCrc = fnv1a32(header, 4 * sizeof(uint32_t));
    if (expectedHdrCrc != actualHdrCrc) {
        fclose(f);
        return;
    }

    uint32_t count = header[3];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t uidWords[kManifestUidWords] = {0};
        uint32_t keyFields[5] = {0};
        uint8_t flagsByte = 0;
        uint16_t strLen = 0;

        if (fread(uidWords, sizeof(uint32_t), kManifestUidWords, f) != kManifestUidWords ||
            fread(keyFields, sizeof(uint32_t), 5, f) != 5 ||
            fread(&flagsByte, sizeof(flagsByte), 1, f) != 1 ||
            fread(&strLen, sizeof(strLen), 1, f) != 1) {
            break;  // Truncation recovery
        }

        if (strLen > 512) {
            break;  // Prevent buffer overrun or reader desynchronization
        }

        std::string src;
        if (strLen > 0) {
            src.resize(strLen);
            if (fread(&src[0], 1, strLen, f) != strLen) {
                break;
            }
        }

        uint32_t recCrc = 0;
        if (fread(&recCrc, sizeof(recCrc), 1, f) != 1) {
            break;
        }

        uint32_t checkH = fnv1a32(uidWords, kManifestUidWords * sizeof(uint32_t));
        checkH = fnv1a32(keyFields, sizeof(keyFields), checkH);
        checkH = fnv1a32(&flagsByte, 1, checkH);
        checkH = fnv1a32(&strLen, 2, checkH);
        if (strLen > 0) checkH = fnv1a32(src.data(), strLen, checkH);
        if (checkH != recCrc) {
            break;  // Corruption detected, stop safely at last valid record
        }

        ShaderUid uid{};
        memcpy(&uid, uidWords, std::min(sizeof(uid), sizeof(uidWords)));

        PipelineKey key{};
        key.uid = uid;
        key.flags = keyFields[0];
        key.depthState = keyFields[1];
        key.blendState = keyFields[2];
        key.blendState2 = keyFields[3];
        key.rasterState = keyFields[4];

        addEntry(uid, key, src, (flagsByte & 1) != 0);
    }

    fclose(f);
    port_log("vk_shader_inventory: loaded %zu persistent inventory entries from %s",
             mEntries.size(), mInventoryFilePath.c_str());
}

void ShaderInventory::invalidateIfStale(uint32_t currentBuildVersion) {
    if (mInventoryFilePath.empty()) return;
    FILE* f = fopen(mInventoryFilePath.c_str(), "rb");
    if (!f) return;

    uint32_t header[5] = {0};
    bool stale = false;
    if (fread(header, sizeof(uint32_t), 5, f) == 5) {
        if (header[0] != kInventoryMagic || header[1] != kInventoryVersion || header[2] != currentBuildVersion) {
            stale = true;
        }
    } else {
        stale = true;
    }
    fclose(f);

    if (stale) {
        port_log("vk_shader_inventory: cache is stale (build version mismatch); invalidating %s",
                 mInventoryFilePath.c_str());
        remove(mInventoryFilePath.c_str());
        mEntries.clear();
        mUidIndex.clear();
        mKeyIndex.clear();
        mStats = ShaderInventoryStats{};
        mCanonicalPopulated = false;
    }
}

}  // namespace gpu
