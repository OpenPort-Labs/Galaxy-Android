// High-level emulation of the DSP mixer JAudio2 drives on the Wii (the
// "Zelda" microcode family, in the variant Super Mario Galaxy uses: six
// mixing destinations per voice, positional "Dolby" mixing, voice samples
// read from the ARAM area in MEM2).
//
// Voices are JASDsp::TChannel blocks (0x180 bytes) the game fills in main
// memory; the microcode's names for their fields are in the comments below.
// Every sync command renders sub-frames of 0x50 samples (2.5 ms at 32 kHz):
// each active voice is decoded (AFC ADPCM / PCM8 / PCM16 / oscillators),
// resampled with a 4-tap filter, filtered, and mixed with volume ramps into
// the destination buffers; the front left/right mix is written to the
// game's output buffers.  Four FX lines add a filtered feedback delay.
//
// The field semantics follow the reverse engineering documented in
// Dolphin's Zelda microcode HLE.  Unlike the console, the voice blocks and
// output buffers here are native-endian (the CPU side is our code too);
// sample data and the coefficient tables keep their big-endian layout.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>

#include "JSystem/JAudio2/JASDSPInterface.hpp"
#include "port/port.h"

extern "C" u32 port_dsp_varam_base(void);

static std::mutex sMixerMutex;

namespace {

typedef JASDsp::TChannel Voice;
const int N = 0x50;
typedef s16 Buffer[N];

inline s16 clamp16(s32 v) { return (s16)(v < -0x8000 ? -0x8000 : v > 0x7FFF ? 0x7FFF : v); }
inline s16 be16(const u8* p) { return (s16)((p[0] << 8) | p[1]); }

// Voice block words (microcode names).
inline u16* words(Voice* v) { return (u16*)v; }
enum : int {
    W_END_REACHED = 0x05,
    W_CHANNELS = 0x08,          // 6 x {buffer id, target volume, current volume, flags}
    W_LOWPASS_HIST = 0x68,      // yn1, xn1
    W_SAMPLES_BEFORE_LOOP = 0x36,
};

struct Mixer {
    bool ready = false;
    u32 voiceCount = 0;
    Voice* voices = nullptr;
    JASDsp::FxBuf* fx = nullptr;
    s16 resCoeffs[0x100];
    s16 patterns[0x100];
    s16 sine[0x80];
    s16 afcCoeffs[0x20];

    Buffer fl, fr, bl, br;
    Buffer flRev, frRev, blRev, brRev, rev0, rev1;
    Buffer unk0, unk1, unk2;
    u16 fxIndex[4] = {0, 0, 0, 0};
    s16 fxLast8[4][8];

