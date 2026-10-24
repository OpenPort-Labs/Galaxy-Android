#pragma once
// Builds a tiny encrypted Wii disc image so extractor tests exercise the real
// boot/DOL/FST/partition path with synthetic, non-proprietary data.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "aes.h"
}

namespace wii_test {

constexpr uint32_t kPlainSize = 0x7C00 * 4;
constexpr uint32_t kRawSize = 0x8000 * 4;
constexpr uint32_t kPartOffset = 0x50000;
constexpr uint32_t kDataOffset = 0x80000;
constexpr uint32_t kImageSize = kDataOffset + kRawSize;
constexpr uint32_t kDolOffset = 0x440;
constexpr uint32_t kFstOffset = 0x800;

struct FileSpec {
    std::string name;
    uint32_t offset;
    uint32_t length;
};

struct SyntheticWiiDisc {
    // Raw boot-header field values; zero means "use the built value".
    uint32_t fstSizeField = 0;
    uint32_t fstEntryCount = 0;
    // DOL text section 0, relative to the DOL header.
    uint32_t dolSectionOff = 0x100;
    uint32_t dolSectionLen = 0x40;
    std::vector<FileSpec> files;

    static void putBE32(uint8_t* p, uint32_t v) {
        p[0] = (uint8_t)(v >> 24);
        p[1] = (uint8_t)(v >> 16);
        p[2] = (uint8_t)(v >> 8);
        p[3] = (uint8_t)v;
    }

    std::vector<uint8_t> build() const {
        static const uint8_t kCommonKey[16] = {
            0xeb, 0xe4, 0x2a, 0x22, 0x5e, 0x85, 0x93, 0xe4,
            0x48, 0xd9, 0xc5, 0x45, 0x73, 0x81, 0xaa, 0xf7};
        static const uint8_t kTitleKey[16] = {
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
            0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};

        std::vector<uint8_t> plain(kPlainSize, 0);
        plain[0x18] = 0x5D; plain[0x19] = 0x1C;
        plain[0x1A] = 0x9E; plain[0x1B] = 0xA3;

        putBE32(&plain[kDolOffset], dolSectionOff);
        putBE32(&plain[kDolOffset + 0x90], dolSectionLen);
        for (uint32_t i = 0x100; i < 0x100 + 0x40; ++i) plain[kDolOffset + i] = 0x55;

        const uint32_t numEntries = (uint32_t)files.size() + 1;
        std::vector<uint8_t> fst(numEntries * 12, 0);
        fst[0] = 1;
        putBE32(&fst[8], numEntries);
        std::string strtab(1, '\0');
        for (size_t i = 0; i < files.size(); ++i) {
            const FileSpec& f = files[i];
            const uint32_t nameOff = (uint32_t)strtab.size();
            strtab += f.name;
            strtab.push_back('\0');
            uint8_t* e = &fst[(i + 1) * 12];
            e[1] = (uint8_t)(nameOff >> 16);
            e[2] = (uint8_t)(nameOff >> 8);
            e[3] = (uint8_t)nameOff;
            putBE32(e + 4, f.offset >> 2);
            putBE32(e + 8, f.length);
            if ((uint64_t)f.offset + f.length <= plain.size()) {
                memset(&plain[f.offset], (int)('A' + i), f.length);
            }
        }
        fst.insert(fst.end(), strtab.begin(), strtab.end());
        while (fst.size() & 3) fst.push_back(0);
        if (fstEntryCount) putBE32(&fst[8], fstEntryCount);
        if (kFstOffset + fst.size() > plain.size()) return {};
        memcpy(&plain[kFstOffset], fst.data(), fst.size());
        putBE32(&plain[0x420], kDolOffset >> 2);
        putBE32(&plain[0x424], kFstOffset >> 2);
        putBE32(&plain[0x428], fstSizeField ? fstSizeField : (uint32_t)(fst.size() >> 2));

        std::vector<uint8_t> image(kImageSize, 0);
        image[0x18] = 0x5D; image[0x19] = 0x1C;
        image[0x1A] = 0x9E; image[0x1B] = 0xA3;
        putBE32(&image[0x40000], 1);
        putBE32(&image[0x40004], 0x40020 >> 2);
        putBE32(&image[0x40020], kPartOffset >> 2);
        putBE32(&image[0x40024], 0);

        uint8_t iv[16] = {};
        uint8_t encKey[16];
        memcpy(encKey, kTitleKey, 16);
        AES_ctx ctx;
        AES_init_ctx_iv(&ctx, kCommonKey, iv);
        AES_CBC_encrypt_buffer(&ctx, encKey, 16);

        uint8_t partHdr[0x400] = {};
        memcpy(partHdr + 0x1BF, encKey, 16);
        putBE32(partHdr + 0x2B8, (kDataOffset - kPartOffset) >> 2);
        putBE32(partHdr + 0x2BC, kPlainSize >> 2);
        memcpy(&image[kPartOffset], partHdr, sizeof(partHdr));

        for (uint32_t i = 0; i < kPlainSize / 0x7C00; ++i) {
            uint8_t* cluster = &image[kDataOffset + i * 0x8000];
            memcpy(cluster + 0x400, &plain[i * 0x7C00], 0x7C00);
            AES_init_ctx_iv(&ctx, kTitleKey, cluster + 0x3D0);
            AES_CBC_encrypt_buffer(&ctx, cluster + 0x400, 0x7C00);
        }
        return image;
    }
};

}  // namespace wii_test
