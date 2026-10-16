#pragma once

#include <android/input.h>
#include <stdint.h>
#include <string>
#include "port/input.h"

#include <vector>

namespace mobile_ui {

// In-shader vertex for 2D batching (used by both GLES 3.2 and Vulkan 1.0)
struct UIVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
    float mode;   // 0 = rect, 1 = circle/ring, 2 = font glyph, 3 = cogwheel, 4 = close 'X', etc.
    float param;  // ring: inner ratio; rect: corner radius ratio; cogwheel: rot angle
    float aspect; // rect aspect ratio (w/h or h/w)
    float extra;  // rect: 0 = horiz/square, 1 = vert
};

struct Settings {
    bool showControls = true;
    bool fullscreen = true;
    float opacity = 0.65f;
    float scale = 1.0f;
    bool dynamicStick = true;
    float stickOffsetX = 0.0f;
    float stickOffsetY = 0.0f;
    float buttonOffsetX = 0.0f;
    float buttonOffsetY = 0.0f;
    bool showDpad = true;
    bool dpadOnRight = false;
    float dpadOffsetX = 0.0f;
    float dpadOffsetY = 0.0f;

    // Advanced Mods (off by default)
    bool modFly = false;
    bool modSkipCutscenes = false;
    bool modInfiniteHealth = false;
    bool modInfiniteLives = false;
    bool modInfiniteStarBits = false;

    // Game Speed Multiplier (1 = 1x, 2 = 1.5x, 3 = 2x, 4 = 3x, 5 = 4x)
    int gameSpeed = 1;

    // In-game Language (0 = Auto, 1 = English, 2 = Spanish, 3 = French, 4 = German, 5 = Italian)
    int languageChoice = 0;

    // Render Resolution Scale (0 = 480p Native, 1 = 720p HD, 2 = 1080p FHD, 3 = 1440p 2K)
    int renderScale = 2;

    // Haptic feedback (off by default)
    bool haptics = false;

    // FPS configuration
    int targetFps = 60;  // 30, 60, 90, 120, or 144
    bool adaptiveFps = true;
    bool batterySaver = false; // 30 FPS thermal protection & power save mode
    bool gyroAiming = false;   // Gyroscope / accelerometer Star Pointer motion aiming
    bool gyroInvertX = false;  // Invert horizontal gyro axis
    bool gyroInvertY = false;  // Invert vertical gyro axis

    bool fullTouchMode = true;
    int perfHudMode = 2; // 0 = Off, 1 = FPS, 2 = Full Hardware Stats
    int graphicsBackend = 1; // 1 = Vulkan 1.0 (Full Native Default), 0 = OpenGL ES 3.2
    bool relativePointer = false; // Relative trackpad-style Star Pointer aiming (Issue #11)
    int screenOrientationChoice = 0; // 0 = Auto Sensor Landscape, 1 = Landscape, 2 = Reverse Landscape (Issues #18 & #19)

    void load(const std::string& path);
    void save(const std::string& path) const;
};

class UIOverlay {
public:
    static UIOverlay& get();

    bool init();
    void setSavePath(const std::string& path);

    // Per-frame tick to handle pulse buttons, auto-release, and hardware metrics
    void update(PortPadState& pad, int& spinFrames);

    // Current active buttons pressed on touchscreen
    uint32_t getTouchButtons() const;

    // Current virtual stick state
    bool isStickActive() const { return mStickPointerId >= 0; }
    float getTouchStickX() const { return (mStickPointerId >= 0) ? mTouchStickX : 0.0f; }
    float getTouchStickY() const { return (mStickPointerId >= 0) ? mTouchStickY : 0.0f; }

    // Processes an Android motion or key event.
    // Returns true if consumed (e.g., settings menu or virtual controls).
    bool handleInput(const AInputEvent* event, PortPadState& pad, int& spinFrames,
                     int screenW, int screenH, int vpX, int vpY, int vpW, int vpH);

    // Generate 2D quad geometry for Vulkan and OpenGL renderers
    void generateVertices(int screenW, int screenH, std::vector<UIVertex>& outVerts);

    // Retrieves font atlas dimensions and 8-bit coverage data
    static void getFontAtlas(int& width, int& height, std::vector<uint8_t>& coverage);

    // Render the overlay in OpenGL ES 3.2
    void render(int screenW, int screenH);

    // Call when a frame is presented to display to track true presented FPS
    void reportFramePresented();

    bool needsRedraw();
    bool isSettingsOpen() const { return mSettingsOpen || mDrawerAnim > 0.01f; }
    void closeSettings();
    void triggerSaveAnywhere();
    void triggerSaveSlot(int slot);
    void triggerLoadSlot(int slot);
    int currentSlot() const { return mCurrentSlot; }
    void setCurrentSlot(int s) { mCurrentSlot = (s >= 1 && s <= 3) ? s : 1; }
    void showToast(const std::string& msg);
    Settings& settings() { return mSettings; }
    // The backend that actually initialised (1 = Vulkan 1.0, 0 = OpenGL ES):
    // OpenGL takes over when no usable Vulkan device or driver is present, so
    // the requested setting and the running backend can differ.
    void setActiveBackend(int backend) { mActiveBackend = backend; }
    int activeBackend() const { return mActiveBackend; }
    // Device name reported by the running backend (set from the Vulkan device
    // properties when Vulkan is active, from GL_RENDERER otherwise).
    void setGpuRendererName(const std::string& name) { if (!name.empty()) mGpuRendererName = name; }
    void reportGpuRenderTime(int64_t renderDurationNs) {
        if (renderDurationNs > 0) {
            mGpuRenderTimeAccumNs += renderDurationNs;
            mGpuRenderFrameCount++;
        }
    }
    void setPlusEdition(bool plus) { mPlusEdition = plus; }
    bool isPlusEdition() const { return mPlusEdition; }

