// Force-included into every decompiled translation unit (-include port/compat.h).
// Bridges CodeWarrior/PowerPC assumptions to a 64-bit little-endian Clang target.
#pragma once

#ifndef TARGET_PC
#define TARGET_PC 1
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
// Declare the C library's printf family (including bionic's FORTIFY inline
// wrappers) before the renames below, so the macros only affect game code.
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// 16-bit wide strings.  CodeWarrior's wchar_t is 16 bits and the game stores
// UTF-16 text in its data files; we build with -fshort-wchar, so the host
// libc's (32-bit) wide-string functions must not be used.
// ---------------------------------------------------------------------------
size_t port_wcslen(const wchar_t* s);
wchar_t* port_wcsncpy(wchar_t* dst, const wchar_t* src, size_t n);
wchar_t* port_wcscpy(wchar_t* dst, const wchar_t* src);
int port_wcscmp(const wchar_t* a, const wchar_t* b);
int port_wcsncmp(const wchar_t* a, const wchar_t* b, size_t n);
wchar_t* port_wcschr(const wchar_t* s, wchar_t c);
wchar_t* port_wcscat(wchar_t* dst, const wchar_t* src);
int port_swprintf(wchar_t* dst, size_t n, const wchar_t* fmt, ...);
int port_vswprintf(wchar_t* dst, size_t n, const wchar_t* fmt, va_list args);

#define wcslen port_wcslen
#define wcsncpy port_wcsncpy
#define wcscpy port_wcscpy
#define wcscmp port_wcscmp
#define wcsncmp port_wcsncmp
#define wcschr port_wcschr
#define wcscat port_wcscat
#define swprintf port_swprintf
#define vswprintf port_vswprintf

// CodeWarrior MSL printf semantics for game code (NULL "%s" prints nothing,
// "l" is 32-bit, "%ls" is 16-bit); see platform/src/port/msl_printf.cpp.
int port_vsnprintf(char* dst, size_t n, const char* fmt, va_list args);
int port_snprintf(char* dst, size_t n, const char* fmt, ...);
int port_vsprintf(char* dst, const char* fmt, va_list args);
int port_sprintf(char* dst, const char* fmt, ...);
#ifndef PORT_PLATFORM_CODE
#define vsnprintf port_vsnprintf
#define snprintf port_snprintf
#define vsprintf port_vsprintf
#define sprintf port_sprintf
#endif

// Renderer hooks called from game code (platform/src/gx/gx_recorder.cpp):
// markers around HUD drawing and the main camera, both kept in order with
// the GX commands issued before them.
void port_gx_marker(unsigned int kind);
// Frame-time statistics (see port/port.h).
void port_perf_frame_begin(void);
void port_perf_frame_work_done(void);
void port_gx_camera(const float* camera);  // PORT_GX_CAMERA_WORDS words
// Called where the game steers with the Wii remote's tilt (Star Ball, Ray
// surfing).  While it keeps being called, the controller's orientation feeds
// the accelerometer; otherwise the remote reads as held level and still, so
// ordinary pointing never registers as a shake.  neutralPitchDeg: remote
// pitch (0 = level, 90 = pointing up) that a level-held controller stands
// for (platform/src/input/wpad.cpp).
void port_input_use_tilt(float neutralPitchDeg);
#define PORT_GX_MARK_HUD_BEGIN 1
#define PORT_GX_MARK_HUD_END 2
#define PORT_GX_MARK_SKY_BEGIN 3  // skies drawn around the game camera
#define PORT_GX_MARK_SKY_END 4
#define PORT_GX_MARK_PLAYER_BEGIN 5  // the player's model and what it carries (motion vectors of their own)
#define PORT_GX_MARK_PLAYER_END 6
#define PORT_GX_MARK_SCENE_DEPTH 7  // the 3D scene's depth is complete (image effects and a Z clear follow)
#define PORT_GX_MARK_POINTER_BEGIN 8  // the pointer's 2D cursor (inside the HUD markers)
#define PORT_GX_MARK_POINTER_END 9
#define PORT_GX_CAMERA_WORDS 33
// Word 25 of the camera record: presentation hints.
#define PORT_GX_CAMERA_DIORAMA 1     // gameplay: show the scene as a VR diorama
#define PORT_GX_CAMERA_POINTER_UI 2  // the Wii pointer is used on 2D menus
#define PORT_GX_CAMERA_PLAYER 4      // words 27-29 hold the player's centre, 30-32 its up (against gravity), in world space
#define PORT_GX_CAMERA_OCCLUDED 8    // level geometry lies between the VR eye and the player
#define PORT_GX_CAMERA_GROUNDED 16   // the player stands on the ground (not jumping, falling or swimming)
#define PORT_GX_CAMERA_CENTRE_PLAYER 32  // the diorama centres the player (else the point the camera watches)

