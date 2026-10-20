#pragma once

#include <stdint.h>

namespace mobile_ui {

// In-shader vertex for 2D batching (Vulkan)
struct UIVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
    float mode;   // 0 = rect, 1 = circle/ring, 2 = font glyph, 3 = cogwheel, 4 = close 'X', etc.
    float param;  // ring: inner ratio; rect: corner radius ratio; cogwheel: rot angle
    float aspect; // rect aspect ratio (w/h or h/w)
    float extra;  // rect: 0 = horiz/square, 1 = vert
};

// Multi-touch pointer role assignment
enum class TouchRole {
    NONE,
    STICK,
    STAR_STICK,
    CAMERA_SWIPE,
    DPAD,
    BTN_A,
    BTN_B,
    BTN_Z,
    BTN_SPIN,
    BTN_PLUS,
    STAR_POINTER
};

struct TouchPoint {
    int32_t id = -1;
    TouchRole role = TouchRole::NONE;
    float startX = 0.0f;
    float startY = 0.0f;
    float lastX = 0.0f;
    float lastY = 0.0f;
    int64_t startTime = 0;
};

}  // namespace mobile_ui
