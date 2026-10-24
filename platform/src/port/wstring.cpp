// 16-bit wchar_t string functions (the game is built with -fshort-wchar;
// the host C library's wide functions assume 32-bit wchar_t).
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

typedef uint16_t wch;

// Caps stop hostile formats from causing unbounded padding or buffer overflow.
static constexpr int kMaxFormatWidth = 4096;
static constexpr int kMaxFormatPrecision = 1024, kMaxNumericPrecision = 60;

extern "C" {

size_t port_wcslen(const wchar_t* s) {
    const wch* p = (const wch*)s;
    size_t n = 0;
    while (p[n]) {
        n++;
    }
    return n;
}

wchar_t* port_wcsncpy(wchar_t* dst, const wchar_t* src, size_t n) {
    wch* d = (wch*)dst;
    const wch* s = (const wch*)src;
    size_t i = 0;
    for (; i < n && s[i]; i++) {
        d[i] = s[i];
    }
    for (; i < n; i++) {
        d[i] = 0;
    }
    return dst;
}

wchar_t* port_wcscpy(wchar_t* dst, const wchar_t* src) {
    wch* d = (wch*)dst;
    const wch* s = (const wch*)src;
    while ((*d++ = *s++) != 0) {
    }
    return dst;
}

int port_wcscmp(const wchar_t* a, const wchar_t* b) {
    const wch* x = (const wch*)a;
    const wch* y = (const wch*)b;
    while (*x && *x == *y) {
        x++;
        y++;
    }
    return (int)*x - (int)*y;
}

int port_wcsncmp(const wchar_t* a, const wchar_t* b, size_t n) {
    const wch* x = (const wch*)a;
    const wch* y = (const wch*)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i] || x[i] == 0) {
            return (int)x[i] - (int)y[i];
        }
    }
    return 0;
}

wchar_t* port_wcschr(const wchar_t* s, wchar_t c) {
    const wch* p = (const wch*)s;
    for (;; p++) {
        if (*p == (wch)c) {
            return (wchar_t*)p;
        }
        if (*p == 0) {
            return nullptr;
        }
    }
}

wchar_t* port_wcscat(wchar_t* dst, const wchar_t* src) {
    port_wcscpy((wchar_t*)((wch*)dst + port_wcslen(dst)), src);
    return dst;
}

