#include "port/port.h"

// In Vulkan-only runtime, headless offscreen GL rendering is disabled.
extern "C" unsigned long long port_headless_render(const char* path, int scale) {
    (void)path;
    (void)scale;
    port_log("headless: offscreen GL rendering disabled in Vulkan-only runtime");
    return 0;
}

extern "C" void port_headless_xrsim(const char* path, int scale, float ipd, float fov) {
    (void)path;
    (void)scale;
    (void)ipd;
    (void)fov;
}
