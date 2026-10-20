#pragma once

#include <cstdint>

namespace gpu {

// Per-frame shader data DrawRecord layout (words per distinct draw state).
enum : uint32_t {
    kRecPosSlots = 0,    // 22 slots: rows 0..65 of position/texture matrix memory, 3 rows each
    kRecNrmSlots = 22,   // 11 slots: normal matrix rows 0..32
    kRecPost = 33,       // 8: post-transform matrix per texgen
    kRecLights = 41,     // base of light state in st[]
    kRecProj = 42,       // base of projection state in st[]
    kRecPixel = 43,      // base of pixel state in st[]
    kRecMtxIdxA = 44,    // XF 0x18 (default matrix indices)
    kRecMtxIdxB = 45,    // XF 0x19
    kRecFlags = 46,      // bit 0: VR camera, bit 1: sky, bit 2: pointer cursor
    kRecWords = 48,
};

// Lighting uniform state: lights, material and ambient colors.
struct LightState {
    float lights[8][5][4];
    float matColor[2][4];
    float ambColor[2][4];
};

// Projection state: GX projection rows, depth params (zScale, zOffset).
struct ProjState {
    float proj[4][4];
    float depthParams[4];
};

// Pixel engine and texture uniform state.
struct PixelState {
    float tevReg[4][4];      // PREV, REG0, REG1, REG2
    float konst[4][4];       // K0..K3
    float alphaRef[4];       // ref0, ref1, Z texture bias (24-bit units)
    float fogColor[4];
    float fogParams[4];      // a, b, c, 0
    float fogRange[4];
    float texSize[8][4];     // LOD bias, height, 1/width, 1/height
    float indMtx[3][2][4];   // indirect matrices (rows, scaled)
    float efbSize[4];        // efb width/height for indirect/fog helpers
};

// VR / stereo eye uniform state.
struct alignas(16) EyeBlock {
    float vrView[4][4];      // game view space -> eye space (rows)
    float vrProj[4][4];      // eye space -> clip (rows)
    int32_t vrFlags[4];      // x: 1 rendering for a VR eye, 2 flat screen stereo pair
    float vrFocus[4];        // cutaway target (eye space), w > 0: enabled
    float vrCut[4];          // cutaway shape (see gpu::EyeView)
    float vrEyePos[4];       // the eye in game view space (lights at the camera move there)
    float vrStereo[4];       // the stereo pair's shift (see gpu::EyeView::stereo)
    float vrStereo2[4];      // x: pointer cursor's shift, y: 1 = cursor goes with HUD
};

}  // namespace gpu
