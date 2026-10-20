#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "wii_disc_reader.h"
#include "wii_extractor.h"

namespace wii {

void ensure_directory_exists(const std::string& path);
uint32_t read_be32_val(const uint8_t* p);
uint16_t read_be16_val(const uint8_t* p);

class WiiPartitionSession {
private:
    DiscImageReader* m_disc = nullptr;
    uint64_t m_partOffset = 0;
    uint64_t m_dataOffset = 0;
    uint64_t m_dataSize = 0;
    uint8_t m_titleKey[16] = {};

    uint32_t m_cachedClusterIdx = 0xFFFFFFFF;
    uint8_t m_clusterRaw[0x8000] = {};
    uint8_t m_clusterDec[0x7C00] = {};

public:
    explicit WiiPartitionSession(DiscImageReader* disc);

    uint64_t dataSize() const { return m_dataSize; }
    bool initDataPartition(LogCallback log);
    bool readDecrypted(uint64_t decOffset, size_t len, uint8_t* out);
    bool extractFileToDisk(uint64_t decOffset, size_t len, const std::string& outPath);
};

} // namespace wii
