#include "thp_types.h"
#include "thp_scan.h"

#include <stddef.h>
#include <string.h>

namespace {

using namespace thp;

Decoder sDec;
u32 sInputSize = 4u << 20;
u32 sOutputWidth = 0;
u32 sOutputHeight = 0;

const u32 kMaxInputSize = 64u << 20;
const u32 kMaxFrameWidth = 1920;
const u32 kMaxFrameHeight = 1088;

const u8* beginSegment(const u8*& c, const u8* end) {
    if (c > end || (size_t)(end - c) < 2) return nullptr;
    u32 length = read16(c);
    c += 2;
    if (length < 2 || (size_t)(length - 2) > (size_t)(end - c)) return nullptr;
    return c + (length - 2);
}

s32 readQuant(Decoder& d, const u8*& c, const u8* end) {
    const u8* segEnd = beginSegment(c, end);
    if (!segEnd) return kErrBadSyntax;
    while (c < segEnd) {
        u8 head = *c++;
        u8 pq = head >> 4, id = head & 3;
        if (pq > 1) return kErrBadTable;
        u32 bytes = pq ? 128 : 64;
        if ((size_t)(segEnd - c) < bytes) return kErrBadSyntax;
        float q[64];
        for (int i = 0; i < 64; i++) {
            q[kNaturalOrder[i]] = pq ? (float)read16(c + i * 2) : (float)c[i];
        }
        c += bytes;
        for (int row = 0, i = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++, i++) {
                d.quant[id][i] = (float)((double)q[i] * kAanScale[row] * kAanScale[col]);
            }
        }
        d.validQuant |= (u8)(1 << id);
    }
    return 0;
}

s32 readHuffman(Decoder& d, const u8*& c, const u8* end) {
    const u8* segEnd = beginSegment(c, end);
    if (!segEnd) return kErrBadSyntax;
    while (c < segEnd) {
        if ((size_t)(segEnd - c) < 17) return kErrBadSyntax;
        u8 cls = *c >> 4, id = *c & 15;
        c++;
        if (cls > 1 || id > 1) {
            return kErrBadTable;
        }
        Huffman& h = d.huff[id * 2 + cls];
        const u8* counts = c;
        c += 16;
        s32 total = 0;
        for (int l = 0; l < 16; l++) {
            total += counts[l];
        }
        if (total > 256 || (size_t)(segEnd - c) < (size_t)total) {
            return kErrBadTable;
        }
        memcpy(h.values, c, (size_t)total);
        c += total;

        memset(h.look, 0, sizeof(h.look));
        s32 code = 0, k = 0;
        for (int l = 1; l <= 16; l++) {
            h.valOffset[l] = k - code;
            if (counts[l - 1] == 0) {
                h.maxCode[l] = -1;
            } else {
                for (int i = 0; i < counts[l - 1]; i++, code++, k++) {
                    if (l <= Huffman::kLookBits) {
                        int shift = Huffman::kLookBits - l;
                        for (int j = 0; j < (1 << shift); j++) {
                            h.look[(code << shift) | j] = (u16)(l << 8 | h.values[k]);
                        }
                    }
                }
                h.maxCode[l] = code - 1;
            }
            code <<= 1;
        }
        h.maxCode[17] = 0x7FFFFFFF;
        d.validHuff |= (u8)(1 << (id * 2 + cls));
    }
    return 0;
}

s32 readFrameHeader(Decoder& d, const u8*& c, const u8* end) {
    if (c > end || (size_t)(end - c) < 17) return kErrBadSyntax;
    c += 2;
    if (*c++ != 8) {
        return kErrBadPrecision;
    }
    d.height = read16(c);
    d.width = read16(c + 2);
    c += 4;
    if (!d.width || !d.height || d.width > kMaxFrameWidth || d.height > kMaxFrameHeight) {
        return kErrBadSyntax;
    }
    if (sOutputWidth && sOutputHeight && (d.width != sOutputWidth || d.height != sOutputHeight)) {
        return kErrBadSyntax;
    }
    if (*c++ != 3) {
        return kErrBadComponents;
    }
    for (int i = 0; i < 3; i++) {
        c++;
        u8 sampling = *c++;
        if ((i == 0 && sampling != 0x22) || (i > 0 && sampling != 0x11)) {
            return kErrBadSampling;
        }
        d.comp[i].quant = *c++ & 3;
    }
    return 0;
}

s32 readScanHeader(Decoder& d, const u8*& c, const u8* end) {
    if (c > end || (size_t)(end - c) < 14) return kErrBadSyntax;
    c += 2;
    if (*c++ != 3) {
        return kErrBadComponents;
    }
    for (int i = 0; i < 3; i++) {
        c++;
        u8 tables = *c++;
        d.comp[i].dc = (u8)((tables >> 4) & 1) * 2;
        d.comp[i].ac = (u8)(tables & 1) * 2 + 1;
        if (!(d.validHuff & (1 << d.comp[i].dc)) || !(d.validHuff & (1 << d.comp[i].ac))) {
            return kErrBadTable;
        }
        if (!(d.validQuant & (1 << d.comp[i].quant))) {
            return kErrBadTable;
        }
        d.comp[i].pred = 0;
    }
    c += 3;
    return 0;
}

}  // namespace

extern "C" {

void THPSetInputSize(u32 size) { sInputSize = size; }
void THPSetOutputSize(u32 width, u32 height) {
    sOutputWidth = width;
    sOutputHeight = height;
}

BOOL THPInit(void) { return TRUE; }

s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV, void* work) {
    if (!file) {
        return kErrNoInput;
    }
    if (!tileY || !tileU || !tileV) {
        return kErrNoOutput;
    }
    if (!work) {
        return kErrNoWork;
    }
    if (sInputSize < 4 || sInputSize > kMaxInputSize) {
        return kErrNoInput;
    }
    Decoder& d = sDec;
    d.validHuff = 0;
    d.validQuant = 0;
    d.restartInterval = 0;
    d.width = d.height = 0;
    const u8* c = (const u8*)file;
    const u8* end = c + sInputSize;
    for (;;) {
        if (c >= end || *c++ != 0xFF) {
            return kErrBadSyntax;
        }
        while (c < end && *c == 0xFF) {
            c++;
        }
        if (c >= end) {
            return kErrBadSyntax;
        }
        u8 marker = *c++;
        s32 status = 0;
        switch (marker) {
        case 0xD8:
            break;
        case 0xC0:
            status = readFrameHeader(d, c, end);
            break;
        case 0xC4:
            status = readHuffman(d, c, end);
            break;
        case 0xDB:
            status = readQuant(d, c, end);
            break;
        case 0xDD:
            if (c > end || (size_t)(end - c) < 4) return kErrBadSyntax;
            d.restartInterval = read16(c + 2);
            c += 4;
            break;
        case 0xDA:
            status = readScanHeader(d, c, end);
            if (status == 0) {
                if (!d.width || !d.height) {
                    return kErrBadSyntax;
                }
                d.p = c;
                d.end = end;
                decodeScan(d, (u8*)tileY, (u8*)tileU, (u8*)tileV);
            }
            return status;
        default:
            if ((marker >= 0xE0 && marker <= 0xEF) || marker == 0xFE) {
                if (c > end || (size_t)(end - c) < 2) return kErrBadSyntax;
                u16 len = (u16)read16(c);
                if (len < 2 || (size_t)(end - c) < (size_t)len) return kErrBadSyntax;
                c += len;
                break;
            }
            return kErrUnsupportedMarker;
        }
        if (status != 0) {
            return status;
        }
    }
}

}  // extern "C"
