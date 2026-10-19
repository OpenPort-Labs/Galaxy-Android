#pragma once
#ifdef THP_STANDALONE
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

namespace thp {

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

extern const u8 kNaturalOrder[64 + 16];
extern const double kAanScale[8];

struct Huffman {
    enum { kLookBits = 9 };
    u16 look[1 << kLookBits];
    s32 maxCode[18];
    s32 valOffset[18];
    u8 values[256];
};

struct Component {
    u8 quant, dc, ac;
    s32 pred;
};

struct Decoder {
    float quant[4][64];
    Huffman huff[4];
    u8 validHuff;
    u8 validQuant;
    Component comp[3];
    u32 width, height;
    u32 restartInterval;
    const u8* p;
    const u8* end;
    u64 acc;
    s32 bits;
};

inline u32 read16(const u8* p) { return (u32)p[0] << 8 | p[1]; }
inline s32 extend(u32 v, int s) { return v < (1u << (s - 1)) ? (s32)v - (1 << s) + 1 : (s32)v; }

}  // namespace thp

