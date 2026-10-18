// Definitions the original binary had (or never needed) that the decomp does
// not provide as out-of-line code.
#include <string.h>
#include "port/port.h"

#include "Game/Effect/ParticleEmitter.hpp"
#include "Game/System/StationedArchiveLoader.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "nw4r/lyt/pane.h"
#include "nw4r/ut/TextWriterBase.h"

// Base-class draw of the J3D packet hierarchy does nothing.
void J3DPacket::draw() {}

// Only derived conditions are ever instantiated; the base accepts everything.
bool StationedArchiveLoader::Condition::isExecute(const MR::StationedFileInfo*) const { return true; }

namespace nw4r {
    namespace lyt {
        Pane::Pane() {
            Init();
            mBasePosition = 4;  // centre/centre
            memset(mName, 0, sizeof(mName));
            memset(mUserData, 0, sizeof(mUserData));
            mTranslate = math::VEC3(0.0f, 0.0f, 0.0f);
            mRotate = math::VEC3(0.0f, 0.0f, 0.0f);
            mScale = math::VEC2(1.0f, 1.0f);
            mSize = Size(0.0f, 0.0f);
            mAlpha = 0xFF;
            mGlbAlpha = mAlpha;
            mFlag = 0;
            SetVisible(true);
        }
    }  // namespace lyt

    namespace ut {
        // The game only prints wide strings (see CustomTagProcessor.cpp).
        template <>
        f32 TextWriterBase< char >::PrintImpl(StreamType str, int length) {
            (void)str;
            (void)length;
            return 0.0f;
        }
    }  // namespace ut
}  // namespace nw4r