    s16* bufferFor(u16 id) {
        switch (id & 0xFFF0) {
        case 0x0D00: return fl;
        case 0x0D60: return fr;
        case 0x0F40: return bl;
        case 0x0CA0: return br;
        case 0x0E80: return flRev;
        case 0x0EE0: return frRev;
        case 0x0C00: return blRev;
        case 0x0C50: return brRev;
        case 0x0DC0: return rev0;
        case 0x0E20: return rev1;
        case 0x09A0: return unk0;
        case 0x0FA0: return unk1;
        case 0x0B00: return unk2;
        default: return nullptr;
        }
    }
};

Mixer M;

// ARAM offsets live in MEM2 (the "virtual ARAM" JAudio sets up).
const u8* aram(u32 offset, u32 size) {
    u32 base = port_dsp_varam_base();
    if (!base || offset >= 0x2000000u || size > 0x2000000u - offset) return nullptr;
    return (const u8*)(uintptr_t)(base + offset);
}

void addWithVolume(s16* dst, const s16* src, int count, u16 vol) {  // 1.15
    for (int i = 0; i < count; i++) dst[i] = clamp16(dst[i] + clamp16(((s32)src[i] * (s32)vol) >> 15));
}

// Mixes src into dst with a volume ramp (1.31 volume, per-sample step);
// returns the final volume.
s32 addWithRamp(s16* dst, const s16* src, s32 vol, s32 step) {
    if (!vol && !step) return vol;
    for (int i = 0; i < N; i++) {
        dst[i] = clamp16(dst[i] + ((((s32)(vol >> 16)) * src[i]) >> 16));
        vol += step;
    }
    return vol;
}

void scaleInPlace(s16* buf, u16 vol, int fracBits) {  // 1.15 (fracBits 15) or 4.12 (12)
    for (int i = 0; i < N; i++) buf[i] = clamp16(((s32)buf[i] * (s32)vol) >> fracBits);
}

// ---------------------------------------------------------------------------
// Sample sources
// ---------------------------------------------------------------------------
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

void decodeAfc(Voice* v, s16* dst, u32 blocks) {
    u32 bytes = v->_100;  // 9 (4-bit) or 5 (2-bit) bytes per 16 samples
    const u8* src = aram(v->_70, blocks * bytes);
    v->_70 += blocks * bytes;
    s16* hist = (s16*)v->_B0;  // yn2 = [14], yn1 = [15]
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

// Produces `count` raw AFC samples, handling the cached tail of the last
// decoded block, the end of the sound and looping (same bookkeeping as the
// microcode: remaining length _74, ARAM address _70, cached count _64).
void downloadAfc(Voice* v, s16* dst, u32 count) {
    s16* cache = (s16*)v->_B0;
    if (v->_8) {  // reset
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
                    // Keep the samples the loop will continue from.
                    const s16* src = cache + ((v->_114 + 15) & 15);
                    for (u32 i = 0; i < v->_64; i++) cache[15 - i] = *src--;
                }
            }
            return;
        }

