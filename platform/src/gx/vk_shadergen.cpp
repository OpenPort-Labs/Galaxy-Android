#include "vk_shadergen.h"
#include "vk_shadergen_common.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <shaderc/shaderc.hpp>

#include "gpu.h"
#include "port/port.h"

namespace gpu {

namespace {

shaderc_optimization_level optimizationLevel() {
    static const shaderc_optimization_level level = [] {
        const char* v = getenv("PETARI_SHADER_OPT");
        return (v && strcmp(v, "0") == 0) ? shaderc_optimization_level_zero : shaderc_optimization_level_performance;
    }();
    return level;
}

constexpr uint32_t kSpirvCacheVersion = 2;
constexpr char kSpirvCacheMagic[4] = {'P', 'T', 'S', 'V'};
constexpr uint32_t kSpirvCacheMaxBytes = 64u << 20;

struct SpirvPair {
    std::vector<uint32_t> vs;
    std::vector<uint32_t> fs;
};

std::mutex sSpirvCacheLock;
std::string sSpirvCachePath;
std::unordered_map<uint64_t, SpirvPair> sSpirvCache;
bool sSpirvCacheLoaded = false;
bool sSpirvCacheHeaderWritten = false;

uint64_t fnv1a64(const void* data, size_t len, uint64_t h) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ull;
    }
    return h;
}

uint32_t fnv1a32(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint32_t)p[i];
        h *= 16777619u;
    }
    return h;
}

uint64_t sourceKey(const std::string& vs, const std::string& fs) {
    static const char* tag = (optimizationLevel() == shaderc_optimization_level_zero)
                                 ? "petari-vk-spirv/1/opt0"
                                 : "petari-vk-spirv/1/opt2";
    uint64_t h = 1469598103934665603ull;
    h = fnv1a64(tag, strlen(tag), h);
    h = fnv1a64(vs.data(), vs.size(), h);
    h = fnv1a64("\0", 1, h);
    h = fnv1a64(fs.data(), fs.size(), h);
    return h;
}

void loadSpirvCacheLocked() {
    sSpirvCacheLoaded = true;
    if (sSpirvCachePath.empty()) return;
    FILE* f = fopen(sSpirvCachePath.c_str(), "rb");
    if (!f) return;

    char magic[4] = {0};
    uint32_t version = 0;
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) || memcmp(magic, kSpirvCacheMagic, sizeof(magic)) != 0 ||
        fread(&version, sizeof(version), 1, f) != 1 || version != kSpirvCacheVersion) {
        fclose(f);
        return;
    }
    sSpirvCacheHeaderWritten = true;

    for (;;) {
        uint64_t key = 0;
        uint32_t vsBytes = 0, fsBytes = 0, vsSum = 0, fsSum = 0;
        if (fread(&key, sizeof(key), 1, f) != 1 || fread(&vsBytes, sizeof(vsBytes), 1, f) != 1 ||
            fread(&fsBytes, sizeof(fsBytes), 1, f) != 1 || fread(&vsSum, sizeof(vsSum), 1, f) != 1 ||
            fread(&fsSum, sizeof(fsSum), 1, f) != 1) {
            break;
        }
        if (vsBytes == 0 || fsBytes == 0 || (vsBytes & 3u) || (fsBytes & 3u) || vsBytes > kSpirvCacheMaxBytes ||
            fsBytes > kSpirvCacheMaxBytes) {
            break;
        }
        SpirvPair pair;
        pair.vs.resize(vsBytes / 4);
        pair.fs.resize(fsBytes / 4);
        if (fread(pair.vs.data(), 1, vsBytes, f) != vsBytes || fread(pair.fs.data(), 1, fsBytes, f) != fsBytes) {
            break;
        }
        if (fnv1a32(pair.vs.data(), vsBytes) != vsSum || fnv1a32(pair.fs.data(), fsBytes) != fsSum) continue;
        sSpirvCache[key] = std::move(pair);
    }
    fclose(f);
    port_log("vk_shadergen: %zu compiled shader programs reused from %s", sSpirvCache.size(), sSpirvCachePath.c_str());
}

void storeSpirvCacheLocked(uint64_t key, const std::vector<uint32_t>& vs, const std::vector<uint32_t>& fs) {
    sSpirvCache[key] = SpirvPair{vs, fs};
    if (sSpirvCachePath.empty()) return;

    FILE* f = fopen(sSpirvCachePath.c_str(), sSpirvCacheHeaderWritten ? "ab" : "wb");
    if (!f) return;
    if (!sSpirvCacheHeaderWritten) {
        uint32_t version = kSpirvCacheVersion;
        fwrite(kSpirvCacheMagic, 1, sizeof(kSpirvCacheMagic), f);
        fwrite(&version, sizeof(version), 1, f);
        sSpirvCacheHeaderWritten = true;
    }

    const uint32_t vsBytes = (uint32_t)(vs.size() * sizeof(uint32_t));
    const uint32_t fsBytes = (uint32_t)(fs.size() * sizeof(uint32_t));
    const uint32_t vsSum = fnv1a32(vs.data(), vsBytes);
    const uint32_t fsSum = fnv1a32(fs.data(), fsBytes);
    uint64_t k = key;
    fwrite(&k, sizeof(k), 1, f);
    fwrite(&vsBytes, sizeof(vsBytes), 1, f);
    fwrite(&fsBytes, sizeof(fsBytes), 1, f);
    fwrite(&vsSum, sizeof(vsSum), 1, f);
    fwrite(&fsSum, sizeof(fsSum), 1, f);
    fwrite(vs.data(), 1, vsBytes, f);
    fwrite(fs.data(), 1, fsBytes, f);
    fclose(f);
}

}  // namespace

