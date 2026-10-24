#include "wii_extractor.h"
#include "wii_disc_reader.h"
#include "wii_partition.h"
#include "path_security.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <memory>

namespace wii {

// Sanity limits for structures that are small on every real disc but are
// declared with 32-bit fields in the (untrusted) boot header.
static constexpr uint64_t kMaxFstSize = 16ull * 1024 * 1024;
static constexpr uint64_t kMaxDolSize = 64ull * 1024 * 1024;
static constexpr uint32_t kMaxFstEntries = 1u << 17;

static bool rangeFits(uint64_t offset, uint64_t length, uint64_t limit) {
    return length <= limit && offset <= limit - length;
}

static const char* fstName(const char* stringTable, size_t stringTableSize, uint32_t offset) {
    if (offset >= stringTableSize) return nullptr;
    const char* start = stringTable + offset;
    const void* nullTerminator = memchr(start, '\0', stringTableSize - offset);
    if (!nullTerminator) return nullptr;
    return isSafeName(start) ? start : nullptr;
}

bool extractWiiDisc(
    const std::string& imagePath,
    const std::string& outDir,
    bool includeMovies,
    LogCallback logCb,
    ProgressCallback progressCb) {

    if (outDir.empty()) {
        if (logCb) logCb("[Error] Destination directory path is empty.");
        return false;
    }

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

    std::string safeRoot = normalizePath(outDir);
    if (!ensureSafeDirectories(safeRoot, "sys") || !ensureSafeDirectories(safeRoot, "files")) {
        if (logCb) logCb("[Error] Destination directory initialization failed or symlink detected.");
        return false;
    }

    uint8_t bootBin[0x440];
    if (!session.readDecrypted(0, sizeof(bootBin), bootBin)) {
        if (logCb) logCb("[Error] Failed to read decrypted boot.bin header.");
        return false;
    }

    uint64_t dolOffset = (uint64_t)read_be32_val(bootBin + 0x420) << 2;
    uint64_t fstOffset = (uint64_t)read_be32_val(bootBin + 0x424) << 2;
    uint64_t fstSize   = (uint64_t)read_be32_val(bootBin + 0x428) << 2;

    if (fstSize < 12 || fstSize > kMaxFstSize ||
        !rangeFits(fstOffset, fstSize, session.dataSize())) {
        if (logCb) logCb("[Error] FST offset/size in the disc header is out of range.");
        return false;
    }

    uint8_t dolHeader[0x100];
    if (!session.readDecrypted(dolOffset, sizeof(dolHeader), dolHeader)) {
        if (logCb) logCb("[Error] Failed to read DOL header.");
        return false;
    }

    uint64_t dolSize = 0;
    for (int i = 0; i < 7; ++i) {
        uint64_t off = read_be32_val(dolHeader + i * 4);
        uint64_t len = read_be32_val(dolHeader + 0x90 + i * 4);
        if (off + len > dolSize) dolSize = off + len;
    }
    for (int i = 0; i < 11; ++i) {
        uint64_t off = read_be32_val(dolHeader + 0x1C + i * 4);
        uint64_t len = read_be32_val(dolHeader + 0xAC + i * 4);
        if (off + len > dolSize) dolSize = off + len;
    }

    if (dolSize == 0 || dolSize > kMaxDolSize ||
        !rangeFits(dolOffset, dolSize, session.dataSize())) {
        if (logCb) logCb("[Error] DOL offset/size in the disc header is out of range.");
        return false;
    }

    std::vector<uint8_t> fstData((size_t)fstSize);
    if (!session.readDecrypted(fstOffset, fstSize, fstData.data())) {
        if (logCb) logCb("[Error] Failed to read FST table.");
        return false;
    }

    uint32_t numEntries = read_be32_val(fstData.data() + 8);
    if (numEntries == 0 || numEntries > kMaxFstEntries ||
        (uint64_t)numEntries * 12 >= fstSize) {
        if (logCb) logCb("[Error] FST entry count is out of range.");
        return false;
    }
    const size_t stringTableOffset = (size_t)numEntries * 12;
    const char* stringTable = (const char*)(fstData.data() + stringTableOffset);
    const size_t stringTableSize = (size_t)fstSize - stringTableOffset;

    // Validate the whole table and account for every byte it would write
    // before touching the destination, so a hostile FST cannot exhaust the
    // disk by repeating one extent or overflow the running total.
    const uint64_t outputQuota = session.dataSize();
    uint64_t outputBytes = dolSize + fstSize;
    if (outputBytes > outputQuota) {
        if (logCb) logCb("[Error] Declared output exceeds the partition size.");
        return false;
    }
    {
        std::vector<uint32_t> spans;
        spans.push_back(numEntries);
        for (uint32_t i = 1; i < numEntries; ++i) {
            const uint8_t* entry = fstData.data() + (size_t)i * 12;
            while (!spans.empty() && i >= spans.back()) {
                spans.pop_back();
            }
            uint32_t nameOff = ((uint32_t)entry[1] << 16) | ((uint32_t)entry[2] << 8) | (uint32_t)entry[3];
            if (!fstName(stringTable, stringTableSize, nameOff)) continue;
            if (entry[0] & 1) {
                uint32_t nextIdx = read_be32_val(entry + 8);
                if (nextIdx <= i || nextIdx > numEntries) continue;
                if (spans.size() < 32) spans.push_back(nextIdx);
            } else {
                uint64_t fileOffset = (uint64_t)read_be32_val(entry + 4) << 2;
                uint32_t fileLen = read_be32_val(entry + 8);
                if (!rangeFits(fileOffset, fileLen, session.dataSize())) continue;
                outputBytes += fileLen;
                if (outputBytes > outputQuota) {
                    if (logCb) logCb("[Error] FST declares more output than the partition can hold.");
                    return false;
                }
            }
        }
    }

    if (logCb) logCb("[Extractor] Extracting sys/main.dol...");
    if (!session.extractFileToDisk(dolOffset, (size_t)dolSize, "sys/main.dol", safeRoot)) {
        if (logCb) logCb("[Error] Failed to extract sys/main.dol.");
        return false;
    }

    if (logCb) logCb("[Extractor] Extracting sys/fst.bin...");
    if (!session.extractFileToDisk(fstOffset, (size_t)fstSize, "sys/fst.bin", safeRoot)) {
        if (logCb) logCb("[Error] Failed to extract sys/fst.bin.");
        return false;
    }

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
        const uint8_t* entry = fstData.data() + (size_t)i * 12;
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

        if (flags & 1) {
            uint32_t nextIdx = read_be32_val(entry + 8);
            if (nextIdx <= i || nextIdx > numEntries) {
                if (logCb) logCb("[Warning] Skipping an FST directory with an invalid span.");
                continue;
            }
            if (dirStack.size() >= 32) {
                if (logCb) logCb("[Warning] Skipping FST directory exceeding max depth (32).");
                continue;
            }
            std::string dirPath = dirStack.back().path.empty() ? name : dirStack.back().path + "/" + name;
            dirStack.push_back({nextIdx, dirPath});
            if (!ensureSafeDirectories(safeRoot, "files/" + dirPath)) {
                if (logCb) logCb("[Error] Failed to create directory: " + dirPath);
                return false;
            }
        } else {
            uint64_t fileOffset = (uint64_t)read_be32_val(entry + 4) << 2;
            uint32_t fileLen    = read_be32_val(entry + 8);
            if (!rangeFits(fileOffset, fileLen, session.dataSize())) {
                if (logCb) logCb("[Warning] Skipping an FST entry with an out-of-range file offset.");
                continue;
            }
            std::string filePath = dirStack.back().path.empty() ? name : dirStack.back().path + "/" + name;

            if (!includeMovies) {
                if (filePath.find("MovieData") != std::string::npos ||
                    filePath.rfind(".thp") != std::string::npos) {
                    continue;
                }
            }

            if (!session.extractFileToDisk(fileOffset, fileLen, "files/" + filePath, safeRoot)) {
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
