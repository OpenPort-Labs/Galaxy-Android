// Wii remote (WPAD) and KPAD emulation fed by the port input layer.
#include <string.h>

#include <atomic>
#include <mutex>

#include "port/input.h"
#include "port/port.h"
#include "revolution/kpad.h"
#include "revolution/wpad.h"

static std::mutex sInputMutex;
static PortPadState sPad[4];
static int sRumble[4];
static u32 sPrevHold[4];
static WPADConnectCallback sConnectCb[4];
static WPADExtensionCallback sExtCb[4];
static int sReportedConnected[4];
static int sReportedExtension[4];

extern "C" void port_input_request_pause(void);

extern "C" void port_input_set(int chan, const PortPadState* state) {
    if (chan < 0 || chan >= 4) {
        return;
    }
    std::lock_guard<std::mutex> lk(sInputMutex);
    const uint32_t kPlusMinus = 0x0010 | 0x1000;
    if (chan == 0 && state->connected && (state->buttons & kPlusMinus & ~sPad[0].buttons)) {
        port_input_request_pause();
    }
    sPad[chan] = *state;
}

extern "C" int port_input_rumble(int chan) { return (chan >= 0 && chan < 4) ? sRumble[chan] : 0; }

extern "C" int port_input_real_a(void) {
    std::lock_guard<std::mutex> lk(sInputMutex);
    return sPad[0].connected && (sPad[0].buttons & 0x0800) ? 1 : 0;
}

// A pause request: each press of + or - on controller 0 (the VR
// controllers' Menu and X), see compat.h.
static std::atomic<int64_t> sPauseRequestAt{0};

extern "C" void port_input_request_pause(void) { sPauseRequestAt.store(port_host_time_ns()); }

extern "C" int port_input_take_pause_request(void) {
    int64_t at = sPauseRequestAt.load();
    if (at == 0 || port_host_time_ns() - at > 1500000000ll) return 0;
    return sPauseRequestAt.compare_exchange_strong(at, 0) ? 1 : 0;
}

extern "C" void port_input_discard_pause_request(void) { sPauseRequestAt.store(0); }

// The camera's turns swapped (the invert_camera setting), see compat.h.
static std::atomic<int> sCameraInverted{0};

extern "C" void port_input_set_camera_inverted(int inverted) { sCameraInverted.store(inverted); }

extern "C" int port_input_camera_inverted(void) { return sCameraInverted.load(); }

static std::atomic<int64_t> sTiltUsedAt{0};
static std::atomic<float> sTiltNeutralPitch{0.0f};

extern "C" void port_input_use_tilt(float neutralPitchDeg) {
    sTiltNeutralPitch.store(neutralPitchDeg);
    sTiltUsedAt.store(port_host_time_ns());
}

// Gravity as the remote's accelerometer reads it, in g.  KPAD's axes: x left,
// y out of the button face, z where the remote points; held level, (0, -1, 0)
// (KPADReset).  The game turns these into its own (WPadAcceleration::update:
// x = -acc.x, y = acc.z, z = -acc.y).  While the game steers by tilt the
// remote is the right controller pitched up by the neutral angle, so holding
// the controller level gives the grip the game asks for; otherwise the remote
// is held level, and aiming the laser never moves it.
static Vec remoteGravity(const PortPadState& s) {
    Vec g = {0.0f, -1.0f, 0.0f};
    if (port_host_time_ns() - sTiltUsedAt.load() >= 500000000) {  // 0.5 s
        return g;
    }
    // A controller whose orientation is unknown counts as held level.
    Vec down = s.tilted ? Vec{s.downX, s.downY, s.downZ} : Vec{0.0f, -1.0f, 0.0f};
    float p = sTiltNeutralPitch.load() * (3.14159265f / 180.0f);
    float c = __builtin_cosf(p), sn = __builtin_sinf(p);
    g.x = -down.x;
    g.y = down.y * c - down.z * sn;
    g.z = down.z * c + down.y * sn;
    return g;
}

static PortPadState snapshot(int chan) {
    std::lock_guard<std::mutex> lk(sInputMutex);
    return sPad[chan];
}

// Reports connection and Nunchuk changes through the registered callbacks,
// as the Bluetooth stack does.  Runs on the game thread from KPADRead.
static void pollConnection(int chan, const PortPadState& s) {
    if (s.connected != sReportedConnected[chan]) {
        sReportedConnected[chan] = s.connected;
        if (!s.connected) {
            sReportedExtension[chan] = 0;
        }
        if (sConnectCb[chan]) {
            sConnectCb[chan](chan, s.connected ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER);
        }
    }
    if (s.connected && !sReportedExtension[chan] && sExtCb[chan]) {
        sReportedExtension[chan] = 1;
        sExtCb[chan](chan, WPAD_DEV_FREESTYLE);
    }
}

