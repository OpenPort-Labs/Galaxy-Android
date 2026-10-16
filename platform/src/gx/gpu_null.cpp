// Placeholder GPU backend used until the renderer is attached: consumes the
// command stream (keeping register state, sync and EFB-copy semantics) and
// counts presented frames.
#include <atomic>

#include "gpu.h"
#include "port/port.h"

namespace {
struct NullBackend : gpu::Backend {
    void draw(int, int, gpu::u32, const gpu::u8*, gpu::u32) override {}
    void bpWrite(gpu::u32, gpu::u32, gpu::u32) override {}
    void xfWrite(gpu::u32, gpu::u32) override {}
    void cpWrite(gpu::u32, gpu::u32) override {}
    void efbCopy(bool, bool) override {}
    void loadTlut(gpu::u32) override {}
    void preloadTexture() override {}
    gpu::u32 peekColor(gpu::u16, gpu::u16) override { return 0; }
    gpu::u32 peekZ(gpu::u16, gpu::u16) override { return 0x00FFFFFF; }
};
NullBackend sNull;
uint64_t sFrames;
}  // namespace

extern "C" void port_gx_use_null_backend(void) { gpu::setBackend(&sNull); }

extern std::atomic<uint64_t> gPresentedFrames;
extern "C" void port_gx_recorder_end_frame(void);

extern "C" void __attribute__((weak)) port_gx_present_xfb(void* xfb, int black) {
    port_gx_recorder_end_frame();
    sFrames++;
    gPresentedFrames.store(sFrames);
    if ((sFrames % 300) == 1) {
        // gDrawCount is incremented by the command processor for every backend,
        // so this reports real progress with the recorder attached as well.
        port_log("gx: frame %llu presented (xfb %p%s), %llu draws so far", (unsigned long long)sFrames, xfb, black ? ", black" : "",
                 (unsigned long long)gpu::gDrawCount);
    }
}
