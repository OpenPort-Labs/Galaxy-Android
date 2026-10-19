#include "thp_types.h"
#include "thp_idct.h"
#include <string.h>

namespace {

using namespace thp;

Decoder sDec;

s32 readQuant(Decoder& d, const u8*& c, const u8* end) {
    s32 length = (s32)read16(c) - 2;
    c += 2;
    while (length > 0 && c < end) {
        u8 pq = *c >> 4, id = *c & 3;
        c++;
        float q[64];
        for (int i = 0; i < 64; i++) {
            q[kNaturalOrder[i]] = pq ? (float)read16(c + i * 2) : (float)c[i];
        }
        c += pq ? 128 : 64;
        length -= pq ? 129 : 65;
        for (int row = 0, i = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++, i++) {
                d.quant[id][i] = (float)((double)q[i] * kAanScale[row] * kAanScale[col]);
            }
        }
        d.validQuant |= 1 << id;
    }
    return 0;
}

s32 readHuffman(Decoder& d, const u8*& c, const u8* end) {
    s32 length = (s32)read16(c) - 2;
    c += 2;
    while (length > 0 && c + 17 <= end) {
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
        if (total > 256 || c + total > end) {
            return kErrBadTable;
        }
        memcpy(h.values, c, (size_t)total);
        c += total;
        length -= 17 + total;

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
        d.validHuff |= 1 << (id * 2 + cls);
    }
    return 0;
}

s32 readFrameHeader(Decoder& d, const u8*& c, const u8* end) {
    if (c + 17 > end) return kErrBadSyntax;
    c += 2;
    if (*c++ != 8) {
        return kErrBadPrecision;
    }
    d.height = read16(c);
    d.width = read16(c + 2);
    c += 4;
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
    if (c + 14 > end) return kErrBadSyntax;
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
        d.comp[i].pred = 0;
    }
    c += 3;
    return 0;
}

inline void fill(Decoder& d) {
    while (d.bits <= 56) {
        u64 b = d.p < d.end ? *d.p++ : 0;
        d.acc |= b << (56 - d.bits);
        d.bits += 8;
    }
}

inline u32 getBits(Decoder& d, int n) {
    u32 v = (u32)(d.acc >> (64 - n));
    d.acc <<= n;
    d.bits -= n;
    return v;
}

inline int decodeSymbol(Decoder& d, const Huffman& h) {
    u16 e = h.look[d.acc >> (64 - Huffman::kLookBits)];
    if (e) {
        getBits(d, e >> 8);
        return e & 0xFF;
    }
    u32 code16 = (u32)(d.acc >> 48);
    for (int l = Huffman::kLookBits + 1; l <= 16; l++) {
        s32 code = (s32)(code16 >> (16 - l));
        if (code <= h.maxCode[l]) {
            getBits(d, l);
            return h.values[(code + h.valOffset[l]) & 0xFF];
        }
    }
    getBits(d, 16);
    return 0;
}

int decodeBlock(Decoder& d, Component& c, s32* coef) {
    memset(coef, 0, 64 * sizeof(s32));
    fill(d);
    int s = decodeSymbol(d, d.huff[c.dc]);
    if (s) {
        c.pred += extend(getBits(d, s & 15), s & 15);
    }
    coef[0] = c.pred;
    int k = 1;
    const Huffman& ac = d.huff[c.ac];
    while (k < 64) {
        fill(d);
        int rs = decodeSymbol(d, ac);
        int r = rs >> 4;
        s = rs & 15;
        if (s) {
            k += r;
            coef[kNaturalOrder[k]] = extend(getBits(d, s), s);
            k++;
        } else if (r == 15) {
            k += 16;
        } else {
            break;
        }
    }
    return k;
}

void decodeScan(Decoder& d, u8* tileY, u8* tileU, u8* tileV) {
    s32 coef[64];
    u8 px[64];
    u32 mcusX = (d.width + 15) / 16, mcusY = (d.height + 15) / 16;
    u32 cw = (d.width + 1) / 2, ch = (d.height + 1) / 2;
    u32 untilRestart = d.restartInterval;
    const float* qy = d.quant[d.comp[0].quant];
    const float* qu = d.quant[d.comp[1].quant];
    const float* qv = d.quant[d.comp[2].quant];
    d.acc = 0;
    d.bits = 0;
    for (u32 my = 0; my < mcusY; my++) {
        for (u32 mx = 0; mx < mcusX; mx++) {
            for (u32 b = 0; b < 4; b++) {
                int n = decodeBlock(d, d.comp[0], coef);
                idct(coef, n, qy, px);
                storeBlock(tileY, d.width, d.height, mx * 16 + (b & 1) * 8, my * 16 + (b >> 1) * 8, px);
            }
            int n = decodeBlock(d, d.comp[1], coef);
            idct(coef, n, qu, px);
            storeBlock(tileU, cw, ch, mx * 8, my * 8, px);
            n = decodeBlock(d, d.comp[2], coef);
            idct(coef, n, qv, px);
            storeBlock(tileV, cw, ch, mx * 8, my * 8, px);

            if (d.restartInterval && --untilRestart == 0) {
                untilRestart = d.restartInterval;
                getBits(d, d.bits & 7);
                d.comp[0].pred = d.comp[1].pred = d.comp[2].pred = 0;
            }
        }
    }
}

}  // namespace

extern "C" {

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
    Decoder& d = sDec;
    d.validHuff = 0;
    d.validQuant = 0;
    d.restartInterval = 0;
    d.width = d.height = 0;
    const u8* c = (const u8*)file;
    const u8* end = c + (4u << 20);
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
            if (c + 4 > end) return kErrBadSyntax;
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
                if (c + 2 > end) return kErrBadSyntax;
                u16 len = read16(c);
                if (c + len > end) return kErrBadSyntax;
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
