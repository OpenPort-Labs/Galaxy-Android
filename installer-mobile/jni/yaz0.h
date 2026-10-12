#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

long long yaz0_decode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen);

#ifdef __cplusplus
}
#endif
