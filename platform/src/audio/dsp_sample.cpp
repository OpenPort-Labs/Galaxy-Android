#include "dsp_internal.h"

enum Source : u16 {
    SRC_SQUARE = 0,
    SRC_SAW = 1,
    SRC_SQUARE_25 = 3,
    SRC_PATTERN_1 = 4,
    SRC_AFC_LQ = 5,
    SRC_PATTERN_0 = 7,
    SRC_PCM8 = 8,
    SRC_AFC_HQ = 9,
    SRC_PATTERN_0_VAR = 10,
    SRC_PATTERN_2 = 11,
    SRC_PATTERN_3 = 12,
    SRC_PCM16 = 16,
};

static void decodeAfc(Voice* v, s16* dst, u32 blocks) {
    u32 bytes = v->_100;
    const u8* src = aram(v->_70, blocks * bytes);
    v->_70 += blocks * bytes;
    s16* hist = (s16*)v->_B0;
    s32 yn1 = hist[15], yn2 = hist[14];
    for (u32 b = 0; b < blocks; b++) {
        if (!src) {
            for (int i = 0; i < 16; i++) *dst++ = 0;
            continue;
        }
        s32 scale = 1 << ((src[0] >> 4) & 0xF);
        u32 idx = src[0] & 0xF;
        src++;
        s16 nib[16];
        if (bytes == 9) {
            for (int i = 0; i < 16; i += 2) {
                nib[i] = (s16)((s16)((src[0] >> 4) << 12) >> 1);
                nib[i + 1] = (s16)((s16)((src[0] & 0xF) << 12) >> 1);
                src++;
            }
        } else {
            for (int i = 0; i < 16; i += 4) {
                for (int k = 0; k < 4; k++) nib[i + k] = (s16)((s16)(((src[0] >> (6 - 2 * k)) & 3) << 14) >> 1);
                src++;
            }
        }
        for (int i = 0; i < 16; i++) {
            s32 s = (scale * nib[i] + yn1 * M.afcCoeffs[idx * 2] + yn2 * M.afcCoeffs[idx * 2 + 1]) >> 11;
            s = clamp16(s);
            *dst++ = (s16)s;
            yn2 = yn1;
            yn1 = s;
        }
    }
    hist[14] = (s16)yn2;
    hist[15] = (s16)yn1;
}

static void downloadAfc(Voice* v, s16* dst, u32 count) {
    s16* cache = (s16*)v->_B0;
    if (v->_8) {
        cache[15] = cache[14] = 0;
        v->_64 = 0;
        v->_74 = v->_114;
        v->_70 = v->_118;
    }
    if (v->mIsFinished) {
        memset(dst, 0, count * 2);
        return;
    }
    if (v->_64 > 16) v->_64 = 16;
    for (;;) {
        u32 fromCache = v->_64 < count ? v->_64 : count;
        const s16* base = cache + (16 - v->_64);
        for (u32 i = 0; i < fromCache; i++) *dst++ = base[i];
        v->_64 -= (u16)fromCache;
        count -= fromCache;
        if (!count) return;

        if (count <= v->_74) {
            u32 blocks = (count + 15) >> 4;
            u32 decoded = blocks << 4;
            if (decoded < v->_74) {
                v->_64 = (u16)(decoded - count);
                v->_74 -= decoded;
            } else {
                v->_64 = (u16)(v->_74 - count);
                v->_74 = 0;
            }
            s16 tmp[0x520];
            decodeAfc(v, tmp, blocks);
            memcpy(dst, tmp, count * 2);
            if (v->_64) {
                memcpy(cache, tmp + decoded - 16, 32);
                if (!v->_74 && v->_114) {
                    const s16* src = cache + ((v->_114 + 15) & 15);
                    for (u32 i = 0; i < v->_64; i++) cache[15 - i] = *src--;
                }
            }
            return;
        }

        if (v->_74) {
            u32 left = v->_74;
            s16 tmp[0x520];
            decodeAfc(v, tmp, (left + 15) >> 4);
            memcpy(dst, tmp, left * 2);
            dst += left;
            count -= left;
            v->_74 = 0;
        }
        if (!v->_102) {
            v->mIsFinished = 1;
            memset(dst, 0, count * 2);
            return;
        }
        u32 loopStart = v->_110;
        v->_70 = v->_118 + (loopStart >> 4) * v->_100;
        cache[14] = v->_106;
        cache[15] = v->_104;
        decodeAfc(v, cache, 1);
        v->_64 = (u16)(16 - (loopStart & 15));
        v->_74 = v->_114 - v->_64 - loopStart;
    }
}

