#include <atomic>
#include <stddef.h>
#include <stdint.h>

std::atomic<uint64_t> gPresentedFrames{0};

extern "C" {

// Diorama is never shown in standard flat-screen mobile mode.
int port_vr_diorama(void) {
    return 0;
}

// Flat-screen uses the engine's native camera viewing volume culling.
int port_vr_cull_view(float* cameraMtx, float* tanHalfX, float* tanHalfY) {
    (void)cameraMtx;
    (void)tanHalfX;
    (void)tanHalfY;
    return 0;
}

// Flat-screen uses the game camera's native screen axes for character movement.
int port_vr_move_axes(float* right, float* up, float* forward) {
    (void)right;
    (void)up;
    (void)forward;
    return 0;
}

// Standard 2D cursor is drawn on screen (not projected into a 3D VR diorama).
int port_vr_pointer_in_world(void) {
    return 0;
}

int port_vr_pointer_ray(float* origin, float* dir) {
    (void)origin;
    (void)dir;
    return 0;
}

void port_vr_pointer_reach(float distance) {
    (void)distance;
}

void port_vr_pointer_depth(float depth) {
    (void)depth;
}

void port_vr_pointer_touched(unsigned long long id) {
    (void)id;
}

// In flat-screen mode with port_vr_diorama() == 0, the game handles native 2D wipes.
void port_vr_wipe(int kind, float closed, unsigned int rgb) {
    (void)kind;
    (void)closed;
    (void)rgb;
}

static const void* sTransitHost = nullptr;

void port_vr_transit_begin(const void* host) {
    sTransitHost = host;
}

void port_vr_transit_end(const void* host) {
    if (sTransitHost == host) {
        sTransitHost = nullptr;
    }
}

const void* port_vr_transit_host(void) {
    return sTransitHost;
}

void port_vr_pause_menu(int selecting) {
    (void)selecting;
}

}  // extern "C"
