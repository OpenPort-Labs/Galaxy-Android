#include "SettingsMenuModActions.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>

extern "C" void port_set_game_speed(float speed);
extern "C" void port_android_vibrate(int ms);
extern "C" int port_is_post_processing_enabled(void);
extern "C" void port_set_post_processing_enabled(int enabled);
extern "C" void port_set_speedrun_hud_enabled(int enabled);
extern "C" void port_trigger_savestate_save(void);
extern "C" void port_trigger_savestate_load(void);
extern "C" int port_savestate_has_data(void);

namespace mobile_ui {

namespace {

static void applyGameSpeed(int speedIdx) {
    float speed = 1.0f;
    switch (speedIdx) {
        case 1: speed = 1.0f; break;
        case 2: speed = 1.5f; break;
        case 3: speed = 2.0f; break;
        case 4: speed = 3.0f; break;
        case 5: speed = 4.0f; break;
        default: speed = 1.0f; break;
    }
    port_set_game_speed(speed);
}

}  // namespace

bool SettingsMenuModActions::dispatch(MenuActionId action, float px, float midX, float relX, float contentW,
                                      Settings& settings, SaveManager& saveMgr,
                                      std::string& outToastMsg, bool& outCloseSettings) {
    (void)outCloseSettings;
    switch (action) {
    case ACT_VULKAN_POSTPROCESS: {
        int cur = port_is_post_processing_enabled();
        port_set_post_processing_enabled(cur ? 0 : 1);
        outToastMsg = cur ? "Vulkan Post-Processing: OFF" : "Vulkan FXAA & Cosmic Bloom: ENABLED";
        return true;
    }
    case ACT_SAVESTATE_ACTIONS:
        if (px < midX) {
            port_trigger_savestate_save();
            if (settings.haptics) port_android_vibrate(50);
            outToastMsg = "Instant Savestate Saved!";
        } else {
            if (port_savestate_has_data()) {
                port_trigger_savestate_load();
                if (settings.haptics) port_android_vibrate(75);
                outToastMsg = "Instant Savestate Restored!";
            } else {
                outToastMsg = "No savestate in memory! Tap Save State first.";
            }
        }
        return true;
    case ACT_SPEEDRUN_HUD:
        settings.speedrunHud = !settings.speedrunHud;
        port_set_speedrun_hud_enabled(settings.speedrunHud ? 1 : 0);
        outToastMsg = settings.speedrunHud ? "Speedrun Practice HUD: ON" : "Speedrun Practice HUD: OFF";
        return true;
    case ACT_SHIELD_TEXTURES:
        settings.modShieldTextures = !settings.modShieldTextures;
        outToastMsg = settings.modShieldTextures ? "Shield HD Textures: ENABLED (1080p)" : "Shield HD Textures: DISABLED";
        return true;
    case ACT_SHIELD_CUTSCENES:
        settings.modShieldCutscenes = !settings.modShieldCutscenes;
        outToastMsg = settings.modShieldCutscenes ? "Shield 60FPS Cutscenes: ENABLED" : "Shield 60FPS Cutscenes: DISABLED";
        return true;
    case ACT_SHIELD_CONTROLS:
        settings.modShieldControls = !settings.modShieldControls;
        outToastMsg = settings.modShieldControls ? "Shield Calibrated Analog: ENABLED" : "Shield Calibrated Analog: OFF";
        return true;
    case ACT_CLASSIC_GAMEPAD:
        settings.modClassicGamepadUI = !settings.modClassicGamepadUI;
        outToastMsg = settings.modClassicGamepadUI ? "Modern Gamepad ABXY Prompts: ENABLED" : "Modern Gamepad Prompts: DISABLED";
        return true;
    case ACT_GAME_SPEED: {
        int idx = std::max(0, std::min(4, (int)(relX / (contentW / 5.0f))));
        settings.gameSpeed = idx + 1;
        applyGameSpeed(settings.gameSpeed);
        outToastMsg = (settings.gameSpeed == 1) ? "Game Speed: 1.0x (Normal)" :
                      (settings.gameSpeed == 2) ? "Game Speed: 1.5x" :
                      (settings.gameSpeed == 3) ? "Game Speed: 2.0x (Fast)" :
                      (settings.gameSpeed == 4) ? "Game Speed: 3.0x" : "Game Speed: 4.0x (Turbo)";
        return true;
    }
    case ACT_FLY_MODE:
        settings.modFly = !settings.modFly;
        port_set_fly_mode(settings.modFly ? 1 : 0);
        outToastMsg = settings.modFly ? "Fly Mode: ON (Hold A ascend, Z descend)" : "Fly Mode: OFF";
        return true;
    case ACT_SKIP_CUTSCENES:
        settings.modSkipCutscenes = !settings.modSkipCutscenes;
        port_set_skip_cutscenes(settings.modSkipCutscenes ? 1 : 0);
        outToastMsg = settings.modSkipCutscenes ? "Skip Cutscenes: ON" : "Skip Cutscenes: OFF";
        return true;
    case ACT_INFINITE_HEALTH:
        settings.modInfiniteHealth = !settings.modInfiniteHealth;
        port_set_infinite_health(settings.modInfiniteHealth ? 1 : 0);
        outToastMsg = settings.modInfiniteHealth ? "Infinite Health: ON" : "Infinite Health: OFF";
        return true;
    case ACT_INFINITE_LIVES:
        settings.modInfiniteLives = !settings.modInfiniteLives;
        port_set_infinite_lives(settings.modInfiniteLives ? 1 : 0);
        outToastMsg = settings.modInfiniteLives ? "Infinite Lives (99): ON" : "Infinite Lives: OFF";
        return true;
    case ACT_INFINITE_STARBITS:
        settings.modInfiniteStarBits = !settings.modInfiniteStarBits;
        port_set_infinite_starbits(settings.modInfiniteStarBits ? 1 : 0);
        outToastMsg = settings.modInfiniteStarBits ? "Infinite Star Bits (999): ON" : "Infinite Star Bits: OFF";
        return true;
    case ACT_RIIVOLUTION:
        settings.modRiivolution = !settings.modRiivolution;
        outToastMsg = settings.modRiivolution ? "Riivolution Mods: ON (/sdcard/GalaxyData/mods)" : "Riivolution Mods: OFF";
        return true;
    case ACT_RELOAD_MODS:
        outToastMsg = "Scanned /sdcard/GalaxyData/mods: Active!";
        return true;
    case ACT_QUICK_CARDS:
        outToastMsg = (px < midX) ? saveMgr.triggerSaveSlot(4, settings.haptics)
                                  : saveMgr.triggerLoadSlot(4, settings.haptics);
        return true;
    case ACT_SELECT_SLOT: {
        int idx = std::max(0, std::min(3, (int)(relX / (contentW / 4.0f))));
        saveMgr.setCurrentSlot(idx + 1);
        outToastMsg = (saveMgr.currentSlot() == 4) ? "Active Slot: Quick Slot"
                                                  : ("Active Slot: Slot " + std::to_string(saveMgr.currentSlot()));
        return true;
    }
    case ACT_SLOT_ACTIONS:
        outToastMsg = (px < midX) ? saveMgr.triggerSaveSlot(saveMgr.currentSlot(), settings.haptics)
                                  : saveMgr.triggerLoadSlot(saveMgr.currentSlot(), settings.haptics);
        return true;
    case ACT_SD_BACKUP_ACTIONS:
        if (px < midX) saveMgr.exportSave(outToastMsg);
        else saveMgr.importSave(outToastMsg);
        return true;
    default:
        return false;
    }
}

}  // namespace mobile_ui
