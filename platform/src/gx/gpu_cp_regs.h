#pragma once
#include "gpu.h"

namespace gpu {

void writeCP(u8 reg, u32 value);
void writeXF(u32 addr, u32 value);
void writeBP(u32 cmd);
void loadIndexed(int array, u32 cmd);

}  // namespace gpu
