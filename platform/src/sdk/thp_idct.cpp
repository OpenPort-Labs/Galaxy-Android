#include "thp_idct.h"
#include <string.h>

namespace thp {

const u8 kNaturalOrder[64 + 16] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63,
};

const double kAanScale[8] = {1.0, 1.387039845, 1.306562965, 1.175875602, 1.0, 0.785694958, 0.541196100, 0.275899379};

inline u8 toPixel(float v) {
    v = v * 0.125f + 128.5f;
    return v <= 0.0f ? 0 : v >= 255.0f ? 255 : (u8)v;
}

void idct(const s32* coef, int count, const float* q, u8* out) {
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

void storeBlock(u8* plane, u32 width, u32 height, u32 x, u32 y, const u8* px) {
    if (!plane || !px || x >= width || y >= height) {
        return;
    }
    u32 tilesPerRow = (width + 7) / 8;
    size_t rowBytes = (x + 8 <= width) ? 8 : (size_t)(width - x);
    for (u32 r = 0; r < 8 && y + r < height; r++) {
        u32 row = y + r;
        memcpy(plane + ((size_t)(row >> 2) * tilesPerRow + (x >> 3)) * 32 + (row & 3) * 8, px + r * 8, rowBytes);
    }
}

}  // namespace thp
