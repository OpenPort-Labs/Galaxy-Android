#pragma once
#include <stdint.h>
#include "aes.h"

#define Nb 4

#if defined(AES256) && (AES256 == 1)
    #define Nk 8
    #define Nr 14
#elif defined(AES192) && (AES192 == 1)
    #define Nk 6
    #define Nr 12
#else
    #define Nk 4
    #define Nr 10
#endif

typedef uint8_t state_t[4][4];

#ifdef __cplusplus
extern "C" {
#endif

void Cipher(state_t* state, const uint8_t* RoundKey);
#if (defined(CBC) && CBC == 1) || (defined(ECB) && ECB == 1)
void InvCipher(state_t* state, const uint8_t* RoundKey);
#endif
void XorWithIv(uint8_t* buf, const uint8_t* Iv);

#ifdef __cplusplus
}
#endif