bool compileShaderToSPIRV(const std::string& src, bool isVertex, std::vector<uint32_t>& outSpirv, std::string& outError) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    options.SetOptimizationLevel(optimizationLevel());

    shaderc_shader_kind kind = isVertex ? shaderc_glsl_vertex_shader : shaderc_glsl_fragment_shader;
    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(src, kind, isVertex ? "shader.vert" : "shader.frag", options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        outError = result.GetErrorMessage();
        return false;
    }

    outSpirv.assign(result.cbegin(), result.cend());
    return true;
}

bool lookupVulkanShader(const ShaderUid& uid, std::vector<uint32_t>& outVsSpirv, std::vector<uint32_t>& outFsSpirv) {
    const uint64_t key = sourceKey(genVulkanVertexShader(uid), genVulkanFragmentShader(uid));
    std::lock_guard<std::mutex> lock(sSpirvCacheLock);
    if (!sSpirvCacheLoaded) loadSpirvCacheLocked();
    auto it = sSpirvCache.find(key);
    if (it == sSpirvCache.end()) return false;
    outVsSpirv = it->second.vs;
    outFsSpirv = it->second.fs;
    return true;
}

bool compileVulkanShader(const ShaderUid& uid, std::vector<uint32_t>& outVsSpirv, std::vector<uint32_t>& outFsSpirv) {
    std::string vsSrc = genVulkanVertexShader(uid);
    std::string fsSrc = genVulkanFragmentShader(uid);
    const uint64_t key = sourceKey(vsSrc, fsSrc);
    {
        std::lock_guard<std::mutex> lock(sSpirvCacheLock);
        if (!sSpirvCacheLoaded) loadSpirvCacheLocked();
        auto it = sSpirvCache.find(key);
        if (it != sSpirvCache.end()) {
            outVsSpirv = it->second.vs;
            outFsSpirv = it->second.fs;
            return true;
        }
    }

    std::string err;
    if (!compileShaderToSPIRV(vsSrc, true, outVsSpirv, err)) {
        port_log("vk_shadergen: VS error: %s", err.c_str());
        return false;
    }
    if (!compileShaderToSPIRV(fsSrc, false, outFsSpirv, err)) {
        port_log("vk_shadergen: FS error: %s", err.c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock(sSpirvCacheLock);
    storeSpirvCacheLocked(key, outVsSpirv, outFsSpirv);
    return true;
}

void setVulkanShaderCachePath(const char* path) {
    std::lock_guard<std::mutex> lock(sSpirvCacheLock);
    sSpirvCachePath = path ? path : "";
    sSpirvCacheLoaded = false;
    sSpirvCacheHeaderWritten = false;
    sSpirvCache.clear();
}

void prewarmCommonShaders() {
    {
        std::lock_guard<std::mutex> lock(sSpirvCacheLock);
        if (!sSpirvCacheLoaded) loadSpirvCacheLocked();
        if (sSpirvCache.size() >= 8) {
            port_log("vk_shadergen: prewarmCommonShaders skipped (%zu cached shaders already present)", sSpirvCache.size());
            return;
        }
    }

    port_log("vk_shadergen: prewarming common Galaxy shaders into permanent cache...");
    std::vector<ShaderUid> uids;

    uids.push_back(ShaderUid{});

    for (uint32_t chans = 0; chans <= 1; chans++) {
        for (uint32_t texGens = 0; texGens <= 1; texGens++) {
            for (uint32_t tevStages = 1; tevStages <= 1; tevStages++) {
                for (uint32_t hud = 0; hud <= 1; hud++) {
                    ShaderUid u{};
                    u.numChans = chans;
                    u.numTexGens = texGens;
                    u.numTevStages = tevStages;
                    u.hud = hud;
                    for (uint32_t c = 0; c < chans && c < 2; c++) {
                        u.colorCtrl[c] = 0x0001;
                        u.alphaCtrl[c] = 0x0001;
                    }
                    for (uint32_t s = 0; s < tevStages; s++) {
                        u.tevColor[s] = 0x0F0800;
                        u.tevAlpha[s] = 0x0F0800;
                    }
                    uids.push_back(u);
                }
            }
        }
    }

    int count = 0;
    std::vector<uint32_t> vs, fs;
    for (const auto& u : uids) {
        if (compileVulkanShader(u, vs, fs)) {
            count++;
        }
    }
    port_log("vk_shadergen: prewarm completed (%d common shaders compiled and saved to disk)", count);
}

}  // namespace gpu