    // Pointer position (Star Pointer)
    void setPointerPos(float x, float y) {
        mPointerX = x;
        mPointerY = y;
    }
    float getPointerX() const { return mPointerX; }
    float getPointerY() const { return mPointerY; }

    // Touch controls edit mode
    bool isEditModeActive() const { return mEditLayoutMode; }
    void setEditMode(bool active);
    void resetLayoutOffsets();

    // Save Data Import / Export
    bool exportSave();
    bool importSave();

private:
    UIOverlay();

    Settings mSettings;
    std::string mSavePath;
    int mCurrentSlot = 1;
    int mActiveBackend = 1;  // what actually initialised (see setActiveBackend)
    bool mPlusEdition = true;
    bool mInitialized = false;
    bool mDirty = false;

    // UI state
    bool mSettingsOpen = false;
    float mDrawerAnim = 0.0f; // 0.0 = closed offscreen, 1.0 = fully open
    int64_t mLastAnimTime = 0;
    bool mEditLayoutMode = false;
    int mDraggingCluster = 0; // 0 = none, 1 = stick cluster, 2 = button cluster
    float mDragStartX = 0.0f, mDragStartY = 0.0f;
    float mInitialStickOfsX = 0.0f, mInitialStickOfsY = 0.0f;
    float mInitialBtnOfsX = 0.0f, mInitialBtnOfsY = 0.0f;
    float mInitialDpadOfsX = 0.0f, mInitialDpadOfsY = 0.0f;

    // Settings Navigation & Scrolling
    int mSettingsTab = 0;              // 0 = General, 1 = Advanced
    float mDrawerScrollY = 0.0f;       // Current scroll offset (pixels)
    float mDrawerScrollTarget = 0.0f;
    bool mDrawerIsScrolling = false;
    int mDrawerScrollPointerId = -1;
    float mDrawerTouchStartY = 0.0f;
    float mDrawerTouchStartX = 0.0f;
    float mDrawerScrollStartOffset = 0.0f;

    // Virtual Stick State
    int mStickPointerId = -1;
    float mStickBaseX = 0.0f;
    float mStickBaseY = 0.0f;
    float mStickKnobX = 0.0f;
    float mStickKnobY = 0.0f;
    float mTouchStickX = 0.0f;
    float mTouchStickY = 0.0f;

    // Button press states (for visual press effect)
    bool mBtnAPressed = false;
    bool mBtnBPressed = false;
    bool mBtnZPressed = false;
    bool mBtnSpinPressed = false;
    bool mBtnPlusPressed = false;
    bool mBtnCfgPressed = false;
    bool mBtnDpadLeft = false;
    bool mBtnDpadRight = false;
    bool mBtnDpadUp = false;
    bool mBtnDpadDown = false;

    // Persistent in-game Star Pointer state (normalized -1..1, default to screen center 0, 0)
    float mPointerX = 0.0f;
    float mPointerY = 0.0f;

    // Finger active press tracking (prevents stuck buttons)
    bool mFingerDownOnA = false;
    bool mFingerDownOnB = false;
    bool mFingerDownOnZ = false;
    bool mFingerDownOnSpin = false;
    bool mFingerDownOnPlus = false;

    // Multi-touch Pointer Roles
    enum class TouchRole {
        NONE,
        STICK,
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
    TouchPoint mTouches[10];

    // Full Touch Gesture & Tap State
    int mTapPendingFrames = 0;
    int mTapDelayFrames = 0;
    int mTouchPointerId = -1;
    int64_t mTouchStartTime = 0;
    float mTouchStartX = 0.0f, mTouchStartY = 0.0f;
    int64_t mLastTapTime = 0;

    // Hardware & Performance Monitoring Metrics
    float mCurrentFps = 60.0f;
    float mAvgFrameTimeMs = 16.6f;
    int mFpsFrameCounter = 0;
    int64_t mFpsLastTimeNs = 0;
    float mCpuUsagePercent = 0.0f;
    float mGpuUsagePercent = 0.0f;
    float mRamUsageMB = 0.0f;
    int64_t mGpuRenderTimeAccumNs = 0;
    int mGpuRenderFrameCount = 0;
    int64_t mLastHwSampleTime = 0;
    std::string mGpuRendererName = "Mali-G68 GPU";

    // In-Game Log Viewer State
    bool mLogViewerOpen = false;
    float mLogScrollY = 0.0f;

    // Toast Notification State
    std::string mToastMessage = "Game Progress Saved Anywhere!";
    float mSaveToastTimer = 0.0f;

    // GL resources
    unsigned int mProg = 0;
    unsigned int mVao = 0;
    unsigned int mVbo = 0;
    unsigned int mFontTex = 0;
    int mAtlasW = 0;
    int mAtlasH = 0;
};

inline UIOverlay& get() {
    return UIOverlay::get();
}

}  // namespace mobile_ui
