#include "wii_extractor.h"
extern "C" {
#include "aes.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <android/log.h>
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "WiiExtractor", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "WiiExtractor", __VA_ARGS__)
#else
#define LOGD(...)
#define LOGE(...)
#endif

namespace wii {

static const uint8_t WII_COMMON_KEY[16] = {
    0xeb, 0xe4, 0x2a, 0x22, 0x5e, 0x85, 0x93, 0xe4,
    0x48, 0xd9, 0xc5, 0x45, 0x73, 0x81, 0xaa, 0xf7
};

static inline uint16_t read_be16(const uint8_t* p) {
    return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}

static inline uint32_t read_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  | uint32_t(p[3]);
}

// True when [offset, offset + length) lies inside [0, limit).
static bool rangeFits(uint64_t offset, uint64_t length, uint64_t limit) {
    return offset <= limit && length <= limit - offset;
}

// A single FST path component: non-empty, relative, no separators.
static bool safeName(const char* name) {
    if (!name || !*name) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    for (const char* c = name; *c; ++c) {
        if (*c == '/' || *c == '\\') return false;
    }
    return true;
}

// Resolves the NUL-terminated name at `nameOff` inside the FST string table.
// Returns null when the offset is out of range, the name runs past the table,
// or the name is not a safe single path component.
static const char* fstName(const char* stringTable, size_t stringTableSize, uint32_t nameOff) {
    if (nameOff >= stringTableSize) return nullptr;
    const char* name = stringTable + nameOff;
    if (!memchr(name, '\0', stringTableSize - nameOff)) return nullptr;
    return safeName(name) ? name : nullptr;
}

static void ensure_directory_exists(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        std::string sub = path.substr(0, pos);
        if (!sub.empty()) {
            mkdir(sub.c_str(), 0777);
        }
    }
    mkdir(path.c_str(), 0777);
}

