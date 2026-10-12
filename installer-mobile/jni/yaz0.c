/* Yaz0 decoder used by the offline cooker (loaded through ctypes).
 * Build: x86_64-w64-mingw32-clang -O2 -shared -o yaz0.dll yaz0.c */
#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

/* Returns the number of bytes written, or -1 if the input is malformed. */
EXPORT long long yaz0_decode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen) {
    size_t s = 16, d = 0;
    if (srcLen < 16) {
        return -1;
    }
    while (d < dstLen) {
        if (s >= srcLen) {
            return -1;
        }
        uint8_t code = src[s++];
        for (int bit = 0; bit < 8 && d < dstLen; bit++) {
            if (code & (0x80 >> bit)) {
                if (s >= srcLen) {
                    return -1;
                }
                dst[d++] = src[s++];
            } else {
                if (s + 1 >= srcLen) {
                    return -1;
                }
                uint8_t b1 = src[s], b2 = src[s + 1];
                s += 2;
                size_t dist = (((size_t)(b1 & 0x0F) << 8) | b2) + 1;
                size_t n = b1 >> 4;
                if (n == 0) {
                    if (s >= srcLen) {
                        return -1;
                    }
                    n = (size_t)src[s++] + 0x12;
                } else {
                    n += 2;
                }
                if (dist > d) {
                    return -1;
                }
                for (size_t i = 0; i < n && d < dstLen; i++, d++) {
                    dst[d] = dst[d - dist];
                }
            }
        }
    }
    return (long long)d;
}