template <typename T>
static void downloadPcm(Voice* v, s16* dst, u32 count) {
    if (v->mIsFinished) {
        memset(dst, 0, count * 2);
        return;
    }
    if (v->_8) {
        v->_74 = v->_114 - v->_68;
        v->_70 = v->_118 + v->_68 * (u32)sizeof(T);
    }
    u16* w = words(v);
    w[W_END_REACHED] = 0;
    while (count) {
        if (w[W_END_REACHED]) {
            w[W_END_REACHED] = 0;
            if (!v->_102) {
                memset(dst, 0, count * 2);
                v->mIsFinished = 1;
                return;
            }
            v->_68 = v->_110;
            v->_74 = v->_114 - v->_68;
            v->_70 = v->_118 + v->_68 * (u32)sizeof(T);
        }
        u32 n = v->_74 < count ? v->_74 : count;
        const u8* src = aram(v->_70, n * (u32)sizeof(T));
        for (u32 i = 0; i < n; i++) {
            if (!src) {
                *dst++ = 0;
            } else if (sizeof(T) == 1) {
                *dst++ = (s16)((s8)src[i] << 8);
            } else {
                *dst++ = be16(src + i * 2);
            }
        }
        v->_74 -= n;
        v->_70 += n * (u32)sizeof(T);
        count -= n;
        if (!v->_74) w[W_END_REACHED] = 1;
    }
}

static void resample(Voice* v, const s16* src, s16* dst) {
    u32 ratio = v->mPitch, pos = (u16)v->_60;
    if ((ratio >> 12) >= 4) {
        for (int i = 0; i < N; i++) {
            pos += ratio;
            dst[i] = src[pos >> 12];
        }
    } else {
        for (int i = 0; i < N; i++) {
            const s16* c = &M.resCoeffs[((pos & 0xFFF) >> 6) * 4];
            const s16* in = &src[pos >> 12];
            s64 acc = 0;
            for (int k = 0; k < 4; k++) acc += (s64)2 * c[k] * in[k];
            dst[i] = clamp16((s32)(acc >> 16));
            pos += ratio;
        }
    }
    for (int k = 0; k < 4; k++) v->_78[k] = src[(pos >> 12) + k];
    v->_66 = dst[N - 1];
    v->_60 = (s16)(pos & 0xFFF);
}

void loadInput(Voice* v, s16* out) {
    if (v->mPauseFlag) {
        for (int i = 0; i < N; i++) out[i] = v->_66;
        return;
    }
    s16 raw[4 + 0x500 + 0x20];
    for (int k = 0; k < 4; k++) raw[k] = v->_78[k];
    u32 needed = ((u32)(u16)v->_60 + (u32)N * v->mPitch) >> 12;
    if (needed > 0x500 + 0x10) needed = 0x500 + 0x10;
    switch (v->_100) {
    case SRC_SQUARE:
    case SRC_SQUARE_25: {
        u32 shift = v->_100 == SRC_SQUARE ? 1 : 2, mask = (1u << shift) - 1;
        u32 ratio = (u32)v->mPitch << (shift - 1), pos = (u32)(u16)v->_60 << shift;
        for (int i = 0; i < N; i++) {
            out[i] = ((pos >> 16) & mask) ? (s16)0xC000 : 0x4000;
            pos += ratio;
        }
        v->_60 = (s16)((pos >> shift) & 0xFFFF);
        return;
    }
    case SRC_SAW: {
        u32 pos = (u16)v->_60;
        for (int i = 0; i < N; i++) {
            out[i] = (s16)(pos & 0xFFFF);
            pos += v->mPitch >> 1;
        }
        v->_60 = (s16)(pos & 0xFFFF);
        return;
    }
    case SRC_PATTERN_0:
    case SRC_PATTERN_0_VAR:
    case SRC_PATTERN_1:
    case SRC_PATTERN_2:
    case SRC_PATTERN_3: {
        int idx = v->_100 == SRC_PATTERN_1 ? 1 : v->_100 == SRC_PATTERN_2 ? 2 : v->_100 == SRC_PATTERN_3 ? 3 : 0;
        const s16* pattern = M.patterns + idx * 0x40;
        u32 pos = (u32)(u16)v->_60 << 6, step = (u32)v->mPitch << 5;
        for (int i = 0; i < N; i++) {
            out[i] = pattern[pos >> 16];
            pos = (pos + step) % (0x40u << 16);
            if (v->_100 == SRC_PATTERN_0_VAR) pos = ((pos << 10) + M.br[i] * v->mPitch) >> 10;
        }
        v->_60 = (s16)(pos >> 6);
        return;
    }
    case SRC_PCM8:
        downloadPcm<s8>(v, raw + 4, needed);
        break;
    case SRC_AFC_HQ:
    case SRC_AFC_LQ:
        downloadAfc(v, raw + 4, needed);
        break;
    case SRC_PCM16:
        downloadPcm<s16>(v, raw + 4, needed);
        break;
    default:
        memset(out, 0, N * 2);
        return;
    }
    resample(v, raw, out);
}
