#pragma once

#include <cstring>
#include "gpu.h"
#include "gx_record.h"

namespace gpu {

struct VertexReader {
    const u8* p;
    bool bigEndian;

    u8 u8_() { return *p++; }
    u16 u16_() {
        u16 v = bigEndian ? (u16)((p[0] << 8) | p[1]) : (u16)(p[0] | (p[1] << 8));
        p += 2;
        return v;
    }
    u32 u32_() {
        u32 v = bigEndian ? be32(p) : ((u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24));
        p += 4;
        return v;
    }
    float comp(u32 fmt, float scale) {
        switch (fmt) {
        case 0: return u8_() * scale;
        case 1: return (s8)u8_() * scale;
        case 2: return u16_() * scale;
        case 3: return (s16)u16_() * scale;
        case 4: {
            u32 v = u32_();
            float f;
            memcpy(&f, &v, 4);
            return f;
        }
        default: return 0.0f;
        }
    }
    u32 color(u32 fmt) {
        switch (fmt) {
        case 0: {
            u16 c = u16_();
            u32 r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            return ((r << 3) | (r >> 2)) | (((g << 2) | (g >> 4)) << 8) | (((b << 3) | (b >> 2)) << 16) | 0xFF000000u;
        }
        case 1: {
            u32 r = u8_(), g = u8_(), b = u8_();
            return r | (g << 8) | (b << 16) | 0xFF000000u;
        }
        case 2: {
            u32 r = u8_(), g = u8_(), b = u8_();
            p++;
            return r | (g << 8) | (b << 16) | 0xFF000000u;
        }
        case 3: {
            u16 c = u16_();
            u32 r = (c >> 12) & 15, g = (c >> 8) & 15, b = (c >> 4) & 15, a = c & 15;
            return (r * 17) | ((g * 17) << 8) | ((b * 17) << 16) | ((a * 17) << 24);
        }
        case 4: {
            u32 v = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
            p += 3;
            u32 r = (v >> 18) & 63, g = (v >> 12) & 63, b = (v >> 6) & 63, a = v & 63;
            auto e = [](u32 x) { return (x << 2) | (x >> 4); };
            return e(r) | (e(g) << 8) | (e(b) << 16) | (e(a) << 24);
        }
        default: {
            u32 r = u8_(), g = u8_(), b = u8_(), a = u8_();
            return r | (g << 8) | (b << 16) | (a << 24);
        }
        }
    }
};

static inline u32 toBits(float f) {
    u32 v;
    memcpy(&v, &f, 4);
    return v;
}

static inline const u8* readComps(const u8* p, u32 fmt, bool bigEndian, u32 n, float scale, u32*& w) {
    switch (fmt) {
    case 0:
        for (u32 i = 0; i < n; i++) *w++ = toBits(p[i] * scale);
        return p + n;
    case 1:
        for (u32 i = 0; i < n; i++) *w++ = toBits((s8)p[i] * scale);
        return p + n;
    case 2:
        for (u32 i = 0; i < n; i++, p += 2) *w++ = toBits((bigEndian ? (u16)((p[0] << 8) | p[1]) : (u16)(p[0] | (p[1] << 8))) * scale);
        return p;
    case 3:
        for (u32 i = 0; i < n; i++, p += 2) *w++ = toBits((s16)(bigEndian ? ((p[0] << 8) | p[1]) : (p[0] | (p[1] << 8))) * scale);
        return p;
    case 4:
        for (u32 i = 0; i < n; i++, p += 4) {
            u32 v;
            memcpy(&v, p, 4);
            *w++ = bigEndian ? __builtin_bswap32(v) : v;
        }
        return p;
    default:
        for (u32 i = 0; i < n; i++) *w++ = 0;
        return p;
    }
}

}  // namespace gpu
