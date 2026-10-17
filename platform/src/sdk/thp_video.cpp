// THP movie frame decoder.  The SDK's THPDec.c is Broadway assembly (paired
// singles and the locked cache), so this is a portable replacement with the
// same contract: THPVideoDecode() takes one frame's video component and
// writes the Y, U and V planes straight into GX I8 textures (8x4 texel tiles
// of 32 bytes each) for THPGXYuv2RgbDraw().
//
// THP video is baseline JPEG with fixed 4:2:0 sampling, except that the
// entropy-coded data has no 0xFF00 byte stuffing and a restart interval only
// realigns to the next byte (there are no RSTn markers).  Error codes match
// the SDK's.
#ifdef THP_STANDALONE  // host-side test build (tools/thp_test)
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t s16;
typedef int32_t s32;
typedef int BOOL;
#define TRUE 1
#else
#include "revolution.h"
#include "revolution/thp.h"
#endif
#include <string.h>

namespace {

enum : s32 {
    kErrBadSyntax = 3,
    kErrBadPrecision = 10,
    kErrUnsupportedMarker = 11,
    kErrBadComponents = 12,
    kErrBadTable = 15,
    kErrBadSampling = 19,
    kErrNoInput = 25,
    kErrNoWork = 26,
    kErrNoOutput = 27,
};

const u8 kNaturalOrder[64 + 16] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
    // Corrupt data can run past the end of a block; keep it inside.
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63,
};

// AAN IDCT scale factors: cos(k*pi/16) * sqrt(2) for k > 0.
const double kAanScale[8] = {1.0, 1.387039845, 1.306562965, 1.175875602, 1.0, 0.785694958, 0.541196100, 0.275899379};

struct Huffman {
    enum { kLookBits = 9 };
    u16 look[1 << kLookBits];  // (length << 8) | value for codes up to kLookBits long, 0 otherwise
    s32 maxCode[18];           // largest code of each length, -1 if there is none
    s32 valOffset[18];         // value index = code + valOffset[length]
    u8 values[256];
};

struct Component {
    u8 quant, dc, ac;
    s32 pred;
};

struct Decoder {
    float quant[4][64];  // dequantization with the AAN scale folded in, natural order
    Huffman huff[4];     // [id * 2 + class], class 0 = DC, 1 = AC
    u8 validHuff;
    u8 validQuant;
    Component comp[3];
    u32 width, height;
    u32 restartInterval;
    const u8* p;
    const u8* end;
    u64 acc;  // bit buffer, next bit in the MSB
    s32 bits;
};

Decoder sDec;

u32 read16(const u8* p) { return (u32)p[0] << 8 | p[1]; }

// ---------------------------------------------------------------------------
// Headers
// ---------------------------------------------------------------------------
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

s32 readFrameHeader(Decoder& d, const u8*& c) {
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
        c++;  // component id
        u8 sampling = *c++;
        if ((i == 0 && sampling != 0x22) || (i > 0 && sampling != 0x11)) {
            return kErrBadSampling;
        }
        d.comp[i].quant = *c++ & 3;
    }
    return 0;
}

s32 readScanHeader(Decoder& d, const u8*& c) {
    c += 2;
    if (*c++ != 3) {
        return kErrBadComponents;
    }
    for (int i = 0; i < 3; i++) {
        c++;  // component id
        u8 tables = *c++;
        d.comp[i].dc = (u8)((tables >> 4) & 1) * 2;
        d.comp[i].ac = (u8)(tables & 1) * 2 + 1;
        if (!(d.validHuff & (1 << d.comp[i].dc)) || !(d.validHuff & (1 << d.comp[i].ac))) {
            return kErrBadTable;
        }
        d.comp[i].pred = 0;
    }
    c += 3;  // spectral selection and approximation: always 0, 63, 0
    return 0;
}

// ---------------------------------------------------------------------------
// Entropy decoding
// ---------------------------------------------------------------------------
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

// Needs 16 valid bits.
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
    getBits(d, 16);  // corrupt data
    return 0;
}

inline s32 extend(u32 v, int s) { return v < (1u << (s - 1)) ? (s32)v - (1 << s) + 1 : (s32)v; }

// Decodes one block's coefficients (natural order) and returns how many
// entries of the zigzag sequence may be nonzero.
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

// ---------------------------------------------------------------------------
// Inverse DCT (AAN, as in libjpeg's jidctflt.c) straight into I8 tiles
// ---------------------------------------------------------------------------
inline u8 toPixel(float v) {
    v = v * 0.125f + 128.5f;
    return v <= 0.0f ? 0 : v >= 255.0f ? 255 : (u8)v;
}