extern "C" {

// ---------------------------------------------------------------------------
// WPAD
// ---------------------------------------------------------------------------
s32 WPADProbe(s32 chan, u32* type) {
    PortPadState s = snapshot(chan);
    if (!s.connected) {
        if (type) *type = WPAD_DEV_NOT_FOUND;
        return WPAD_ERR_NO_CONTROLLER;
    }
    if (type) *type = WPAD_DEV_FREESTYLE;
    return WPAD_ERR_NONE;
}

void WPADControlMotor(s32 chan, u32 command) {
    if (chan >= 0 && chan < 4) {
        sRumble[chan] = command == WPAD_MOTOR_RUMBLE;
    }
}

s32 WPADControlSpeaker(s32 chan, u32, WPADCallback cb) {
    if (cb) cb(chan, WPAD_ERR_NONE);
    return WPAD_ERR_NONE;
}
// The remote speaker is emulated: the game's speaker PCM is captured at the
// encoder (WENCGetEncodeData) and mixed into the main audio output.
BOOL WPADCanSendStreamData(s32 chan) { return chan == 0 && snapshot(0).connected; }
s32 WPADSendStreamData(s32, void*, u16) { return WPAD_ERR_NONE; }
BOOL WPADIsSpeakerEnabled(s32 chan) { return chan == 0 && snapshot(0).connected; }
u8 WPADGetSpeakerVolume(void) { return 0x60; }
void WPADDisconnect(s32) {}

s32 WPADGetInfoAsync(s32 chan, WPADInfo* info, WPADCallback cb) {
    if (info) {
        memset(info, 0, sizeof(*info));
        info->battery = 4;
        info->led = 1u << chan;
        info->nearempty = 0;
    }
    if (cb) cb(chan, snapshot(chan).connected ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER);
    return WPAD_ERR_NONE;
}

u8 WPADGetSensorBarPosition(void) { return 0; }  // above the screen
u32 WPADGetWorkMemorySize(void) { return 0x10000; }
void WPADRegisterAllocator(WPADAlloc, WPADFree) {}
void WPADSetAutoSleepTime(u8) {}

WPADConnectCallback WPADSetConnectCallback(s32 chan, WPADConnectCallback cb) {
    WPADConnectCallback old = sConnectCb[chan & 3];
    sConnectCb[chan & 3] = cb;
    return old;
}

WPADExtensionCallback WPADSetExtensionCallback(s32 chan, WPADExtensionCallback cb) {
    WPADExtensionCallback old = sExtCb[chan & 3];
    sExtCb[chan & 3] = cb;
    return old;
}

// ---------------------------------------------------------------------------
// KPAD
// ---------------------------------------------------------------------------
void KPADInit(void) {}
void KPADReset(void) { memset(sPrevHold, 0, sizeof(sPrevHold)); }
void KPADSetAccParam(s32, f32, f32) {}
void KPADSetBtnRepeat(s32, f32, f32) {}
void KPADSetDistParam(s32, f32, f32) {}
void KPADSetHoriParam(s32, f32, f32) {}
void KPADSetPosParam(s32, f32, f32) {}
void KPADSetSensorHeight(s32, f32) {}

s32 KPADRead(s32 chan, KPADStatus samples[], u32 length) {
    if (chan < 0 || chan >= 4 || length == 0) {
        return 0;
    }
    PortPadState s = snapshot(chan);
    pollConnection(chan, s);
    KPADStatus* k = &samples[0];
    memset(k, 0, sizeof(*k));
    if (!s.connected) {
        k->wpad_err = WPAD_ERR_NO_CONTROLLER;
        k->dev_type = WPAD_DEV_NOT_FOUND;
        sPrevHold[chan] = 0;
        return 0;
    }
    u32 hold = s.buttons & 0xFFFF;
    if (chan == 0) {
        // While a cutscene is skipped the port presses A for the game, and
        // after a skip A reads as released until the player lets go
        // (cutscene_skip.cpp).
        int injected = port_skip_inject_a();
        if (injected >= 0) {
            hold = (hold & ~0x0800u) | (injected ? 0x0800u : 0u);
        }
    }
    k->hold = hold;
    k->trig = hold & ~sPrevHold[chan];
    k->release = sPrevHold[chan] & ~hold;
    sPrevHold[chan] = hold;

    Vec g = remoteGravity(s);
    k->acc.x = g.x + s.accX;
    k->acc.y = g.y + s.accY;
    k->acc.z = g.z + s.accZ;
    k->acc_value = __builtin_sqrtf(k->acc.x * k->acc.x + k->acc.y * k->acc.y + k->acc.z * k->acc.z);
    // The remote's pitch, as KPAD works it out (calc_acc_vertical, unsmoothed).
    float side = __builtin_sqrtf(k->acc.x * k->acc.x + k->acc.y * k->acc.y);
    float len = __builtin_sqrtf(side * side + k->acc.z * k->acc.z);
    k->acc_vertical.x = len > 0.0f ? side / len : 1.0f;
    k->acc_vertical.y = len > 0.0f ? -k->acc.z / len : 0.0f;
    k->pos.x = s.pointerX;
    k->pos.y = s.pointerY;
    k->horizon.x = 1.0f;
    k->horizon.y = 0.0f;
    k->dist = s.pointerDist > 0.0f ? s.pointerDist : 2.0f;
    k->dpd_valid_fg = s.pointerValid ? 2 : 0;
    k->dev_type = WPAD_DEV_FREESTYLE;
    k->wpad_err = WPAD_ERR_NONE;
    k->data_format = WPAD_FMT_FREESTYLE_ACC_DPD;
    auto& fs = k->ex_status.fs;
    fs.stick.x = s.stickX;
    fs.stick.y = s.stickY;
    // The Nunchuk is held level (same axes as the remote's).
    fs.acc.x = s.nunAccX;
    fs.acc.y = s.nunAccY - 1.0f;
    fs.acc.z = s.nunAccZ;
    fs.acc_value = __builtin_sqrtf(fs.acc.x * fs.acc.x + fs.acc.y * fs.acc.y + fs.acc.z * fs.acc.z);
    return 1;
}

}  // extern "C"
