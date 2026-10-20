#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gx_shader_uid.h"
#include "gx/vk/vk_types.h"

namespace gpu {

constexpr uint32_t kInventoryMagic = 0x55535450;      // "PTSU"
// Schema 3 widened the serialized ShaderUid from 90 words to the full struct.
// Schema 4 drops efbHasAlpha, which no shader or pipeline field depends on.
constexpr uint32_t kInventoryVersion = 4;
constexpr uint32_t kBuildVersion = 20261020;
constexpr uint32_t kShaderGenVersion = 1;
constexpr size_t kManifestUidWords = 99;
static_assert(sizeof(ShaderUid) == kManifestUidWords * sizeof(uint32_t),
              "kManifestUidWords must serialise every ShaderUid word");

uint32_t fnv1a32(const void* data, size_t size, uint32_t seed = 2166136261u);

struct ManifestStagedEntry {
    ShaderUid uid;
    PipelineKey key;
    std::string source;
};

bool parseAssetManifest(const std::string& manifestPath,
                        std::vector<ManifestStagedEntry>& outEntries,
                        uint32_t& outBuildVersion);

// Writes the same format the reader above accepts.  Used for the cooked
// manifest and for the runtime-discovered inventory cache.
bool writeAssetManifest(const std::string& path,
                        const std::vector<ManifestStagedEntry>& entries,
                        uint32_t buildVersion);

std::vector<std::string> getManifestCandidates(const std::string& cacheDir,
                                              const std::string& dataRoot,
                                              const std::string& filesDir);

bool isInventoryFileStale(const std::string& path, uint32_t currentBuildVersion);

}  // namespace gpu
