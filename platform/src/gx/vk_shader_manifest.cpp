#include "gx/vk_shader_manifest.h"
#include "port/port.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <algorithm>

namespace gpu {

uint32_t fnv1a32(const void* data, size_t size, uint32_t seed) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t h = seed;
    for (size_t i = 0; i < size; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

std::vector<std::string> getManifestCandidates(const std::string& cacheDir,
                                              const std::string& dataRoot,
                                              const std::string& filesDir) {
    return {
        cacheDir + "/galaxy_shader_manifest.bin",
        filesDir + "/galaxy_shader_manifest.bin",
        dataRoot + "/galaxy_shader_manifest.bin",
        "/sdcard/GalaxyData/galaxy_shader_manifest.bin",
        "assets/galaxy_shader_manifest.bin"
    };
}

bool parseAssetManifest(const std::string& manifestPath,
                        std::vector<ManifestStagedEntry>& outEntries,
                        uint32_t& outBuildVersion) {
    FILE* f = fopen(manifestPath.c_str(), "rb");
    if (!f) return false;

    struct stat st{};
    if (fstat(fileno(f), &st) != 0) {
        fclose(f);
        return false;
    }
    const size_t fileSize = (size_t)st.st_size;
    constexpr size_t kHeaderSize = 5 * sizeof(uint32_t);
    constexpr size_t kMinRecordSize = (kManifestUidWords + 5) * sizeof(uint32_t) + 1 + 2 + sizeof(uint32_t);

    if (fileSize < kHeaderSize) {
        fclose(f);
        return false;
    }

    uint32_t header[5] = {0};
    if (fread(header, sizeof(uint32_t), 5, f) != 5) {
        fclose(f);
        return false;
    }

    if (header[0] != kInventoryMagic || header[1] != kInventoryVersion || header[2] != kBuildVersion) {
        fclose(f);
        return false;
    }

    uint32_t count = header[3];
    uint32_t expectedHeaderCrc = header[4];
    uint32_t actualHeaderCrc = fnv1a32(header, 4 * sizeof(uint32_t));
    if (expectedHeaderCrc != actualHeaderCrc) {
        fclose(f);
        return false;
    }

    if (count > 100000 || (fileSize - kHeaderSize) < (size_t)count * kMinRecordSize) {
        fclose(f);
        return false;
    }

    outEntries.clear();
    outEntries.reserve(count);
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
            ok = false;
            break;
        }

        if (strLen > 512) {
            ok = false;
            break;
        }

        std::string src;
        if (strLen > 0) {
            src.resize(strLen);
            if (fread(&src[0], 1, strLen, f) != strLen) {
                ok = false;
                break;
            }
        }

        uint32_t recCrc = 0;
        if (fread(&recCrc, sizeof(recCrc), 1, f) != 1) {
            ok = false;
            break;
        }

        uint32_t checkH = fnv1a32(uidWords, kManifestUidWords * sizeof(uint32_t));
        checkH = fnv1a32(keyFields, sizeof(keyFields), checkH);
        checkH = fnv1a32(&flagsByte, 1, checkH);
        checkH = fnv1a32(&strLen, 2, checkH);
        if (strLen > 0) checkH = fnv1a32(src.data(), strLen, checkH);
        if (checkH != recCrc) {
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

        outEntries.push_back({uid, key, src.empty() ? "asset_manifest" : ("asset:" + src)});
    }

    if (!ok || outEntries.size() != count) {
        fclose(f);
        outEntries.clear();
        return false;
    }

    int trailingByte = fgetc(f);
    if (trailingByte != EOF) {
        fclose(f);
        outEntries.clear();
        return false;
    }

    fclose(f);
    outBuildVersion = header[2];
    return true;
}

bool isInventoryFileStale(const std::string& path, uint32_t currentBuildVersion) {
    if (path.empty()) return false;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

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
    return stale;
}

}  // namespace gpu
