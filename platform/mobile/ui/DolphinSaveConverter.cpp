#include "DolphinSaveConverter.h"
#include <string.h>

namespace mobile_ui {

static inline uint16_t bswap16(uint16_t v) { return (v << 8) | (v >> 8); }
static inline uint32_t bswap32(uint32_t v) {
    return ((v & 0xFF000000) >> 24) | ((v & 0x00FF0000) >> 8) |
           ((v & 0x0000FF00) << 8) | ((v & 0x000000FF) << 24);
}
static inline uint64_t bswap64(uint64_t v) {
    return (((uint64_t)bswap32((uint32_t)v)) << 32) | (uint64_t)bswap32((uint32_t)(v >> 32));
}

static inline uint16_t readU16(const uint8_t* p, bool be) {
    uint16_t v; memcpy(&v, p, 2); return be ? bswap16(v) : v;
}
static inline void writeU16(uint8_t* p, uint16_t v, bool be) {
    uint16_t s = be ? bswap16(v) : v; memcpy(p, &s, 2);
}
static inline uint32_t readU32(const uint8_t* p, bool be) {
    uint32_t v; memcpy(&v, p, 4); return be ? bswap32(v) : v;
}
static inline void writeU32(uint8_t* p, uint32_t v, bool be) {
    uint32_t s = be ? bswap32(v) : v; memcpy(p, &s, 4);
}
static inline uint64_t readU64(const uint8_t* p, bool be) {
    uint64_t v; memcpy(&v, p, 8); return be ? bswap64(v) : v;
}
static inline void writeU64(uint8_t* p, uint64_t v, bool be) {
    uint64_t s = be ? bswap64(v) : v; memcpy(p, &s, 8);
}

bool DolphinSaveConverter::isDolphinSave(const uint8_t* data, size_t size) {
    if (!data || size < 0x140 || size > 0x10000) return false;
    return (data[4] == 0 && data[5] == 0 && data[6] == 0 && data[7] == 2 &&
            data[8] == 0 && data[9] == 0 && data[10] == 0 && data[11] == 19);
}

bool DolphinSaveConverter::isNativeSave(const uint8_t* data, size_t size) {
    if (!data || size < 0x140 || size > 0x10000) return false;
    return (data[4] == 2 && data[5] == 0 && data[6] == 0 && data[7] == 0 &&
            data[8] == 19 && data[9] == 0 && data[10] == 0 && data[11] == 0);
}

uint32_t DolphinSaveConverter::calcChecksum(const uint8_t* data, size_t size, bool bigEndian) {
    if (size < 4) return 0;
    uint16_t sum = 0, invSum = 0;
    size_t words = (size - 4) / 2;
    for (size_t i = 0; i < words; i++) {
        uint16_t w = readU16(data + 4 + i * 2, bigEndian);
        sum += w;
        invSum += (uint16_t)(~w);
    }
    return ((uint32_t)sum << 16) | (uint32_t)invSum;
}

static void convertPlayChunk(uint8_t* p, uint32_t sz, bool fromBe) {
    if (sz >= 7) {
        uint32_t sb = readU32(p + 1, fromBe);
        writeU32(p + 1, sb, !fromBe);
        uint16_t lv = readU16(p + 5, fromBe);
        writeU16(p + 5, lv, !fromBe);
        if (sz >= 9) {
            uint16_t sup = readU16(p + 7, fromBe);
            writeU16(p + 7, sup, !fromBe);
        }
    }
}

static void convertWordsChunk(uint8_t* p, uint32_t sz, bool fromBe) {
    for (uint32_t k = 0; k + 2 <= sz; k += 2) {
        uint16_t w = readU16(p + k, fromBe);
        writeU16(p + k, w, !fromBe);
    }
}

static void convertSpnChunk(uint8_t* p, uint32_t sz, bool fromBe) {
    if (sz < 1) return;
    uint8_t nGalaxies = p[0];
    uint32_t gOff = 1;
    for (uint8_t g = 0; g < nGalaxies && gOff + 4 <= sz; g++) {
        uint16_t ghash = readU16(p + gOff, fromBe);
        uint16_t bsize = readU16(p + gOff + 2, fromBe);
        writeU16(p + gOff, ghash, !fromBe);
        writeU16(p + gOff + 2, bsize, !fromBe);
        if (gOff + 6 <= sz) {
            uint8_t nScen = p[gOff + 4];
            uint32_t sOff = gOff + 6;
            for (uint8_t s = 0; s < nScen && sOff + 2 <= gOff + bsize && sOff + 2 <= sz; s++) {
                uint16_t sbsize = readU16(p + sOff, fromBe);
                writeU16(p + sOff, sbsize, !fromBe);
                sOff += (sbsize > 0) ? sbsize : 2;
            }
        }
        gOff += (bsize > 0) ? bsize : 4;
    }
}

static void convertGalaChunk(uint8_t* p, uint32_t sz, bool fromBe) {
    if (sz < 6) return;
    uint16_t attNum = readU16(p, fromBe);
    uint16_t numAtt = readU16(p + 2, fromBe);
    uint16_t dsize = readU16(p + 4, fromBe);
    writeU16(p, attNum, !fromBe);
    writeU16(p + 2, numAtt, !fromBe);
    writeU16(p + 4, dsize, !fromBe);
    for (uint16_t a = 0; a < numAtt && (6 + a * 4 + 4) <= sz; a++) {
        uint32_t aOff = 6 + a * 4;
        uint16_t h = readU16(p + aOff, fromBe);
        uint16_t o = readU16(p + aOff + 2, fromBe);
        writeU16(p + aOff, h, !fromBe);
        writeU16(p + aOff + 2, o, !fromBe);
    }
    uint32_t eStart = 6 + (uint32_t)numAtt * 4;
    for (uint16_t e = 0; e < attNum && (eStart + e * dsize + dsize) <= sz; e++) {
        uint32_t eOff = eStart + e * dsize;
        uint16_t gname = readU16(p + eOff, fromBe);
        writeU16(p + eOff, gname, !fromBe);
        for (int c = 0; c < 8 && (eOff + 4 + c * 2 + 2) <= sz; c++) {
            uint32_t cOff = eOff + 4 + c * 2;
            uint16_t coin = readU16(p + cOff, fromBe);
            writeU16(p + cOff, coin, !fromBe);
        }
    }
}

static void convertSyscChunk(uint8_t* p, uint32_t sz, bool fromBe) {
    if (sz < 4) return;
    uint16_t nAtt = readU16(p, fromBe);
    uint16_t dSz = readU16(p + 2, fromBe);
    writeU16(p, nAtt, !fromBe);
    writeU16(p + 2, dSz, !fromBe);
    for (uint16_t a = 0; a < nAtt && (4 + a * 4 + 4) <= sz; a++) {
        uint32_t aOff = 4 + a * 4;
        uint16_t h = readU16(p + aOff, fromBe);
        uint16_t o = readU16(p + aOff + 2, fromBe);
        writeU16(p + aOff, h, !fromBe);
        writeU16(p + aOff + 2, o, !fromBe);
    }
    uint32_t tOff = 4 + (uint32_t)nAtt * 4;
    if (tOff + 20 <= sz) {
        uint64_t t1 = readU64(p + tOff, fromBe);
        writeU64(p + tOff, t1, !fromBe);
        uint64_t t2 = readU64(p + tOff + 8, fromBe);
        writeU64(p + tOff + 8, t2, !fromBe);
        uint32_t sb = readU32(p + tOff + 16, fromBe);
        writeU32(p + tOff + 16, sb, !fromBe);
    }
}

bool DolphinSaveConverter::processSave(const uint8_t* inData, size_t inSize,
                                     std::vector<uint8_t>& outData, bool fromDolphin) {
    if (!inData || inSize < 0x140 || inSize > 0x10000) return false;
    outData.assign(inData, inData + inSize);
    uint8_t* buf = outData.data();

    uint32_t ver = readU32(buf + 4, fromDolphin);
    uint32_t numFiles = readU32(buf + 8, fromDolphin);
    uint32_t fileSize = readU32(buf + 12, fromDolphin);
    if (ver != 2 || numFiles != 19 || fileSize > inSize) return false;

    writeU32(buf + 4, ver, !fromDolphin);
    writeU32(buf + 8, numFiles, !fromDolphin);
    writeU32(buf + 12, fileSize, !fromDolphin);

    for (uint32_t i = 0; i < numFiles; i++) {
        uint32_t metaOff = 16 + i * 16;
        uint32_t off = readU32(buf + metaOff + 12, fromDolphin);
        writeU32(buf + metaOff + 12, off, !fromDolphin);
        if (off + 4 > inSize) continue;

        uint8_t nChunks = buf[off + 1];
        uint32_t cOff = off + 4;
        for (uint8_t c = 0; c < nChunks && cOff + 12 <= inSize; c++) {
            uint8_t s0 = buf[cOff], s1 = buf[cOff + 1], s2 = buf[cOff + 2], s3 = buf[cOff + 3];
            uint32_t h = readU32(buf + cOff + 4, fromDolphin);
            uint32_t sz = readU32(buf + cOff + 8, fromDolphin);
            buf[cOff] = s3; buf[cOff + 1] = s2; buf[cOff + 2] = s1; buf[cOff + 3] = s0;
            writeU32(buf + cOff + 4, h, !fromDolphin);
            writeU32(buf + cOff + 8, sz, !fromDolphin);

            uint32_t pOff = cOff + 12;
            uint32_t pSz = (sz >= 12) ? sz - 12 : 0;
            char sigStr[5] = { (char)(fromDolphin ? s0 : s3),
                               (char)(fromDolphin ? s1 : s2),
                               (char)(fromDolphin ? s2 : s1),
                               (char)(fromDolphin ? s3 : s0), '\0' };

            if (!strcmp(sigStr, "PLAY")) convertPlayChunk(buf + pOff, pSz, fromDolphin);
            else if (!strcmp(sigStr, "FLG1") || !strcmp(sigStr, "PCE1") ||
                     !strcmp(sigStr, "VLE1") || !strcmp(sigStr, "EVL1")) {
                convertWordsChunk(buf + pOff, pSz, fromDolphin);
            } else if (!strcmp(sigStr, "SPN1")) convertSpnChunk(buf + pOff, pSz, fromDolphin);
            else if (!strcmp(sigStr, "GALA")) convertGalaChunk(buf + pOff, pSz, fromDolphin);
            else if (!strcmp(sigStr, "MISC")) {
                if (pSz >= 9) {
                    uint64_t t = readU64(buf + pOff + 1, fromDolphin);
                    writeU64(buf + pOff + 1, t, !fromDolphin);
                }
            } else if (!strcmp(sigStr, "SYSC")) convertSyscChunk(buf + pOff, pSz, fromDolphin);

            cOff += sz;
        }
    }

    uint32_t cs = calcChecksum(buf, fileSize, !fromDolphin);
    writeU32(buf, cs, !fromDolphin);
    return true;
}

bool DolphinSaveConverter::convertDolphinToNative(const uint8_t* inData, size_t inSize,
                                                std::vector<uint8_t>& outData) {
    return processSave(inData, inSize, outData, true);
}

bool DolphinSaveConverter::convertNativeToDolphin(const uint8_t* inData, size_t inSize,
                                                std::vector<uint8_t>& outData) {
    return processSave(inData, inSize, outData, false);
}

}  // namespace mobile_ui
