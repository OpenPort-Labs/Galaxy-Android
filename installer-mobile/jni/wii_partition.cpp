#include "wii_partition.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>
#include "aes.h"

namespace wii {

static const uint8_t WII_COMMON_KEY[16] = {
    0xeb, 0xe4, 0x2a, 0x22, 0x5e, 0x85, 0x93, 0xe4,
    0x48, 0xd9, 0xc5, 0x45, 0x73, 0x81, 0xaa, 0xf7
};

uint16_t read_be16_val(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

uint32_t read_be32_val(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static bool rangeFits(uint64_t offset, uint64_t length, uint64_t limit) {
    return length <= limit && offset <= limit - length;
}

void ensure_directory_exists(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find_first_of('/', pos + 1)) != std::string::npos) {
        std::string sub = path.substr(0, pos);
        if (!sub.empty()) {
            mkdir(sub.c_str(), 0777);
        }
    }
    mkdir(path.c_str(), 0777);
}

WiiPartitionSession::WiiPartitionSession(DiscImageReader* disc) : m_disc(disc) {}

bool WiiPartitionSession::initDataPartition(LogCallback log) {
    uint8_t partInfo[8];
    if (!m_disc->read(0x40000, 8, partInfo)) {
        if (log) log("[Error] Failed to read partition table header at 0x40000.");
        return false;
    }

    uint32_t numPartitions = read_be32_val(partInfo);
    uint64_t tableOffset = (uint64_t)read_be32_val(partInfo + 4) << 2;
    if (numPartitions == 0 || numPartitions > 4) {
        if (log) log("[Error] Implausible partition count in the disc header.");
        return false;
    }

    std::vector<uint8_t> tableData((size_t)numPartitions * 8);
    if (!rangeFits(tableOffset, tableData.size(), m_disc->size()) ||
        !m_disc->read(tableOffset, tableData.size(), tableData.data())) {
        if (log) log("[Error] Failed to read partition table entries.");
        return false;
    }

    bool found = false;
    for (uint32_t i = 0; i < numPartitions; ++i) {
        uint64_t offset = (uint64_t)read_be32_val(&tableData[i * 8]) << 2;
        uint32_t type = read_be32_val(&tableData[i * 8 + 4]);
        if (type == 0) {
            m_partOffset = offset;
            found = true;
            break;
        }
    }

    if (!found) {
        if (log) log("[Error] No game DATA partition found in disc image.");
        return false;
    }

    uint8_t partHdr[0x400];
    if (!m_disc->read(m_partOffset, 0x400, partHdr)) {
        if (log) log("[Error] Failed to read partition header.");
        return false;
    }

    uint8_t iv[16] = {};
    memcpy(iv, partHdr + 0x1DC, 8);
    memcpy(m_titleKey, partHdr + 0x1BF, 16);

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, WII_COMMON_KEY, iv);
    AES_CBC_decrypt_buffer(&ctx, m_titleKey, 16);

    m_dataOffset = m_partOffset + ((uint64_t)read_be32_val(partHdr + 0x2B8) << 2);
    m_dataSize   = (uint64_t)read_be32_val(partHdr + 0x2BC) << 2;

    return true;
}

bool WiiPartitionSession::readDecrypted(uint64_t decOffset, size_t len, uint8_t* out) {
    while (len > 0) {
        uint32_t clusterIdx = (uint32_t)(decOffset / 0x7C00);
        uint32_t clusterRem = (uint32_t)(decOffset % 0x7C00);
        size_t chunk = std::min((size_t)len, (size_t)(0x7C00 - clusterRem));

        if (m_cachedClusterIdx != clusterIdx) {
            uint64_t rawOffset = m_dataOffset + (uint64_t)clusterIdx * 0x8000;
            if (!m_disc->read(rawOffset, 0x8000, m_clusterRaw)) {
                return false;
            }

            uint8_t iv[16];
            memcpy(iv, m_clusterRaw + 0x3D0, 16);
            memcpy(m_clusterDec, m_clusterRaw + 0x400, 0x7C00);

            struct AES_ctx ctx;
            AES_init_ctx_iv(&ctx, m_titleKey, iv);
            AES_CBC_decrypt_buffer(&ctx, m_clusterDec, 0x7C00);

            m_cachedClusterIdx = clusterIdx;
        }

        memcpy(out, m_clusterDec + clusterRem, chunk);
        out += chunk;
        decOffset += chunk;
        len -= chunk;
    }
    return true;
}

bool WiiPartitionSession::extractFileToDisk(uint64_t decOffset, size_t len, const std::string& outPath) {
    size_t lastSlash = outPath.find_last_of('/');
    if (lastSlash != std::string::npos) {
        ensure_directory_exists(outPath.substr(0, lastSlash));
    }

    FILE* fp = fopen(outPath.c_str(), "wb");
    if (!fp) return false;

    uint8_t buf[64 * 1024];
    size_t remaining = len;
    uint64_t curOffset = decOffset;

    while (remaining > 0) {
        size_t chunk = std::min(remaining, sizeof(buf));
        if (!readDecrypted(curOffset, chunk, buf)) {
            fclose(fp);
            return false;
        }
        if (fwrite(buf, 1, chunk, fp) != chunk) {
            fclose(fp);
            return false;
        }
        curOffset += chunk;
        remaining -= chunk;
    }

    fclose(fp);
    return true;
}

} // namespace wii