// The headset's view in game world space, for object culling while the VR
// camera is active: a camera-to-world matrix (3x4 rows; columns are the
// left, up and forward axes and the position) and the tangents of the half
// field of view.  Returns 0 when not rendering in VR.
int port_vr_cull_view(float* cameraMtx, float* tanHalfX, float* tanHalfY);
// The diorama's axes in game world space while it is shown: right, up
// (gravity at the camera's focus) and forward (away from the player).
// Mario's movement follows them instead of the game camera's screen axes,
// which differ once the camera turns upside down: the diorama always shows
// the world upright.  Returns 0 when the diorama is not shown.
int port_vr_move_axes(float* right, float* up, float* forward);
// Nonzero while the player aims the pointer into the VR diorama; the game's
// 2D cursor is hidden then (the VR layer draws a laser and reticle).
int port_vr_pointer_in_world(void);
// The pointing controller's aim ray in game world space (origin[3] and unit
// dir[3]) while it points into the diorama; returns 0 otherwise.  Star
// pointer targets are hit-tested against it instead of the 2D cursor.
int port_vr_pointer_ray(float* origin, float* dir);
// How far along that ray the pointer reaches (game units): its first map
// surface, or open space where star bits aim past the player.  Reported each
// frame; the VR layer draws the laser that long.
void port_vr_pointer_reach(float distance);
// How far in front of the game camera the point under the pointer's 2D
// cursor lies (game units; the first map surface on the camera's ray through
// it, or the far plane).  Reported each frame; the stereoscopic screen draws
// the cursor at that depth, on what it points at.
void port_vr_pointer_depth(float depth);
// Called each frame the game's pointer touches a target (`id` identifies it);
// the VR layer pulses the reticle and ticks the controller on a new touch.
void port_vr_pointer_touched(unsigned long long id);
// Screen wipes, reported each frame one is drawn: kind 0 = fade to `rgb`,
// 1 = ring closing on the player, 2 = ring closing on the screen centre;
// closed = 0 (open) .. 1 (covered).  While the VR diorama is shown
// (port_vr_diorama), the VR layer draws the wipe across the whole view and
// the game skips its 2D version, which would only cover the HUD panel.
void port_vr_wipe(int kind, float closed, unsigned int rgb);
int port_vr_diorama(void);
// Scripted transits: launch and sling stars report themselves from the
// launch until they let go of Mario, pipes from Mario stepping in until he
// jumps out.  The game camera follows those with its own framing (a flight
// across space, or the whole planet a pipe runs through), so while the
// player is still bound to that object the VR layer shows the game camera
// on the virtual screen instead of the diorama.
void port_vr_transit_begin(const void* host);
void port_vr_transit_end(const void* host);  // only if `host` began it
const void* port_vr_transit_host(void);
// The pause menu reports each frame whether its buttons wait for a choice;
// meanwhile the VR layer shows its settings panel beside it.
void port_vr_pause_menu(int selecting);

// Holding A skips cutscenes (platform/src/port/cutscene_skip.cpp).  The game
// reports each update which kind of cutscene runs; once A has been held long
// enough, port_skip_take() returns true once for that kind (movies, the
// galaxy intro camera and fly-in end through their own skip paths), and an
// in-engine demo or a dialogue is fast-forwarded (port_skip_fast_forwarding)
// until it ends.  port_skip_interrupt(): a choice waits for the player; it
// stops a fast-forward (returns 1 then: this update's A was not the
// player's).
#define PORT_SKIP_NONE 0
#define PORT_SKIP_MOVIE 1    // a THP movie
#define PORT_SKIP_OPENING 2  // the camera tour when a galaxy starts
#define PORT_SKIP_STARTER 3  // Mario flying in to a galaxy
#define PORT_SKIP_DEMO 4     // an in-engine cutscene
#define PORT_SKIP_TALK 5     // a dialogue outside a cutscene (most people's talk)
void port_skip_context(int kind);
int port_skip_take(int kind);
int port_skip_fast_forwarding(void);
int port_skip_interrupt(void);
// A scene change began during a fast-forward (the skipped cutscene ends in
// another stage, or in the star select): the fast-forward stops, and the
// change runs at the normal pace, drawn.
void port_skip_scene_change(void);
// The VR controller's menu buttons (X, Menu) ask for the pause menu with a
// single press (wpad.cpp): the game takes the request at the first moment it
// allows the pause, within 1.5 s of the press (port_input_take_pause_request
// returns 1 once), and drops requests made before its pause menu closed.
void port_input_request_pause(void);
int port_input_take_pause_request(void);
void port_input_discard_pause_request(void);
// The invert_camera setting: the D-pad's left and right swapped where they
// turn the game camera round Mario (and nowhere else: pages and menus keep
// theirs), so pushing the right stick right turns the view to the right.
int port_input_camera_inverted(void);
int port_input_real_a(void);
int port_input_real_z(void);
// Display aspect ratio query for native Hor+ ultrawide rendering
float port_get_display_aspect(void);
int port_is_fullscreen_aspect(void);
int port_is_fly_mode(void);
int port_is_skip_cutscenes(void);
int port_is_infinite_health(void);
int64_t port_host_time_ns(void);
void port_host_sleep_ns(int64_t ns);
// Delivers pending interrupts (a safe point; only from the thread holding the
// CPU).  Interrupts are otherwise taken when they are re-enabled, in blocking
// OS calls and when the CPU idles, so a loop that spins until an interrupt
// handler changes something must call this (on the console the interrupt
// would simply preempt it).
void port_irq_poll(void);

// Logging / fatal errors from the port layer.
void port_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void port_fatal(const char* fmt, ...) __attribute__((format(printf, 1, 2), noreturn));

#ifdef __cplusplus
}
#endif

// CodeWarrior-only keywords / pragmas that may leak into headers.
#ifndef _WIN32
#define __declspec(x)
#endif
#define __option(x) 0

#ifdef __cplusplus
#include "port/msl_ext.hpp"
#endif

// MSL <cmath> helpers (MSL's M_PI is a float literal).
#define DEG_TO_RAD(degrees) (degrees * (3.14159265358979323846f / 180.0f))
#define RAD_TO_DEG(radians) (radians * (180.0f / 3.14159265358979323846f))
