// Controller state handed from the XR/input layer to the Wii remote emulation.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortPadState {
    int connected;         // a Wii remote + Nunchuk is present
    uint32_t buttons;      // WPAD_BUTTON_* bits (remote and Nunchuk C/Z)
    float stickX, stickY;  // Nunchuk stick, -1..1 (right / up positive)
    float rightStickX, rightStickY; // Right analog stick (camera orbit / look), -1..1 (right / up positive)
    // Motion of the remote and the Nunchuk in g (shakes), 0 when held still.
    // The emulation adds gravity (wpad.cpp).  KPAD axes: x left, y out of the
    // button face, z where the remote points.
    float accX, accY, accZ;
    float nunAccX, nunAccY, nunAccZ;
    // Which way is down from the right controller, when `tilted`: a unit
    // vector in its axes (x right, y up, z where it aims).  The remote takes
    // this tilt while the game steers by tilt (port_input_use_tilt).
    int tilted;
    float downX, downY, downZ;
    int pointerValid;      // pointer is on screen
    float pointerX, pointerY;  // -1..1 screen space (right / down positive, like KPAD)
    float pointerDist;     // metres from the "sensor bar"
} PortPadState;

// Called by the input layer (any thread).
void port_input_set(int chan, const PortPadState* state);

// Query right analog stick deflection (-1..1).
void port_input_get_right_stick(int chan, float* x, float* y);

// Rumble requested by the game (0/1) for a channel; read by the input layer.
int port_input_rumble(int chan);

#ifdef __cplusplus
}
#endif
