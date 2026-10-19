// CodeWarrior MSL-compatible printf family for the decompiled game code.
//
// compat.h routes sprintf/snprintf/vsprintf/vsnprintf in game code here
// (platform code keeps bionic's).  Differences from bionic that the game
// relies on:
//   - a NULL "%s" argument prints nothing (bionic prints "(null)");
//   - "l" means 32 bits ("long" is 32-bit in the game; its format strings
//     still say %ld/%lu/%lx for values the port passes as int);
//   - "%ls" / "%lc" take 16-bit characters (-fshort-wchar).
// Each conversion is formatted by bionic's snprintf with an explicit,
// correctly sized argument.
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msl_conv.h"

namespace {

struct Out {
    char* dst;
    size_t cap;
    size_t len;

    void put(const char* s, size_t n) {
        if (dst && cap > 0 && len < cap - 1) {
            size_t room = cap - 1 - len;
            memcpy(dst + len, s, n < room ? n : room);
        }
        len += n;
    }
};

// Formats one conversion into `o` using `spec` (a complete bionic format).
template < typename T >
void emit(Out& o, const char* spec, T value) {
    char local[256];
    int n = snprintf(local, sizeof(local), spec, value);
    if (n < 0) {
        return;
    }
    if ((size_t)n < sizeof(local)) {
        o.put(local, (size_t)n);
        return;
    }
    char* big = (char*)malloc((size_t)n + 1);
    if (big) {
        snprintf(big, (size_t)n + 1, spec, value);
        o.put(big, (size_t)n);
        free(big);
    }
}

enum Len { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_BIG_L, LEN_SIZE };

}  // namespace

extern "C" int port_vsnprintf(char* dst, size_t cap, const char* fmt, va_list ap) {
    Out o = {dst, cap, 0};
    va_list args;
    va_copy(args, ap);

    const char* p = fmt;
    while (*p) {
        if (*p != '%') {
            const char* q = strchr(p, '%');
            size_t n = q ? (size_t)(q - p) : strlen(p);
            o.put(p, n);
            p += n;
            continue;
        }
        const char* start = p++;
        if (*p == '%') {
            o.put("%", 1);
            p++;
            continue;
        }

        // Rebuild the spec with '*' resolved and our own length modifier.
        char spec[48];
        size_t sl = 0;
        spec[sl++] = '%';
        while (*p && strchr("-+ #0", *p)) {
            if (sl < 8) {
                spec[sl++] = *p;
            }
            p++;
        }
        int precision = -1;
        if (*p == '*') {
            sl += (size_t)snprintf(spec + sl, sizeof(spec) - sl, "%d", va_arg(args, int));
            p++;
        } else {
            while (isdigit((unsigned char)*p)) {
                if (sl < 24) {
                    spec[sl++] = *p;
                }
                p++;
            }
        }
        if (*p == '.') {
            spec[sl++] = '.';
            p++;
            if (*p == '*') {
                precision = va_arg(args, int);
                sl += (size_t)snprintf(spec + sl, sizeof(spec) - sl, "%d", precision);
                p++;
            } else {
                precision = 0;
                while (isdigit((unsigned char)*p)) {
                    precision = precision * 10 + (*p - '0');
                    if (sl < 40) {
                        spec[sl++] = *p;
                    }
                    p++;
                }
            }
        }

        Len len = LEN_NONE;
        if (*p == 'h') {
            len = LEN_H;
            if (*++p == 'h') {
                len = LEN_HH;
                p++;
            }
        } else if (*p == 'l') {
            len = LEN_L;
            if (*++p == 'l') {
                len = LEN_LL;
                p++;
            }
        } else if (*p == 'L') {
            len = LEN_BIG_L;
            p++;
        } else if (*p == 'z' || *p == 't' || *p == 'j') {
            len = LEN_SIZE;
            p++;
        }

        char conv = *p;
        if (!conv) {
            o.put(start, (size_t)(p - start));
            break;
        }
        p++;

        switch (conv) {
        case 'd':
        case 'i': {
            long long v;
            if (len == LEN_LL || len == LEN_SIZE) {
                v = va_arg(args, long long);
            } else {
                v = va_arg(args, int);
                if (len == LEN_H) {
                    v = (short)v;
                } else if (len == LEN_HH) {
                    v = (signed char)v;
                }
            }
            memcpy(spec + sl, "lld", 4);
            emit(o, spec, v);
            break;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            unsigned long long v;
            if (len == LEN_LL || len == LEN_SIZE) {
                v = va_arg(args, unsigned long long);
            } else {
                v = va_arg(args, unsigned int);
                if (len == LEN_H) {
                    v = (unsigned short)v;
                } else if (len == LEN_HH) {
                    v = (unsigned char)v;
                }
            }
            spec[sl] = 'l';
            spec[sl + 1] = 'l';
            spec[sl + 2] = conv;
            spec[sl + 3] = 0;
            emit(o, spec, v);
            break;
        }
        case 'c': {
            int c = va_arg(args, int);
            if (len == LEN_L && c >= 0x80) {
                char utf[4];
                uint16_t w[2] = {(uint16_t)c, 0};
                msl_wide_to_utf8(w, 1, utf, sizeof(utf));
                spec[sl] = 's';
                spec[sl + 1] = 0;
                emit(o, spec, (const char*)utf);
            } else {
                spec[sl] = 'c';
                spec[sl + 1] = 0;
                emit(o, spec, c);
            }
            break;
        }
        case 's': {
            spec[sl] = 's';
            spec[sl + 1] = 0;
            if (len == LEN_L) {
                const uint16_t* w = va_arg(args, const uint16_t*);
                char utf[1024];
                if (w) {
                    msl_wide_to_utf8(w, precision, utf, sizeof(utf));
                } else {
                    utf[0] = 0;
                }
                emit(o, spec, (const char*)utf);
            } else {
                const char* s = va_arg(args, const char*);
                emit(o, spec, s ? s : "");
            }
            break;
        }
        case 'p': {
            spec[sl] = 'p';
            spec[sl + 1] = 0;
            emit(o, spec, va_arg(args, void*));
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            if (len == LEN_BIG_L) {
                spec[sl] = 'L';
                spec[sl + 1] = conv;
                spec[sl + 2] = 0;
                emit(o, spec, va_arg(args, long double));
            } else {
                spec[sl] = conv;
                spec[sl + 1] = 0;
                emit(o, spec, va_arg(args, double));
            }
            break;
        }
        case 'n': {
            void* dstp = va_arg(args, void*);
            if (len == LEN_H) {
                *(short*)dstp = (short)o.len;
            } else if (len == LEN_HH) {
                *(signed char*)dstp = (signed char)o.len;
            } else if (len == LEN_LL || len == LEN_SIZE) {
                *(long long*)dstp = (long long)o.len;
            } else {
                *(int*)dstp = (int)o.len;
            }
            break;
        }
        default:
            o.put(start, (size_t)(p - start));
            break;
        }
    }
    va_end(args);
    if (dst && cap > 0) {
        dst[o.len < cap ? o.len : cap - 1] = 0;
    }
    return (int)o.len;
}

extern "C" int port_snprintf(char* dst, size_t cap, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = port_vsnprintf(dst, cap, fmt, args);
    va_end(args);
    return n;
}

extern "C" int port_vsprintf(char* dst, const char* fmt, va_list args) {
    return port_vsnprintf(dst, (size_t)-1 >> 1, fmt, args);
}

extern "C" int port_sprintf(char* dst, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = port_vsnprintf(dst, (size_t)-1 >> 1, fmt, args);
    va_end(args);
    return n;
}