extern "C" {
__attribute__((weak)) int port_is_fullscreen_aspect(void) { return 0; }
__attribute__((weak)) float port_get_display_aspect(void) { return 16.0f / 9.0f; }
static int sFlyMode = 0;
__attribute__((weak)) int port_is_fly_mode(void) { return __atomic_load_n(&sFlyMode, __ATOMIC_ACQUIRE); }
__attribute__((weak)) void port_set_fly_mode(int e) { __atomic_store_n(&sFlyMode, e, __ATOMIC_RELEASE); }

static int sSkipCutscenes = 0;
__attribute__((weak)) int port_is_skip_cutscenes(void) { return __atomic_load_n(&sSkipCutscenes, __ATOMIC_ACQUIRE); }
__attribute__((weak)) void port_set_skip_cutscenes(int e) { __atomic_store_n(&sSkipCutscenes, e, __ATOMIC_RELEASE); }

static int sInfiniteHealth = 0;
__attribute__((weak)) int port_is_infinite_health(void) { return __atomic_load_n(&sInfiniteHealth, __ATOMIC_ACQUIRE); }
__attribute__((weak)) void port_set_infinite_health(int e) { __atomic_store_n(&sInfiniteHealth, e, __ATOMIC_RELEASE); }

static int sInfiniteLives = 0;
__attribute__((weak)) int port_is_infinite_lives(void) { return __atomic_load_n(&sInfiniteLives, __ATOMIC_ACQUIRE); }
__attribute__((weak)) void port_set_infinite_lives(int e) { __atomic_store_n(&sInfiniteLives, e, __ATOMIC_RELEASE); }

static int sInfiniteStarbits = 0;
__attribute__((weak)) int port_is_infinite_starbits(void) { return __atomic_load_n(&sInfiniteStarbits, __ATOMIC_ACQUIRE); }
__attribute__((weak)) void port_set_infinite_starbits(int e) { __atomic_store_n(&sInfiniteStarbits, e, __ATOMIC_RELEASE); }

__attribute__((weak)) int port_is_shield_hd_textures(void) { return 1; }
__attribute__((weak)) int port_is_shield_cutscenes(void) { return 1; }
__attribute__((weak)) int port_is_shield_controls(void) { return 1; }
__attribute__((weak)) int port_is_classic_gamepad_ui(void) { return 1; }
__attribute__((weak)) int port_is_riivolution_mods(void) { return 1; }
__attribute__((weak)) int port_input_real_a(void) { return 0; }
__attribute__((weak)) int port_input_real_z(void) { return 0; }

static int sQuickSaveRequested = 0;
static int sSaveInProgress = 0;
static uint64_t sSaveGeneration = 1;

__attribute__((weak)) void port_trigger_quick_save(void) {
    __atomic_store_n(&sQuickSaveRequested, 1, __ATOMIC_RELEASE);
}
__attribute__((weak)) int port_check_quick_save_request(void) {
    return __atomic_exchange_n(&sQuickSaveRequested, 0, __ATOMIC_ACQ_REL);
}
__attribute__((weak)) void port_save_notify_started(void) {
    __atomic_store_n(&sSaveInProgress, 1, __ATOMIC_RELEASE);
}
__attribute__((weak)) void port_save_notify_completed(int success) {
    if (success) {
        __atomic_add_fetch(&sSaveGeneration, 1, __ATOMIC_ACQ_REL);
    }
    __atomic_store_n(&sSaveInProgress, 0, __ATOMIC_RELEASE);
}
__attribute__((weak)) int port_is_save_in_progress(void) {
    return __atomic_load_n(&sSaveInProgress, __ATOMIC_ACQUIRE);
}
__attribute__((weak)) uint64_t port_get_save_generation(void) {
    return __atomic_load_n(&sSaveGeneration, __ATOMIC_ACQUIRE);
}

static int sQuickLoadRequested = 0;
static int sQuickLoadSlot = 1;
__attribute__((weak)) void port_trigger_quick_load(int slot) {
    sQuickLoadSlot = slot;
    __atomic_store_n(&sQuickLoadRequested, 1, __ATOMIC_RELEASE);
}
__attribute__((weak)) int port_check_quick_load_request(int* slot) {
    if (slot) *slot = sQuickLoadSlot;
    return __atomic_exchange_n(&sQuickLoadRequested, 0, __ATOMIC_ACQ_REL);
}

__attribute__((weak)) void port_android_vibrate(int ms) { (void)ms; }

static int sCharacterChoice = 0;
__attribute__((weak)) int port_get_character_choice(void) { return __atomic_load_n(&sCharacterChoice, __ATOMIC_ACQUIRE); }
__attribute__((weak)) void port_set_character_choice(int c) { __atomic_store_n(&sCharacterChoice, c, __ATOMIC_RELEASE); }

static int sOdysseyMoveset = 1;
__attribute__((weak)) int port_is_odyssey_moveset(void) { return __atomic_load_n(&sOdysseyMoveset, __ATOMIC_ACQUIRE); }
__attribute__((weak)) void port_set_odyssey_moveset(int e) { __atomic_store_n(&sOdysseyMoveset, e, __ATOMIC_RELEASE); }

static int sCoopMode = 0;
__attribute__((weak)) int port_is_coop_mode(void) { return __atomic_load_n(&sCoopMode, __ATOMIC_ACQUIRE); }
__attribute__((weak)) void port_set_coop_mode(int e) { __atomic_store_n(&sCoopMode, e, __ATOMIC_RELEASE); }

static int sSavestateSaveReq = 0;
static int sSavestateLoadReq = 0;
static int sSavestateHasData = 0;
__attribute__((weak)) void port_trigger_savestate_save(void) {
    __atomic_store_n(&sSavestateHasData, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&sSavestateSaveReq, 1, __ATOMIC_RELEASE);
}
__attribute__((weak)) void port_trigger_savestate_load(void) { __atomic_store_n(&sSavestateLoadReq, 1, __ATOMIC_RELEASE); }
__attribute__((weak)) int port_check_savestate_save(void) { return __atomic_exchange_n(&sSavestateSaveReq, 0, __ATOMIC_ACQ_REL); }
__attribute__((weak)) int port_check_savestate_load(void) { return __atomic_exchange_n(&sSavestateLoadReq, 0, __ATOMIC_ACQ_REL); }
__attribute__((weak)) int port_savestate_has_data(void) { return __atomic_load_n(&sSavestateHasData, __ATOMIC_ACQUIRE); }

static int sSpeedrunHudEnabled = 0;
__attribute__((weak)) int port_is_speedrun_hud_enabled(void) { return __atomic_load_n(&sSpeedrunHudEnabled, __ATOMIC_ACQUIRE); }
__attribute__((weak)) void port_set_speedrun_hud_enabled(int e) { __atomic_store_n(&sSpeedrunHudEnabled, e, __ATOMIC_RELEASE); }

static PortSpeedrunTelemetry sTelemetry{};
__attribute__((weak)) void port_update_speedrun_telemetry(const PortSpeedrunTelemetry* t) {
    if (t) sTelemetry = *t;
}
__attribute__((weak)) void port_get_speedrun_telemetry(PortSpeedrunTelemetry* t) {
    if (t) *t = sTelemetry;
}

static int sPostProcessing = 0;
__attribute__((weak)) int port_is_post_processing_enabled(void) { return __atomic_load_n(&sPostProcessing, __ATOMIC_ACQUIRE); }
__attribute__((weak)) void port_set_post_processing_enabled(int e) { __atomic_store_n(&sPostProcessing, e, __ATOMIC_RELEASE); }
}
