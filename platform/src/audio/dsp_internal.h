#pragma once

#include <stdint.h>
#include <string.h>

#include "JSystem/JAudio2/JASDSPInterface.hpp"
#include "port/port.h"

extern "C" u32 port_dsp_varam_base(void);

typedef JASDsp::TChannel Voice;
const int N = 0x50;
typedef s16 Buffer[N];

inline s16 clamp16(s32 v) { return (s16)(v < -0x8000 ? -0x8000 : v > 0x7FFF ? 0x7FFF : v); }
inline s16 be16(const u8* p) { return (s16)((p[0] << 8) | p[1]); }
inline u16* words(Voice* v) { return (u16*)v; }

enum : int {
    W_END_REACHED = 0x05,
    W_CHANNELS = 0x08,
    W_LOWPASS_HIST = 0x68,
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

extern Mixer M;

const u8* aram(u32 offset, u32 size);
void addWithVolume(s16* dst, const s16* src, int count, u16 vol);
s32 addWithRamp(s16* dst, const s16* src, s32 vol, s32 step);
void scaleInPlace(s16* buf, u16 vol, int fracBits);
void loadInput(Voice* v, s16* out);
void addVoice(Voice* v);
