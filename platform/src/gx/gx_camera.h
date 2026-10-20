#pragma once
#include <stdint.h>

namespace gpu {

#ifndef _CAMERA_INFO_DEFINED_
#define _CAMERA_INFO_DEFINED_
struct CameraInfo {
    bool valid = false;
    float view[12];   // world -> view, 3x4 rows
    float pos[3];     // camera position (world)
    float watch[3];   // point the camera looks at (usually the player)
    float up[3];      // camera up vector
    float watchUp[3]; // up (anti-gravity) at the watch point
    float fovy;       // degrees
    uint32_t flags;   // PORT_GX_CAMERA_* hints
    float aspect;     // width / height of the game's projection
    float player[3];  // player centre (world), with PORT_GX_CAMERA_PLAYER
    float playerUp[3];  // against the player's gravity (world); zero while it has none
};
#endif

enum class HudMode {
    Inline,
    Target,
    Skip,
};

struct EyeView {
    float view[16];  // game view space -> eye space
    float proj[16];  // eye space -> clip space
    float focus[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float cut[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float eyePos[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int index = 0;   // 0 = left, 1 = right
    bool flatStereo = false;
    float stereo[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float pointer[2] = {0.0f, 0.0f};
};

}  // namespace gpu
