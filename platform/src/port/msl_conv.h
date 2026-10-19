#pragma once
#include <stddef.h>
#include <stdint.h>

void msl_wide_to_utf8(const uint16_t* s, int maxChars, char* out, size_t cap);
