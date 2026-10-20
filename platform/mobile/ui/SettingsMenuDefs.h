#pragma once

#include <stddef.h>

namespace mobile_ui {

enum MenuActionId {
    ACT_NONE = 0,
    ACT_SECTION_HEADER,
    // Controls Tab
    ACT_SHOW_CONTROLS,
    ACT_LAYOUT_BUTTONS,
    ACT_CONTROLS_OPACITY,
    ACT_BUTTON_SIZE,
    ACT_JOYSTICK_MODE,
    ACT_STICK_SENSITIVITY,
    ACT_SHOW_DPAD,
    ACT_DPAD_SIDE,
    ACT_GYRO_AIMING,
    ACT_GYRO_INVERT_BUTTONS,
    ACT_RELATIVE_POINTER,
    ACT_FULL_TOUCH_MODE,
    ACT_HAPTICS,
    ACT_TOUCH_CAMERA,
    ACT_STAR_STICK,
    ACT_STAR_STICK_SENSITIVITY,
    ACT_REPOSITION_STAR_STICK,
    ACT_RIGHT_STICK_MODE,
    // Graphics & Display Tab
    ACT_RENDER_SCALE,
    ACT_TARGET_FPS,
    ACT_DISPLAY_REFRESH,
    ACT_BATTERY_SAVER,
    ACT_FULLSCREEN,
    ACT_ORIENTATION,
    ACT_PERF_HUD,
    ACT_VULKAN_POSTPROCESS,
    ACT_DEVICE_INFO_CARD,
    // Mods & Enhancements Tab
    ACT_SHIELD_TEXTURES,
    ACT_SHIELD_CUTSCENES,
    ACT_SHIELD_CONTROLS,
    ACT_CLASSIC_GAMEPAD,
    ACT_SAVESTATE_ACTIONS,
    ACT_SPEEDRUN_HUD,
    ACT_GAME_SPEED,
    ACT_FLY_MODE,
    ACT_SKIP_CUTSCENES,
    ACT_INFINITE_HEALTH,
    ACT_INFINITE_LIVES,
    ACT_INFINITE_STARBITS,
    ACT_RIIVOLUTION,
    ACT_RELOAD_MODS,
    // Save & System Tab
    ACT_QUICK_CARDS,
    ACT_SELECT_SLOT,
    ACT_SLOT_ACTIONS,
    ACT_SD_BACKUP_ACTIONS,
    ACT_STORAGE_INFO_CARD,
    ACT_LANGUAGE,
    ACT_VIEW_LOGS,
    ACT_EXIT_LAUNCHER
};

enum RowType {
    ROW_SECTION,
    ROW_SWITCH,
    ROW_SLIDER,
    ROW_DROPDOWN,
    ROW_SEGMENTED,
    ROW_DUAL_BTN,
    ROW_SINGLE_BTN,
    ROW_INFO_CARD,
    ROW_QUICK_CARDS
};

struct MenuRowDef {
    MenuActionId action;
    RowType type;
    float heightDp;
    const char* title;
    const char* subtitle;
    int optCount;
    const char* optLabels[6];
    float minVal;
    float maxVal;
    float stepVal;
    const char* valueFormat;
};

const MenuRowDef* getTabRows(int tab, size_t& outCount);
float getTabContentHeight(int tab, float uiScale);

}  // namespace mobile_ui
