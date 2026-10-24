// Host-side check for the portable THP decoder (platform/src/sdk/thp_video.cpp):
//   thp_test <movie.thp> [frame] [out.ppm]
// Decodes frames of a disc (big-endian) or cooked (little-endian) THP file,
// compares the fast IDCT with a reference one, times the decoder and writes
// one frame, converted to RGB, as a PPM.
#define THP_STANDALONE 1
#define _USE_MATH_DEFINES
#include "../../platform/src/sdk/thp_idct.cpp"
#include "../../platform/src/sdk/thp_scan.cpp"
#include "../../platform/src/sdk/thp_video.cpp"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <chrono>
#include <vector>

static bool gBigEndian = true;

static u32 rd32(const u8* p) {
    return gBigEndian ? (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]
                      : (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0];
}

// Reference: the IDCT by its definition, in double precision.
static void referenceIdct(const s32* coef, const u8* quantZigzagNatural, u8* out) {
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            double sum = 0;
            for (int v = 0; v < 8; v++) {
                for (int u = 0; u < 8; u++) {
                    double cu = u ? 1.0 : sqrt(0.5), cv = v ? 1.0 : sqrt(0.5);
                    sum += cu * cv * coef[v * 8 + u] * quantZigzagNatural[v * 8 + u] * cos((2 * x + 1) * u * M_PI / 16) *
                           cos((2 * y + 1) * v * M_PI / 16);
                }
            }
            double p = sum / 4 + 128;
            out[y * 8 + x] = (u8)(p < 0 ? 0 : p > 255 ? 255 : floor(p + 0.5));
        }
    }
}

static void checkIdct() {
    u8 q[64];
    for (int i = 0; i < 64; i++) {
        q[i] = (u8)(2 + (i % 8) + (i / 8) * 2);
    }
    // Build the decoder's scaled table the way readQuant does.
    float fq[64];
    for (int row = 0, i = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++, i++) {
            fq[i] = (float)((double)q[i] * kAanScale[row] * kAanScale[col]);
        }
    }
    srand(1);
    int worst = 0;
    for (int t = 0; t < 20000; t++) {
        s32 coef[64] = {};
        int n = 1 + rand() % 64;
        for (int k = 0; k < n; k++) {
            int mag = k == 0 ? 60 : 30 / (1 + k / 4);
            coef[kNaturalOrder[k]] = rand() % (2 * mag + 1) - mag;
        }
        u8 a[64], b[64];
        idct(coef, n, fq, a);
        referenceIdct(coef, q, b);
        for (int i = 0; i < 64; i++) {
            int e = abs((int)a[i] - (int)b[i]);
            if (e > worst) worst = e;
        }
    }
    printf("idct: worst difference from the reference over 20000 blocks: %d\n", worst);
}

static u8 texel(const u8* tiles, u32 width, u32 x, u32 y) {
    u32 tilesPerRow = (width + 7) / 8;
    return tiles[((size_t)(y >> 2) * tilesPerRow + (x >> 3)) * 32 + (y & 3) * 8 + (x & 7)];
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: thp_test <movie.thp> [frame] [out.ppm]\n");
        return 1;
    }
    checkIdct();
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    int want = argc > 2 ? atoi(argv[2]) : 0;
    u8 h[0x30];
    fread(h, 1, sizeof(h), f);
    gBigEndian = h[5] == 0x01;  // version 0x00011000
    u32 numFrames = rd32(h + 0x14), firstSize = rd32(h + 0x18), compOff = rd32(h + 0x20), movieOff = rd32(h + 0x28);
    u8 comp[0x14 + 12];
    fseek(f, compOff, SEEK_SET);
    fread(comp, 1, sizeof(comp), f);
    u32 ncomp = rd32(comp);
    u32 width = rd32(comp + 0x14), height = rd32(comp + 0x18);
    printf("%s: %s-endian, %u frames, %ux%u, %u components\n", argv[1], gBigEndian ? "big" : "little", numFrames, width, height, ncomp);

    std::vector<u8> y((size_t)width * height), u((size_t)width * height / 4), v((size_t)width * height / 4), buf, work(64);
    u32 off = movieOff, size = firstSize;
    double totalMs = 0;
    int decoded = 0, maxFrames = want + 1 > 600 ? want + 1 : 600;
    for (int n = 0; n < (int)numFrames && n < maxFrames; n++) {
        buf.resize(size + 64);
        fseek(f, off, SEEK_SET);
        if (fread(buf.data(), 1, size, f) != size) {
            printf("short read at frame %d\n", n);
            break;
        }
        u32 next = rd32(buf.data());
        u32 payload = 8 + ncomp * 4;
        if (size < payload) {
            printf("frame %d: truncated frame header\n", n);
            return 1;
        }
        const u8* p = buf.data() + payload;
        THPSetInputSize(size - payload);
        THPSetOutputSize(width, height);
        auto t0 = std::chrono::steady_clock::now();
        s32 err = THPVideoDecode((void*)p, y.data(), u.data(), v.data(), work.data());
        totalMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        decoded++;
        if (err) {
            printf("frame %d: error %d\n", n, err);
            return 1;
        }
        if (n == want && argc > 3) {
            FILE* o = fopen(argv[3], "wb");
            fprintf(o, "P6\n%u %u\n255\n", width, height);
            for (u32 py = 0; py < height; py++) {
                for (u32 px = 0; px < width; px++) {
                    float Y = texel(y.data(), width, px, py);
                    float U = texel(u.data(), width / 2, px / 2, py / 2) - 128.0f;
                    float V = texel(v.data(), width / 2, px / 2, py / 2) - 128.0f;
                    float rgb[3] = {Y + 1.402f * V, Y - 0.344136f * U - 0.714136f * V, Y + 1.772f * U};
                    u8 out[3];
                    for (int i = 0; i < 3; i++) {
                        out[i] = (u8)(rgb[i] < 0 ? 0 : rgb[i] > 255 ? 255 : rgb[i] + 0.5f);
                    }
                    fwrite(out, 1, 3, o);
                }
            }
            fclose(o);
            printf("wrote frame %d to %s\n", n, argv[3]);
        }
        off += size;
        size = next;
    }
    printf("decoded %d frames, %.3f ms per frame\n", decoded, totalMs / decoded);
    return 0;
}