// ---------------------------------------------------------------------------
// Disc Image Reader abstraction (handles raw ISO and WBFS sparse images)
// ---------------------------------------------------------------------------
class DiscImageReader {
public:
    virtual ~DiscImageReader() = default;
    virtual bool read(uint64_t virtualOffset, size_t len, uint8_t* out) = 0;
    virtual uint64_t size() const = 0;
};

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
        if (!m_fp || virtualOffset + len > m_fileSize) return false;
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
        while (len > 0) {
            uint64_t blockIdx = virtualOffset / m_wbfsBlockSize;
            uint64_t blockRem = virtualOffset % m_wbfsBlockSize;
            size_t chunk = (size_t)std::min((uint64_t)len, m_wbfsBlockSize - blockRem);

            if (blockIdx < m_wlbaTable.size()) {
                uint16_t physBlock = m_wlbaTable[blockIdx];
                if (physBlock == 0) {
                    // Scrubbed/unallocated block
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

    uint64_t size() const override { return 4699979776ULL; } // Standard Wii single layer disc size
};

static std::unique_ptr<DiscImageReader> openDiscImage(const std::string& path) {
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

    // Check WBFS format magic ("WBFS" -> 0x57424653)
    if (read_be32(header) == 0x57424653) {
        uint8_t hdSecSzShift = header[8];
        uint8_t wbfsSecSzShift = header[9];
        // Reject implausible shifts: they seed every derived size and the block
        // count, so an out-of-range value would either overflow or allocate
        // gigabytes.
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

        // In a standalone .wbfs file, disc_info starts at hdSecSz
        if (fseeko(fp, (off_t)hdSecSz, SEEK_SET) != 0) {
            fclose(fp);
            return nullptr;
        }

        uint8_t discInfo[0x100];
        if (fread(discInfo, 1, 0x100, fp) != 0x100) {
            fclose(fp);
            return nullptr;
        }

        // Verify Wii magic inside WBFS disc header at offset 0x18
        if (read_be32(discInfo + 0x18) != 0x5D1C9EA3) {
            fclose(fp);
            return nullptr;
        }

        // Read WLBA table (one 16-bit physical block index per disc block).
        uint64_t blocks = 4699979776ULL / wbfsSecSz;
        if (blocks == 0 || blocks > fileSz / 2) {  // the table itself must fit in the file
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

    // Check raw Wii ISO magic at 0x18 ("\x5D\x1C\x9E\xA3")
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

// ---------------------------------------------------------------------------
// Wii Partition Decryptor & FST Extractor
// ---------------------------------------------------------------------------
class WiiPartitionSession {
private:
    DiscImageReader* m_disc = nullptr;
    uint64_t m_partOffset = 0;
    uint64_t m_dataOffset = 0;
    uint64_t m_dataSize = 0;
    uint8_t m_titleKey[16] = {};

    // 1-cluster cache for efficient streaming
    uint32_t m_cachedClusterIdx = 0xFFFFFFFF;
    uint8_t m_clusterRaw[0x8000] = {};
    uint8_t m_clusterDec[0x7C00] = {};

public:
    WiiPartitionSession(DiscImageReader* disc) : m_disc(disc) {}

    // Size of the decrypted DATA partition, used to bound every logical read.
    uint64_t dataSize() const { return m_dataSize; }

    bool initDataPartition(LogCallback log) {
        // Read partition table info at 0x40000
        uint8_t partInfo[8];
        if (!m_disc->read(0x40000, 8, partInfo)) {
            if (log) log("[Error] Failed to read partition table header at 0x40000.");
            return false;
        }

        uint32_t numPartitions = read_be32(partInfo);
        uint64_t tableOffset = (uint64_t)read_be32(partInfo + 4) << 2;
        // A Wii disc has at most four partitions; anything else is a corrupt
        // header and would otherwise size the allocation below.
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

        // Find the DATA partition (part_type == 0)
        bool found = false;
        for (uint32_t i = 0; i < numPartitions; ++i) {
            uint64_t offset = (uint64_t)read_be32(&tableData[i * 8]) << 2;
            uint32_t type = read_be32(&tableData[i * 8 + 4]);
            if (type == 0) { // DATA partition
                m_partOffset = offset;
                found = true;
                break;
            }
        }

        if (!found) {
            if (log) log("[Error] No game DATA partition found in disc image.");
            return false;
        }

        // Read partition header (Ticket + metadata)
        uint8_t partHdr[0x400];
        if (!m_disc->read(m_partOffset, 0x400, partHdr)) {
            if (log) log("[Error] Failed to read partition header.");
            return false;
        }

        // Decrypt Title Key
        // Ticket encrypted Title Key is at offset 0x1BF (16 bytes)
        // Title ID is at offset 0x1DC (8 bytes)
        uint8_t iv[16] = {};
        memcpy(iv, partHdr + 0x1DC, 8);
        memcpy(m_titleKey, partHdr + 0x1BF, 16);

        struct AES_ctx ctx;
        AES_init_ctx_iv(&ctx, WII_COMMON_KEY, iv);
        AES_CBC_decrypt_buffer(&ctx, m_titleKey, 16);

        // Data offset and size are at 0x2B8 and 0x2BC in partition header
        m_dataOffset = m_partOffset + ((uint64_t)read_be32(partHdr + 0x2B8) << 2);
        m_dataSize   = (uint64_t)read_be32(partHdr + 0x2BC) << 2;

        return true;
    }

    bool readDecrypted(uint64_t decOffset, size_t len, uint8_t* out) {
        while (len > 0) {
            uint32_t clusterIdx = (uint32_t)(decOffset / 0x7C00);
            uint32_t clusterRem = (uint32_t)(decOffset % 0x7C00);
            size_t chunk = std::min((size_t)len, (size_t)(0x7C00 - clusterRem));

            if (m_cachedClusterIdx != clusterIdx) {
                uint64_t rawOffset = m_dataOffset + (uint64_t)clusterIdx * 0x8000;
                if (!m_disc->read(rawOffset, 0x8000, m_clusterRaw)) {
                    return false;
                }

                // IV is located at cluster offset 0x3D0..0x3E0
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

    bool extractFileToDisk(uint64_t decOffset, size_t len, const std::string& outPath) {
        // Ensure parent directory exists
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
};

bool extractWiiDisc(
    const std::string& imagePath,
    const std::string& outDir,
    bool includeMovies,
    LogCallback logCb,
    ProgressCallback progressCb) {

    if (logCb) logCb("[Extractor] Opening disc image: " + imagePath);

    auto disc = openDiscImage(imagePath);
    if (!disc) {
        if (logCb) logCb("[Error] Failed to open disc image or unsupported format.");
        return false;
    }

    WiiPartitionSession session(disc.get());
    if (!session.initDataPartition(logCb)) {
        return false;
    }

    if (logCb) logCb("[Extractor] Decrypted partition initialized. Reading disc filesystem...");

    // Create sys and files directories
    std::string sysDir = outDir + "/sys";
    std::string filesDir = outDir + "/files";
    ensure_directory_exists(sysDir);
    ensure_directory_exists(filesDir);

    // Read boot.bin header (first 0x440 bytes of decrypted partition)
    uint8_t bootBin[0x440];
    if (!session.readDecrypted(0, sizeof(bootBin), bootBin)) {
        if (logCb) logCb("[Error] Failed to read decrypted boot.bin header.");
        return false;
    }

    uint64_t dolOffset = (uint64_t)read_be32(bootBin + 0x420) << 2;
    uint64_t fstOffset = (uint64_t)read_be32(bootBin + 0x424) << 2;
    uint32_t fstSize   = read_be32(bootBin + 0x428) << 2;

    // The FST must be at least a header and lie inside the partition.
    if (fstSize < 12 || !rangeFits(fstOffset, fstSize, session.dataSize())) {
        if (logCb) logCb("[Error] FST offset/size in the disc header is out of range.");
        return false;
    }

    // 1. Extract main.dol
    if (logCb) logCb("[Extractor] Extracting sys/main.dol...");
    uint8_t dolHeader[0x100];
    if (!session.readDecrypted(dolOffset, sizeof(dolHeader), dolHeader)) {
        if (logCb) logCb("[Error] Failed to read DOL header.");
        return false;
    }

    uint32_t dolSize = 0;
    for (int i = 0; i < 7; ++i) {
        uint32_t off = read_be32(dolHeader + i * 4);
        uint32_t len = read_be32(dolHeader + 0x90 + i * 4);
        if (off + len > dolSize) dolSize = off + len;
    }
    for (int i = 0; i < 11; ++i) {
        uint32_t off = read_be32(dolHeader + 0x1C + i * 4);
        uint32_t len = read_be32(dolHeader + 0xAC + i * 4);
        if (off + len > dolSize) dolSize = off + len;
    }

    if (dolSize == 0 || !rangeFits(dolOffset, dolSize, session.dataSize())) {
        if (logCb) logCb("[Error] DOL offset/size in the disc header is out of range.");
        return false;
    }
    if (!session.extractFileToDisk(dolOffset, dolSize, sysDir + "/main.dol")) {
        if (logCb) logCb("[Error] Failed to extract sys/main.dol.");
        return false;
    }

    // 2. Extract fst.bin
    if (logCb) logCb("[Extractor] Extracting sys/fst.bin...");
    if (!session.extractFileToDisk(fstOffset, fstSize, sysDir + "/fst.bin")) {
        if (logCb) logCb("[Error] Failed to extract sys/fst.bin.");
        return false;
    }

    // 3. Parse FST and extract all files
    std::vector<uint8_t> fstData(fstSize);
    if (!session.readDecrypted(fstOffset, fstSize, fstData.data())) {
        if (logCb) logCb("[Error] Failed to read FST table.");
        return false;
    }

    // The 12-byte entry table and the name strings after it must both fit
    // inside the buffer read from the disc; numEntries * 12 is widened so it
    // cannot wrap before the comparison.
    uint32_t numEntries = read_be32(fstData.data() + 8);
    if (numEntries == 0 || (uint64_t)numEntries * 12 >= fstSize) {
        if (logCb) logCb("[Error] FST entry count is out of range.");
        return false;
    }
    const size_t stringTableOffset = (size_t)numEntries * 12;
    const char* stringTable = (const char*)(fstData.data() + stringTableOffset);
    const size_t stringTableSize = fstSize - stringTableOffset;

    struct DirFrame {
        uint32_t nextIdx;
        std::string path;
    };
    std::vector<DirFrame> dirStack;
    dirStack.push_back({numEntries, ""});

    if (logCb) logCb("[Extractor] Found " + std::to_string(numEntries) + " FST entries. Extracting game files...");

    uint32_t filesExtracted = 0;
    uint32_t failedFiles = 0;
    for (uint32_t i = 1; i < numEntries; ++i) {
        const uint8_t* entry = fstData.data() + i * 12;
        while (!dirStack.empty() && i >= dirStack.back().nextIdx) {
            dirStack.pop_back();
        }

        uint8_t flags = entry[0];
        uint32_t nameOff = ((uint32_t)entry[1] << 16) | ((uint32_t)entry[2] << 8) | (uint32_t)entry[3];
        const char* name = fstName(stringTable, stringTableSize, nameOff);
        if (!name) {
            if (logCb) logCb("[Warning] Skipping an FST entry with an invalid name.");
            continue;
        }

        if (flags & 1) { // Directory
            uint32_t nextIdx = read_be32(entry + 8);
            if (nextIdx <= i || nextIdx > numEntries) {
                if (logCb) logCb("[Warning] Skipping an FST directory with an invalid span.");
                continue;
            }
            std::string dirPath = dirStack.back().path.empty() ? name : dirStack.back().path + "/" + name;
            dirStack.push_back({nextIdx, dirPath});
            ensure_directory_exists(filesDir + "/" + dirPath);
        } else { // File
            uint64_t fileOffset = (uint64_t)read_be32(entry + 4) << 2;
            uint32_t fileLen    = read_be32(entry + 8);
            if (!rangeFits(fileOffset, fileLen, session.dataSize())) {
                if (logCb) logCb("[Warning] Skipping an FST entry with an out-of-range file offset.");
                continue;
            }
            std::string filePath = dirStack.back().path.empty() ? name : dirStack.back().path + "/" + name;

            // Skip movies if user did not request them
            if (!includeMovies) {
                if (filePath.find("MovieData") != std::string::npos ||
                    filePath.rfind(".thp") != std::string::npos) {
                    continue;
                }
            }

            std::string fullOutPath = filesDir + "/" + filePath;
            if (!session.extractFileToDisk(fileOffset, fileLen, fullOutPath)) {
                if (logCb) logCb("[Error] Failed to extract: " + filePath);
                failedFiles++;
                continue;
            }

            filesExtracted++;
            if (filesExtracted % 50 == 0 || i == numEntries - 1) {
                int percent = (int)((i * 100) / numEntries);
                if (progressCb) progressCb(percent, "Extracting: " + filePath);
            }
        }
    }

    // Fail closed: a partial tree would be handed straight to the cooker, so an
    // incomplete extraction must not be reported as success.
    if (failedFiles > 0) {
        if (logCb) logCb("[Error] Disc extraction was incomplete: " + std::to_string(failedFiles) +
                         " file(s) could not be written.");
        return false;
    }

    if (logCb) logCb("[Extractor] Successfully extracted " + std::to_string(filesExtracted) + " files!");
    if (progressCb) progressCb(100, "Extraction complete!");
    return true;
}

} // namespace wii
