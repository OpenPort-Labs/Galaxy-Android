#include "thp_scan.h"
#include "thp_idct.h"

#include <string.h>

namespace thp {

namespace {

const int kLookBits = Huffman::kLookBits;

inline void fill(Decoder& d) {
    while (d.bits <= 56) {
        u64 b = d.p < d.end ? *d.p++ : 0;
        d.acc |= b << (56 - d.bits);
        d.bits += 8;
    }
}

inline u32 getBits(Decoder& d, int n) {
    if (n <= 0) return 0;
    u32 v = (u32)(d.acc >> (64 - n));
    d.acc <<= n;
    d.bits -= n;
    return v;
}

inline int decodeSymbol(Decoder& d, const Huffman& h) {
    u16 e = h.look[d.acc >> (64 - kLookBits)];
    if (e) {
        getBits(d, e >> 8);
        return e & 0xFF;
    }
    u32 code16 = (u32)(d.acc >> 48);
    for (int l = kLookBits + 1; l <= 16; l++) {
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
            if (k >= 64) break;
            coef[kNaturalOrder[k]] = extend(getBits(d, s), s);
            k++;
        } else if (r == 15) {
            k += 16;
        } else {
            break;
        }
    }
    return k < 64 ? k : 64;
}

}  // namespace

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

}  // namespace thp