        // More requested than left: finish the sound or loop.
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
void downloadPcm(Voice* v, s16* dst, u32 count) {
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

void resample(Voice* v, const s16* src, s16* dst) {
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
    if (v->mPauseFlag) {  // constant sample (silence while paused/stopping)
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

// ---------------------------------------------------------------------------
// Voice filters and mixing
// ---------------------------------------------------------------------------
void lowPass(Voice* v, s16* buf) {
    s16* hist = (s16*)(words(v) + W_LOWPASS_HIST);
    s32 yn1 = v->_8 ? 0 : hist[0], xn1 = v->_8 ? 0 : hist[1];
    s32 coeff = (u16)v->iir_filter_params[4];
    for (int i = 0; i < N; i++) {
        s32 xn0 = buf[i];
        s64 t = ((s64)(xn0 - xn1) * coeff >> 7) + yn1;
        s16 yn0 = clamp16((s32)(t < -0x8000 ? -0x8000 : t > 0x7FFF ? 0x7FFF : t));
        buf[i] = yn0;
        yn1 = yn0;
        xn1 = xn0;
    }
    hist[0] = (s16)yn1;
    hist[1] = (s16)xn1;
}

// Variable-length FIR over the input, with the previous 20 inputs kept in
// the voice block.
void fir(Voice* v, s16* buf, int taps) {
    if (taps > 24) taps = 24;
    const s16* coeff = v->fir_filter_params;  // runs on into the biquad area past 20 taps
    s16 x[20 + N];
    memcpy(x, v->_80, 40);
    memcpy(x + 20, buf, N * 2);
    for (int i = 0; i < N; i++) {
        s64 acc = 0;
        for (int k = 0; k < taps; k++) {
            int j = 20 + i - k;
            if (j >= 0) acc += (s64)coeff[k] * x[j];
        }
        buf[i] = clamp16((s32)(acc >> 15));
    }
    memcpy(v->_80, x + N, 40);
}

void biquad(Voice* v, s16* buf) {
    s16* hist = v->_A8;  // xn1, xn2, yn1, yn2
    const s16* c = v->iir_filter_params;  // bn1, bn2, an1, an2
    s32 xn1 = hist[0], xn2 = hist[1], yn1 = hist[2], yn2 = hist[3];
    for (int i = 0; i < N; i++) {
        s32 xn0 = buf[i];
        s64 t = (s64)c[0] * xn1 + (s64)c[1] * xn2 + (s64)c[2] * yn1 + (s64)c[3] * yn2;
        s16 yn0 = clamp16((s32)(t >> 15));
        buf[i] = yn0;
        xn2 = xn1;
        xn1 = xn0;
        yn2 = yn1;
        yn1 = yn0;
    }
    hist[0] = (s16)xn1;
    hist[1] = (s16)xn2;
    hist[2] = (s16)yn1;
    hist[3] = (s16)yn2;
}

void addVoice(Voice* v) {
    if (!v->mIsActive || v->mIsFinished) return;

    Buffer in;
    loadInput(v, in);

    if (v->iir_filter_params[4] != 0) lowPass(v, in);
    int taps = v->mFilterMode & 0x1F;
    if (taps > 1) fir(v, in, taps);
    const s16* c = v->iir_filter_params;
    if ((v->mFilterMode & 0x20) && (c[0] != 0x7FFF || c[1] || c[2] || c[3])) biquad(v, in);

    if (v->_58) {
        // Positional mixing: X (0 = right .. 0x7F = left) / Y (0 = back .. 0x7F = front).
        if (v->mForcedStop) {
            v->_56 = v->_54 / 2;
            if (v->_56 == 0) v->mIsFinished = 1;
        }
        u32 x = (v->_50 >> 8) & 0x7F, y = v->_50 & 0x7F;
        s16 right = M.sine[x], back = M.sine[y], left = M.sine[x ^ 0x7F], front = M.sine[y ^ 0x7F];
        const int sh = 15;  // SMG's microcode uses the louder variant
        s16 quad[4] = {(s16)((left * front) >> sh), (s16)((left * back) >> sh), (s16)((right * front) >> sh), (s16)((right * back) >> sh)};
        s16 delta = (s16)(v->_56 - v->_54);
        s16 deltas[4], rev[4], revDeltas[4];
        for (int i = 0; i < 4; i++) deltas[i] = (s16)(((u16)quad[i] * delta) >> sh);
        for (int i = 0; i < 4; i++) quad[i] = (s16)((quad[i] * v->_54) >> sh);
        for (int i = 0; i < 4; i++) {
            rev[i] = (s16)((quad[i] * v->_52) >> sh);
            revDeltas[i] = (s16)((deltas[i] * v->_52) >> sh);
        }
        s16* dsts[8] = {M.fl, M.bl, M.fr, M.br, M.flRev, M.blRev, M.frRev, M.brRev};
        for (int i = 0; i < 8; i++) {
            s16 vol = i < 4 ? quad[i] : rev[i - 4];
            s16 dv = i < 4 ? deltas[i] : revDeltas[i - 4];
            addWithRamp(dsts[i], in, (s32)vol << 16, ((s32)dv << 16) / N);
        }
        v->_54 = v->_56;
    } else {
        u16* ch = words(v) + W_CHANNELS;
        if (v->mForcedStop) {
            bool mute = true;
            for (int i = 0; i < 6; i++) {
                ch[i * 4 + 1] = (u16)((s16)ch[i * 4 + 2] / 2);
                mute &= ch[i * 4 + 1] == 0;
            }
            if (mute) v->mIsFinished = 1;
        }
        for (int i = 0; i < 6; i++) {
            u16* c4 = ch + i * 4;
            if (!c4[0]) continue;
            s16 target = (s16)c4[1], current = (s16)c4[2];
            s32 step = ((s32)(s16)(target - current) << 16) / N;
            if (!current && !step) continue;
            s16* dst = M.bufferFor(c4[0]);
            if (!dst) continue;
            s32 vol = addWithRamp(dst, in, (s32)current << 16, step);
            c4[2] = (u16)(s16)(vol >> 16);
        }
    }
    if (!v->mPauseFlag) v->_8 = 0;
}

// ---------------------------------------------------------------------------
// FX lines (filtered feedback delay in MRAM)
// ---------------------------------------------------------------------------
s16* fxAccumulator(int line) {
    s16* bufs[4] = {M.rev0, M.rev1, M.flRev, M.frRev};
    return bufs[line];
}

void fxBeforeFrame() {
    if (!M.fx) return;
    for (int line = 0; line < 4; line++) {
        JASDsp::FxBuf& f = M.fx[line];
        if (!f._0 || !f._4 || !f._2) continue;
        s16* ring = f._4 + (size_t)(M.fxIndex[line] % f._2) * N;
        s16 buf[8 + N];
        memcpy(buf, M.fxLast8[line], 16);
        memcpy(buf + 8, ring, N * 2);
        memcpy(M.fxLast8[line], buf + N, 16);
        auto filter = [&] {
            for (int i = 0; i < N; i++) {
                s32 acc = 0;
                for (int k = 0; k < 8; k++) acc += (s32)buf[i + k] * (s16)f._10[k];
                buf[i] = clamp16(acc >> 15);
            }
        };
        if (f._0 & 1) filter();
        struct {
            u16 id;
            s16 vol;
        } dests[2] = {{f._8, f._A}, {f._C, f._E}};
        for (auto& d : dests) {
            if (!d.id) continue;
            s16* dst = M.bufferFor(d.id);
            if (dst) addWithVolume(dst, buf, N, (u16)d.vol);
        }
        if (f._0 & 2) filter();
        memcpy(fxAccumulator(line), buf, N * 2);
    }
}

void fxAfterFrame() {
    if (!M.fx) return;
    for (int line = 0; line < 4; line++) {
        JASDsp::FxBuf& f = M.fx[line];
        if (!f._0 || !f._4 || !f._2) continue;
        memcpy(f._4 + (size_t)(M.fxIndex[line] % f._2) * N, fxAccumulator(line), N * 2);
        M.fxIndex[line] = (u16)((M.fxIndex[line] + 1) % f._2);
    }
}

void preparePatterns() {
    s16* p2 = M.patterns + 2 * 0x40;
    s32 yn2 = p2[0x3E], yn1 = p2[0x3F], v;
    for (int i = 0; i < 0x40; i += 2) {
        v = yn2 * yn1 - (p2[i] << 16);
        yn2 = yn1;
        yn1 = p2[i];
        p2[i] = (s16)(v >> 16);
        v = 2 * (yn2 * yn1 + (p2[i + 1] << 16));
        yn2 = yn1;
        yn1 = p2[i + 1];
        p2[i + 1] = (s16)(v >> 16);
    }
    s16* p3 = M.patterns + 3 * 0x40;
    yn2 = p3[0x3E];
    yn1 = p3[0x3F];
    s16 acc = (s16)yn1;
    s16 step = (s16)(p3[0] + ((yn1 * yn2 + ((yn2 << 16) + yn1)) >> 16));
    step = (s16)((step & 0x1FF) | 0x2000);
    for (int i = 0; i < 0x40; i++) p3[i] = (s16)(acc + (i + 1) * step);
}

void renderSubFrame(s16* outL, s16* outR, u16 outputVolume) {
    if (!outL || !outR || !M.ready || !M.voices) return;
    memset(M.fl, 0, sizeof(M.fl));
    memset(M.fr, 0, sizeof(M.fr));
    scaleInPlace(M.bl, 0x6784, 15);
    scaleInPlace(M.br, 0x6784, 15);
    fxBeforeFrame();
    addWithVolume(M.flRev, M.blRev, N, 0x7FFF);
    addWithVolume(M.frRev, M.blRev, N, 0xB820);
    addWithVolume(M.flRev, M.brRev + 0x28, 0x28, 0xB820);
    addWithVolume(M.frRev, M.brRev + 0x28, 0x28, 0x7FFF);
    memset(M.blRev, 0, sizeof(M.blRev));
    memset(M.brRev, 0, sizeof(M.brRev));
    preparePatterns();

    for (u32 i = 0; i < M.voiceCount; i++) addVoice(&M.voices[i]);

    scaleInPlace(M.fl, outputVolume, 12);
    scaleInPlace(M.fr, outputVolume, 12);
    memcpy(outL, M.fl, sizeof(M.fl));
    memcpy(outR, M.fr, sizeof(M.fr));
    fxAfterFrame();
}

// PETARI_DSPLOG: the voices playing, once a second (source, pitch, loop,
// then either the positional volume/pan or each destination's volume).
void logVoices() {
    char line[1024];
    int len = 0, count = 0;
    for (u32 i = 0; i < M.voiceCount; i++) {
        Voice* v = &M.voices[i];
        if (!v->mIsActive || v->mIsFinished) continue;
        count++;
        if (len > (int)sizeof(line) - 120) continue;
        len += snprintf(line + len, sizeof(line) - len, " [%u src%u p%04x%s%s%s", i, v->_100, v->mPitch, v->_102 ? " loop" : "",
                        v->mPauseFlag ? " paused" : "", v->mForcedStop ? " stop" : "");
        if (v->_58) {
            len += snprintf(line + len, sizeof(line) - len, " vol%d>%d pos%04x rev%d]", (s16)v->_54, (s16)v->_56, v->_50, v->_52);
        } else {
            u16* ch = words(v) + W_CHANNELS;
            for (int c = 0; c < 6; c++) {
                if (ch[c * 4] && (ch[c * 4 + 1] || ch[c * 4 + 2])) {
                    len += snprintf(line + len, sizeof(line) - len, " %03x:%d", ch[c * 4] >> 4, (s16)ch[c * 4 + 2]);
                }
            }
            len += snprintf(line + len, sizeof(line) - len, "]");
        }
    }
    port_log("dsp: %d voices%s", count, line);
}

}  // namespace

// Command 0x81 (table setup): voice blocks, resampling/pattern/sine table
// (JASDsp::DSPRES_FILTER, stored as big-endian 16-bit pairs), AFC
// coefficients (JASDsp::DSPADPCM_FILTER) and the FX line blocks.
extern "C" void port_dsp_setup(u32 voiceCount, u32 voices, u32 table, u32 afcTable, u32 fxBlocks) {
    std::lock_guard<std::mutex> lk(sMixerMutex);
    M.voiceCount = voiceCount > 0x100 ? 0x100 : voiceCount;
    M.voices = (Voice*)(uintptr_t)voices;
    M.fx = (JASDsp::FxBuf*)(uintptr_t)fxBlocks;
    const u32* t = (const u32*)(uintptr_t)table;
    s16 words16[0x280];
    for (int i = 0; i < 0x140; i++) {
        words16[i * 2] = (s16)(t[i] >> 16);
        words16[i * 2 + 1] = (s16)(t[i] & 0xFFFF);
    }
    memcpy(M.resCoeffs, words16, sizeof(M.resCoeffs));
    memcpy(M.patterns, words16 + 0x100, sizeof(M.patterns));
    memcpy(M.sine, words16 + 0x200, sizeof(M.sine));
    const u8* a = (const u8*)(uintptr_t)afcTable;
    for (int i = 0; i < 0x20; i++) M.afcCoeffs[i] = be16(a + i * 2);
    M.ready = voices != 0;
    port_log("dsp: mixer ready, %u voices at %#x, fx at %#x", voiceCount, voices, fxBlocks);
}

// Command 0x82 (sync frame): render `subFrames` blocks of 0x50 samples.
extern "C" void port_dsp_mix_frame(u32 subFrames, s16* outL, s16* outR, u16 mixerLevel) {
    std::lock_guard<std::mutex> lk(sMixerMutex);
    static const bool sLog = getenv("PETARI_DSPLOG") != nullptr;
    static u32 sLogSubFrames = 0;
    if (sLog && M.ready && (sLogSubFrames += subFrames) >= 400) {
        sLogSubFrames = 0;
        logVoices();
    }
    for (u32 f = 0; f < subFrames; f++) {
        if (M.ready && outL && outR) {
            renderSubFrame(outL + f * N, outR + f * N, mixerLevel);
        } else if (outL && outR) {
            memset(outL + f * N, 0, N * 2);
            memset(outR + f * N, 0, N * 2);
        }
    }
}