void idct(const s32* coef, int count, const float* q, u8* out /* 8x8 */) {
    if (count <= 1) {
        u8 v = toPixel((float)coef[0] * q[0]);
        memset(out, v, 64);
        return;
    }
    float ws[64];
    for (int col = 0; col < 8; col++) {
        const s32* in = coef + col;
        const float* qc = q + col;
        float* w = ws + col;
        if (!in[8] && !in[16] && !in[24] && !in[32] && !in[40] && !in[48] && !in[56]) {
            float dc = (float)in[0] * qc[0];
            for (int r = 0; r < 8; r++) {
                w[r * 8] = dc;
            }
            continue;
        }
        float tmp0 = (float)in[0] * qc[0], tmp1 = (float)in[16] * qc[16];
        float tmp2 = (float)in[32] * qc[32], tmp3 = (float)in[48] * qc[48];
        float tmp10 = tmp0 + tmp2, tmp11 = tmp0 - tmp2;
        float tmp13 = tmp1 + tmp3, tmp12 = (tmp1 - tmp3) * 1.414213562f - tmp13;
        tmp0 = tmp10 + tmp13;
        tmp3 = tmp10 - tmp13;
        tmp1 = tmp11 + tmp12;
        tmp2 = tmp11 - tmp12;

        float tmp4 = (float)in[8] * qc[8], tmp5 = (float)in[24] * qc[24];
        float tmp6 = (float)in[40] * qc[40], tmp7 = (float)in[56] * qc[56];
        float z13 = tmp6 + tmp5, z10 = tmp6 - tmp5, z11 = tmp4 + tmp7, z12 = tmp4 - tmp7;
        tmp7 = z11 + z13;
        tmp11 = (z11 - z13) * 1.414213562f;
        float z5 = (z10 + z12) * 1.847759065f;
        tmp10 = 1.082392200f * z12 - z5;
        tmp12 = -2.613125930f * z10 + z5;
        tmp6 = tmp12 - tmp7;
        tmp5 = tmp11 - tmp6;
        tmp4 = tmp10 + tmp5;

        w[0] = tmp0 + tmp7;
        w[56] = tmp0 - tmp7;
        w[8] = tmp1 + tmp6;
        w[48] = tmp1 - tmp6;
        w[16] = tmp2 + tmp5;
        w[40] = tmp2 - tmp5;
        w[32] = tmp3 + tmp4;
        w[24] = tmp3 - tmp4;
    }
    for (int row = 0; row < 8; row++) {
        const float* w = ws + row * 8;
        u8* o = out + row * 8;
        float tmp10 = w[0] + w[4], tmp11 = w[0] - w[4];
        float tmp13 = w[2] + w[6], tmp12 = (w[2] - w[6]) * 1.414213562f - tmp13;
        float tmp0 = tmp10 + tmp13, tmp3 = tmp10 - tmp13, tmp1 = tmp11 + tmp12, tmp2 = tmp11 - tmp12;

        float z13 = w[5] + w[3], z10 = w[5] - w[3], z11 = w[1] + w[7], z12 = w[1] - w[7];
        float tmp7 = z11 + z13;
        tmp11 = (z11 - z13) * 1.414213562f;
        float z5 = (z10 + z12) * 1.847759065f;
        tmp10 = 1.082392200f * z12 - z5;
        tmp12 = -2.613125930f * z10 + z5;
        float tmp6 = tmp12 - tmp7, tmp5 = tmp11 - tmp6, tmp4 = tmp10 + tmp5;

        o[0] = toPixel(tmp0 + tmp7);
        o[7] = toPixel(tmp0 - tmp7);
        o[1] = toPixel(tmp1 + tmp6);
        o[6] = toPixel(tmp1 - tmp6);
        o[2] = toPixel(tmp2 + tmp5);
        o[5] = toPixel(tmp2 - tmp5);
        o[4] = toPixel(tmp3 + tmp4);
        o[3] = toPixel(tmp3 - tmp4);
    }
}

// Writes an 8x8 block at (x, y) into an I8 texture of the given size.  Widths
// that are not a multiple of 8 put the last block of a row past the plane, so
// the block is clipped to it instead of overwriting the next tile row.
void storeBlock(u8* plane, u32 width, u32 height, u32 x, u32 y, const u8* px) {
    if (x >= width) {
        return;
    }
    u32 tilesPerRow = (width + 7) / 8;
    size_t rowBytes = (x + 8 <= width) ? 8 : (size_t)(width - x);
    for (u32 r = 0; r < 8 && y + r < height; r++) {
        u32 row = y + r;
        memcpy(plane + ((size_t)(row >> 2) * tilesPerRow + (x >> 3)) * 32 + (row & 3) * 8, px + r * 8, rowBytes);
    }
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
                getBits(d, d.bits & 7);  // next byte boundary
                d.comp[0].pred = d.comp[1].pred = d.comp[2].pred = 0;
            }
        }
    }
}

}  // namespace

extern "C" {

BOOL THPInit(void) { return TRUE; }

// `file` is one frame's video component.  The THP player passes the size of
// the whole frame buffer only implicitly, so the scan is bounded by the
// largest THP frame the SDK accepts.
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
        if (*c++ != 0xFF) {
            return kErrBadSyntax;
        }
        while (*c == 0xFF) {
            c++;
        }
        u8 marker = *c++;
        s32 status = 0;
        switch (marker) {
        case 0xD8:  // SOI
            break;
        case 0xC0:  // SOF0
            status = readFrameHeader(d, c);
            break;
        case 0xC4:  // DHT
            status = readHuffman(d, c, end);
            break;
        case 0xDB:  // DQT
            status = readQuant(d, c, end);
            break;
        case 0xDD:  // DRI
            d.restartInterval = read16(c + 2);
            c += 4;
            break;
        case 0xDA:  // SOS: the entropy-coded data follows
            status = readScanHeader(d, c);
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
                c += read16(c);
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
