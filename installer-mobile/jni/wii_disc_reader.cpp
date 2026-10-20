#include "wii_disc_reader.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

namespace wii {

static inline uint16_t read_be16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static bool rangeFits(uint64_t offset, uint64_t length, uint64_t limit) {
    return length <= limit && offset <= limit - length;
}

class IsoReader : public DiscImageReader {
private:
    FILE* m_fp = nullptr;
    uint64_t m_fileSize = 0;

public:
    IsoReader(FILE* fp, uint64_t sz) : m_fp(fp), m_fileSize(sz) {}
    ~IsoReader() override {
        if (m_fp) fclose(m_fp);
    }

    bool read(uint64_t virtualOffset, size_t len, uint8_t* out) override {
        if (!m_fp || !rangeFits(virtualOffset, len, m_fileSize)) return false;
        if (fseeko(m_fp, (off_t)virtualOffset, SEEK_SET) != 0) return false;
        return fread(out, 1, len, m_fp) == len;
    }

    uint64_t size() const override { return m_fileSize; }
};

class WbfsReader : public DiscImageReader {
private:
    FILE* m_fp = nullptr;
    uint64_t m_fileSize = 0;
    uint32_t m_hdSectorSize = 512;
    uint64_t m_wbfsBlockSize = 2 * 1024 * 1024;
    std::vector<uint16_t> m_wlbaTable;

public:
    WbfsReader(FILE* fp, uint64_t sz, uint32_t hdSec, uint64_t wbfsSec, std::vector<uint16_t> table)
        : m_fp(fp), m_fileSize(sz), m_hdSectorSize(hdSec), m_wbfsBlockSize(wbfsSec), m_wlbaTable(std::move(table)) {}

    ~WbfsReader() override {
        if (m_fp) fclose(m_fp);
    }

    bool read(uint64_t virtualOffset, size_t len, uint8_t* out) override {
        if (!rangeFits(virtualOffset, len, size())) return false;
        while (len > 0) {
            uint64_t blockIdx = virtualOffset / m_wbfsBlockSize;
            uint64_t blockRem = virtualOffset % m_wbfsBlockSize;
            size_t chunk = (size_t)std::min((uint64_t)len, m_wbfsBlockSize - blockRem);

            if (blockIdx < m_wlbaTable.size()) {
                uint16_t physBlock = m_wlbaTable[blockIdx];
                if (physBlock == 0) {
                    memset(out, 0, chunk);
                } else {
                    uint64_t physOffset = (uint64_t)physBlock * m_wbfsBlockSize + blockRem;
                    if (!rangeFits(physOffset, chunk, m_fileSize)) return false;
                    if (fseeko(m_fp, (off_t)physOffset, SEEK_SET) != 0) return false;
                    if (fread(out, 1, chunk, m_fp) != chunk) return false;
                }
            } else {
                memset(out, 0, chunk);
            }

            out += chunk;
            virtualOffset += chunk;
            len -= chunk;
        }
        return true;
    }

    uint64_t size() const override { return 4699979776ULL; }
};

std::unique_ptr<DiscImageReader> openDiscImage(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return nullptr;

    fseeko(fp, 0, SEEK_END);
    uint64_t fileSz = (uint64_t)ftello(fp);
    fseeko(fp, 0, SEEK_SET);

    uint8_t header[512];
    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        fclose(fp);
        return nullptr;
    }

    if (read_be32(header) == 0x57424653) {
        uint8_t hdSecSzShift = header[8];
        uint8_t wbfsSecSzShift = header[9];
        if (hdSecSzShift < 9 || hdSecSzShift > 31 || wbfsSecSzShift < 15 || wbfsSecSzShift > 31) {
            fclose(fp);
            return nullptr;
        }
        uint32_t hdSecSz = 1u << hdSecSzShift;
        uint64_t wbfsSecSz = (uint64_t)1 << wbfsSecSzShift;
        if (fileSz < hdSecSz + 0x100) {
            fclose(fp);
            return nullptr;
        }

        if (fseeko(fp, (off_t)hdSecSz, SEEK_SET) != 0) {
            fclose(fp);
            return nullptr;
        }

        uint8_t discInfo[0x100];
        if (fread(discInfo, 1, 0x100, fp) != 0x100) {
            fclose(fp);
            return nullptr;
        }

        if (read_be32(discInfo + 0x18) != 0x5D1C9EA3) {
            fclose(fp);
            return nullptr;
        }

        uint64_t blocks = 4699979776ULL / wbfsSecSz;
        if (blocks == 0 || blocks > fileSz / 2) {
            fclose(fp);
            return nullptr;
        }
        size_t numBlocks = (size_t)blocks;
        std::vector<uint16_t> wlba(numBlocks);
        std::vector<uint8_t> rawTable(numBlocks * 2);
        if (fread(rawTable.data(), 1, rawTable.size(), fp) != rawTable.size()) {
            fclose(fp);
            return nullptr;
        }
        for (size_t i = 0; i < numBlocks; ++i) {
            wlba[i] = read_be16(&rawTable[i * 2]);
        }

        return std::make_unique<WbfsReader>(fp, fileSz, hdSecSz, wbfsSecSz, std::move(wlba));
    }

    if (read_be32(header + 0x18) == 0x5D1C9EA3) {
        return std::make_unique<IsoReader>(fp, fileSz);
    }

    fclose(fp);
    return nullptr;
}

bool isWiiDiscImage(const std::string& filePath) {
    auto reader = openDiscImage(filePath);
    return reader != nullptr;
}

} // namespace wii
