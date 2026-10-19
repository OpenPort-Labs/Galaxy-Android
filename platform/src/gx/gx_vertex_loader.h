#pragma once

#include "gpu.h"
#include "gx_record.h"

namespace gpu {

struct ArrayUse {
    u32 base[12], stride[12], elemBytes[12], lo[12], hi[12];
    void reset() {
        for (int i = 0; i < 12; i++) {
            lo[i] = ~0u;
            hi[i] = 0;
        }
    }
};

u32 loadVertices(WordBuffer& out, const u8* data, u32 count, int vat, u32 stride);
u32 loadVerticesTracked(WordBuffer& out, const u8* data, u32 count, int vat, u32 stride, ArrayUse* use);

}  // namespace gpu
