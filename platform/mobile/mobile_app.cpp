// Flat-screen Android Mobile application entry point (AOSP / Phones).
// Modularized into decoupled OOP components under ui/, system/, display/, input/, render/, and core/.

#include <android_native_app_glue.h>
#include <stdint.h>

#include "core/MobileApp.h"
#include "system/SystemManager.h"
#include "ui/UIOverlay.h"

extern "C" {

__attribute__((visibility("default"))) void port_android_main(
    struct android_app* app, uintptr_t windowBase, size_t windowSize) {
    mobile_core::MobileApp::get().run(app, windowBase, windowSize);
}

void port_android_vibrate(int ms) {
    mobile_system::SystemManager::triggerHaptic(
        mobile_core::MobileApp::get().androidApp(), ms);
}

void port_android_exit_to_launcher() {
    mobile_system::SystemManager::returnToLauncher(
        mobile_core::MobileApp::get().androidApp());
}

float port_get_display_aspect(void) {
    int w = mobile_core::MobileApp::get().screenWidth();
    int h = mobile_core::MobileApp::get().screenHeight();
    if (h > 0) {
        return (float)w / (float)h;
    }
    return 16.0f / 9.0f;
}

int port_is_fullscreen_aspect(void) {
    return mobile_ui::UIOverlay::get().settings().fullscreen ? 1 : 0;
}

int port_is_fly_mode(void) {
    return mobile_ui::UIOverlay::get().settings().modFly ? 1 : 0;
}

int port_is_skip_cutscenes(void) {
    return mobile_ui::UIOverlay::get().settings().modSkipCutscenes ? 1 : 0;
}

int port_is_infinite_health(void) {
    return mobile_ui::UIOverlay::get().settings().modInfiniteHealth ? 1 : 0;
}

int port_is_infinite_lives(void) {
    return mobile_ui::UIOverlay::get().settings().modInfiniteLives ? 1 : 0;
}

int port_is_infinite_starbits(void) {
    return mobile_ui::UIOverlay::get().settings().modInfiniteStarBits ? 1 : 0;
}

int port_is_shield_hd_textures(void) {
    return mobile_ui::UIOverlay::get().settings().modShieldTextures ? 1 : 0;
}

int port_is_shield_cutscenes(void) {
    return mobile_ui::UIOverlay::get().settings().modShieldCutscenes ? 1 : 0;
}

int port_is_shield_controls(void) {
    return mobile_ui::UIOverlay::get().settings().modShieldControls ? 1 : 0;
}

int port_is_classic_gamepad_ui(void) {
    return mobile_ui::UIOverlay::get().settings().modClassicGamepadUI ? 1 : 0;
}

int port_is_riivolution_mods(void) {
    return mobile_ui::UIOverlay::get().settings().modRiivolution ? 1 : 0;
}

}  // extern "C"
