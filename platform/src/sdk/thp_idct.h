#pragma once
#include "thp_types.h"

namespace thp {

void idct(const s32* coef, int count, const float* q, u8* out);
void storeBlock(u8* plane, u32 width, u32 height, u32 x, u32 y, const u8* px);

}  // namespace thp
