#include "msl_conv.h"
#include <string.h>

void msl_wide_to_utf8(const uint16_t* s, int maxChars, char* out, size_t cap) {
    size_t j = 0;
    for (int i = 0; s[i] && (maxChars < 0 || i < maxChars); i++) {
        uint32_t c = s[i];
        char tmp[3];
        size_t k;
        if (c < 0x80) {
            tmp[0] = (char)c;
            k = 1;
        } else if (c < 0x800) {
            tmp[0] = (char)(0xC0 | (c >> 6));
            tmp[1] = (char)(0x80 | (c & 0x3F));
            k = 2;
        } else {
            tmp[0] = (char)(0xE0 | (c >> 12));
            tmp[1] = (char)(0x80 | ((c >> 6) & 0x3F));
            tmp[2] = (char)(0x80 | (c & 0x3F));
            k = 3;
        }
        if (j + k >= cap) {
            break;
        }
        memcpy(out + j, tmp, k);
        j += k;
    }
    out[j] = 0;
}
