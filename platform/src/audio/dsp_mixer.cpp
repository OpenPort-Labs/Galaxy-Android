#include "dsp_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>

static std::mutex sMixerMutex;
Mixer M;

const u8* aram(u32 offset, u32 size) {
    u32 base = port_dsp_varam_base();
    if (!base || offset >= 0x2000000u || size > 0x2000000u - offset) return nullptr;
    return (const u8*)(uintptr_t)(base + offset);
}

void addWithVolume(s16* dst, const s16* src, int count, u16 vol) {
    for (int i = 0; i < count; i++) dst[i] = clamp16(dst[i] + clamp16(((s32)src[i] * (s32)vol) >> 15));
}

s32 addWithRamp(s16* dst, const s16* src, s32 vol, s32 step) {
    if (!vol && !step) return vol;
    for (int i = 0; i < N; i++) {
        dst[i] = clamp16(dst[i] + ((((s32)(vol >> 16)) * src[i]) >> 16));
        vol += step;
    }
    return vol;
}

void scaleInPlace(s16* buf, u16 vol, int fracBits) {
    for (int i = 0; i < N; i++) buf[i] = clamp16(((s32)buf[i] * (s32)vol) >> fracBits);
}

static s16* fxAccumulator(int line) {
    s16* bufs[4] = {M.rev0, M.rev1, M.flRev, M.frRev};
    return bufs[line];
}

static void fxBeforeFrame() {
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

static void fxAfterFrame() {
    if (!M.fx) return;
    for (int line = 0; line < 4; line++) {
        JASDsp::FxBuf& f = M.fx[line];
        if (!f._0 || !f._4 || !f._2) continue;
        memcpy(f._4 + (size_t)(M.fxIndex[line] % f._2) * N, fxAccumulator(line), N * 2);
        M.fxIndex[line] = (u16)((M.fxIndex[line] + 1) % f._2);
    }
}

static void preparePatterns() {
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

static void renderSubFrame(s16* outL, s16* outR, u16 outputVolume) {
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

static void logVoices() {
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
