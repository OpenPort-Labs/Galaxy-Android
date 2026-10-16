# GalaxyAndroid v0.0.1cx — Native Vulkan 1.0 UI, Startup Crash Fix, and Telemetry Release

Hotfix and feature release bringing full native Vulkan 1.0 UI overlays, startup presentation crash resolution, corrected gyro coordinate transforms, and real-time renderer performance telemetry.

---

### Highlights & Fixes in v0.0.1cx

#### 1. Startup Presentation Crash Resolution
- **Uninitialized Vulkan Target Guard**: Resolved immediate crash on boot caused by the idle loop attempting to present `a.vkTarget` before `vkr.hasFrame()` was true. `colorImage` in layout `VK_IMAGE_LAYOUT_UNDEFINED` is now strictly guarded, eliminating GPU page faults and driver crashes at launch.
- **`vkr.present()` Target Quad Check**: Guarded target fullscreen quad sampling with `hasFrame()`, ensuring presentation render pass clears and overlays without sampling unrendered offscreen textures.

#### 2. Native Vulkan 1.0 UI & Touch Controls Overlay
- **No Stubs / Full Vulkan Implementation**: Built complete Vulkan graphics pipeline (`mUiPipeline`) for on-screen controls, glyph fonts, and drawer widgets directly in the swapchain render pass.
- **Vulkan NDC Projection Correction**: Configured top-left origin projection (`proj[5] = 2.0f / height`, `proj[13] = -1.0f`) to match Vulkan coordinate space, eliminating upside-down HUD text.

#### 3. Renderer Performance Telemetry
- **Corrected 0.0 FPS Reporting**: Routed monoscopic render duration metrics through `port_perf_render(renderDurationNs, 0)` in both Vulkan and OpenGL ES loops. Renderer FPS and CPU submission timings now accurately reflect active rendering in logs and HUD stats.

#### 4. Gyroscope Orientation & Inversion Controls
- **Landscape Coordinate Axis Correction**: Fixed horizontal movements moving the star cursor vertically. Corrected sensor orientation transform for Android landscape mode.
- **Invert X / Invert Y Settings**: Added persistent toggles and interactive buttons in Row 19b of the settings drawer for customizable star pointer tracking.

---

# GalaxyAndroid v0.0.1 — Initial Native Android Release

The first native ARM64 Android port of **Super Mario Galaxy**, executing compiled C++ game logic directly on Android hardware with zero emulation overhead.

---

### Highlights & Fixes in v0.0.1

#### 1. Overhauled Mobile Controls & Multi-Touch Input Engine
- **Exclusive Multi-Touch Pointer Tracking**: Rebuilt the touch input pipeline with a stateful pointer role tracking engine (`TouchRole`) supporting up to 10 concurrent fingers.
- **Mutually-Exclusive Nearest-Distance Matching**: Replaced overlapping hitboxes with exclusive Euclidean distance selection. A single touch can never trigger two buttons simultaneously (e.g. A + Spin or B + Z).
- **Eliminated Ghost Tap Injections**: Removed accidental `WPAD_BUTTON_A` press injections upon screen release during gameplay. Mario now jumps strictly when the A button is pressed.
- **Ergonomic Arc Layout**: Spaced out primary action buttons (A: Jump, B: Star Bit Shoot, Z: Crouch/Pound, SPIN: Shake/Cyclone, +: Pause) with comfortable thumb clearance matching rendering sub-pixels.
- **Directional Camera D-Pad**: Smooth camera orbit ( ) and recenter ( ) controls with customizable left/right dock positioning.

#### 2. Shading & Graphics Pipeline Fixes (Mali GPU & GLES 3.2)
- **Actor Lighting Fix**: Solved the actor over-saturation glitch where Mario and NPCs rendered glowing cyan/white. Added division-by-zero protection against `(0,0,0)` camera-origin rim lights (`GX_LIGHT2`) that caused `NaN`/`INF` light vectors on ARM Mali-G68 GPUs.
- **Water Refraction & EFB Distortion Mapping**: Corrected Y-axis screen coordinate mapping in `gl_shadergen.cpp`, fixing flipped texture distortion on water surfaces and heat shimmers.
- **GX Ambient & Material Registers**: Properly initialized default hardware state registers (`0x0A..0x0D`) matching Nintendo's `GXInit.c` specification.

#### 3.  Quick Save Anywhere
- **Save Anywhere In-Game**: Integrated native save routine into the engine sequence (`GameSequenceFunction::quickSaveAnywhere()`) saving Mario's progress, star bits, coin count, comet scheduler, and unlocked galaxies to NAND storage immediately without needing to return to the observatory.
- **Settings Drawer Quick Save**: Dedicated row in the Material 3 drawer (`` -> " Quick Save Anywhere") triggering the save sequence.
- **Floating Toast HUD Banner**: Animated top pill toast notifying player: `"Game Progress Saved Anywhere!"`.

#### 4. Vulkan 1.1 Renderer & Frame-Replay Hardening
- **GX Frame Replay Hook**: Connected `update()` and `render()` to `gpu::frameQueue().latest()` to actively poll and execute game frame draw calls into offscreen Vulkan EFB targets.
- **Blit Format Capabilities & Filter Validation**: Added `vkGetPhysicalDeviceFormatProperties` checks for `VK_FORMAT_FEATURE_BLIT_SRC_BIT` / `VK_FORMAT_FEATURE_BLIT_DST_BIT` and linear filter support, falling back gracefully to `VK_FILTER_NEAREST` or clear pass.
- **Swapchain Transfer Support Verification**: Explicitly verifies `VK_IMAGE_USAGE_TRANSFER_DST_BIT` in surface capabilities before configuring swapchain images.
- **Comprehensive VkResult Error Checking**: Added status checks to `vkEndCommandBuffer`, `vkQueueSubmit`, and `vkQueuePresentKHR`, with robust swapchain recreation on `VK_ERROR_OUT_OF_DATE_KHR` and `VK_SUBOPTIMAL_KHR`.

#### 5. CI Build & Android Compatibility Fixes
- **Linux Case-Sensitivity Fixes**: Fixed `XanimeCore.hpp` and standardized all 12 `KariKariDirector.hpp` include paths across the codebase, resolving CI compiler failure on Ubuntu runners.
- **Gamepad & TV / Handheld Support**: Configured `android.hardware.touchscreen` to `android:required="false"` and enabled `android.hardware.gamepad`, bringing native support to Odin, Retroid Pocket, Logitech G Cloud, and Android TV.

#### 6. In-App Disc Extraction & SAF Storage Fixes
- **Resolved `pythonhome.zip` Extraction Blocker**: Whitelisted `pythonhome.zip` in `.gitignore` to prevent embedded Python standard library assets from being omitted in release builds. Added packaging assertions guaranteeing Python runtime presence.
- **Integrated Native Cooker (`libinstaller.so`)**: Added CMake build target for `installer` library with full JNI aliases for `com.openport.galaxyandroid`, enabling zero-setup in-app asset extraction.
- **SAF Document & Content URI Handling**: Resolved `content://` URIs when selecting `.rvz`, `.iso`, or `.wbfs` files via Android's file picker, resolving clean filenames and streaming images for extraction without skipping the disc unpack step.

---

### Package Contents
- `GalaxyAndroid.apk`: Unified APK containing native ARM64 `libgame.so`, `libmain.so`, `libreserve.so`, `libinstaller.so`, and `libnodtool.so` signed for Android 11+ (API 30+).