// Minimal vswprintf: flags (-0+ space #), width, precision, length (h l ll),
// conversions d i u x X o c s S ls p f g %.
int port_vswprintf(wchar_t* dstW, size_t n, const wchar_t* fmtW, va_list args) {
    wch* dst = (wch*)dstW;
    const wch* fmt = (const wch*)fmtW;
    size_t pos = 0;
    auto put = [&](wch c) {
        if (pos + 1 < n) {
            dst[pos] = c;
        }
        pos++;
    };

    while (*fmt) {
        if (*fmt != '%') {
            put(*fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            put('%');
            fmt++;
            continue;
        }
        bool left = false, zero = false, plus = false, space = false, alt = false;
        for (;; fmt++) {
            if (*fmt == '-') left = true;
            else if (*fmt == '0') zero = true;
            else if (*fmt == '+') plus = true;
            else if (*fmt == ' ') space = true;
            else if (*fmt == '#') alt = true;
            else break;
        }
        int width = 0;
        if (*fmt == '*') {
            width = va_arg(args, int);
            fmt++;
            if (width < 0) {
                left = true;
                width = (width == INT_MIN) ? kMaxFormatWidth : -width;
            }
            if (width > kMaxFormatWidth) width = kMaxFormatWidth;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                if (width < kMaxFormatWidth) width = width * 10 + (*fmt - '0');
                if (width > kMaxFormatWidth) width = kMaxFormatWidth;
                fmt++;
            }
        }
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(args, int);
                fmt++;
                if (prec < 0) prec = -1;
                else if (prec > kMaxFormatPrecision) prec = kMaxFormatPrecision;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    if (prec < kMaxFormatPrecision) prec = prec * 10 + (*fmt - '0');
                    if (prec > kMaxFormatPrecision) prec = kMaxFormatPrecision;
                    fmt++;
                }
            }
        }
        int lng = 0;  // 1 = l, 2 = ll, -1 = h
        if (*fmt == 'h') {
            lng = -1;
            fmt++;
            if (*fmt == 'h') fmt++;
        } else if (*fmt == 'l') {
            lng = 1;
            fmt++;
            if (*fmt == 'l') {
                lng = 2;
                fmt++;
            }
        }
        wch conv = *fmt ? *fmt++ : 0;

        // Build the converted text into a small buffer (narrow or wide).
        wch buf[512];
        int len = 0;
        bool numeric = false;
        bool negative = false;
        switch (conv) {
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            numeric = true;
            unsigned long long v;
            if (conv == 'd' || conv == 'i') {
                long long sv = lng == 2 ? va_arg(args, long long) : lng == 1 ? (long long)va_arg(args, long) : (long long)va_arg(args, int);
                if (lng == -1) sv = (short)sv;
                negative = sv < 0;
                v = negative ? (~(unsigned long long)sv + 1ull) : (unsigned long long)sv;
            } else {
                v = lng == 2 ? va_arg(args, unsigned long long) : lng == 1 ? (unsigned long long)va_arg(args, unsigned long) : (unsigned long long)va_arg(args, unsigned int);
                if (lng == -1) v = (unsigned short)v;
            }
            unsigned base = (conv == 'x' || conv == 'X') ? 16 : conv == 'o' ? 8 : 10;
            const char* digits = conv == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            wch tmp[64];
            int t = 0;
            do {
                tmp[t++] = (wch)digits[v % base];
                v /= base;
            } while (v);
            const int numPrec = prec > kMaxNumericPrecision ? kMaxNumericPrecision : prec;
            while (t < numPrec) {
                tmp[t++] = '0';
            }
            if (alt && base == 16) {
                tmp[t++] = conv;
                tmp[t++] = '0';
            }
            while (t) {
                buf[len++] = tmp[--t];
            }
            break;
        }
        case 'c':
            buf[len++] = (wch)va_arg(args, int);
            break;
        case 's':
        case 'S': {
            if (lng == 1 || conv == 'S') {
                const wch* s = va_arg(args, const wch*);
                if (!s) s = (const wch*)u"(null)";
                for (int i = 0; s[i] && (prec < 0 || i < prec) && len < 511; i++) {
                    buf[len++] = s[i];
                }
            } else {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                for (int i = 0; s[i] && (prec < 0 || i < prec) && len < 511; i++) {
                    buf[len++] = (wch)(unsigned char)s[i];
                }
            }
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(args, void*);
            wch tmp[32];
            int t = 0;
            do {
                tmp[t++] = (wch)"0123456789abcdef"[v % 16];
                v /= 16;
            } while (v);
            buf[len++] = '0';
            buf[len++] = 'x';
            while (t) buf[len++] = tmp[--t];
            break;
        }
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        case 'e':
        case 'E': {
            numeric = true;
            double d = va_arg(args, double);
            char nb[128];
            char f2[16];
            snprintf(f2, sizeof(f2), "%%.%d%c", prec < 0 ? 6 : prec, (char)conv);
            snprintf(nb, sizeof(nb), f2, d < 0 ? -d : d);
            negative = d < 0;
            for (int i = 0; nb[i] && len < 511; i++) buf[len++] = (wch)nb[i];
            break;
        }
        default:
            buf[len++] = '%';
            if (conv) buf[len++] = conv;
            break;
        }

        wch sign = 0;
        if (numeric) {
            if (negative) sign = '-';
            else if (plus) sign = '+';
            else if (space) sign = ' ';
        }
        int total = len + (sign ? 1 : 0);
        int pad = width > total ? width - total : 0;
        if (!left && !(zero && numeric && prec < 0)) {
            while (pad-- > 0) put(' ');
        }
        if (sign) put(sign);
        if (!left && zero && numeric && prec < 0) {
            while (pad-- > 0) put('0');
        }
        for (int i = 0; i < len; i++) put(buf[i]);
        if (left) {
            while (pad-- > 0) put(' ');
        }
    }
    if (n) {
        dst[pos < n ? pos : n - 1] = 0;
    }
    return (int)pos;
}

int port_swprintf(wchar_t* dst, size_t n, const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int r = port_vswprintf(dst, n, fmt, args);
    va_end(args);
    return r;
}

}  // extern "C"
