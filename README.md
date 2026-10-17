# Galaxy Android

<p align="center">
  <img src="docs/images/logo.png" width="140" alt="Galaxy Android Logo" />
</p>

[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)
[![Platform: Android ARM64](https://img.shields.io/badge/Platform-Android%20ARM64-brightgreen.svg)](docs/TECHNICAL.md)
[![Graphics: Vulkan 1.0 (Default) | GLES 3.2](https://img.shields.io/badge/Graphics-Vulkan%201.0%20(Default)%20%7C%20GLES%203.2-orange.svg)](docs/TECHNICAL.md)
[![Audio: AAudio](https://img.shields.io/badge/Audio-AAudio%20Low--Latency-purple.svg)](docs/TECHNICAL.md)
[![Release: v0.0.2](https://img.shields.io/badge/Release-v0.0.2-blue.svg)](https://github.com/OpenPort-Labs/Galaxy-Android/releases/tag/v0.0.2)

**Play the classic 3D cosmic gravity platformer natively on your Android phone, tablet, or handheld console.**

**Galaxy Android** is a **100% native Android ARM64 port**, compiled directly from reconstructed C++ source code. Maintained by [OpenPort Labs](https://github.com/OpenPort-Labs/Galaxy-Android).

It is **not an emulator**: the game code executes natively on your phone's processor using **Vulkan 1.0** (with OpenGL ES 3.2 fallback) and **AAudio**, delivering fluid 60 FPS performance, low battery consumption, and zero emulation overhead.

---

## Acknowledgements & Deep Gratitude

This project is made possible through the pioneering work of the community:

- **[bigmak94 / GalaxyQuest](https://github.com/bigmak94/GalaxyQuest)**: For the standalone mobile/ARM port architecture, memory reservation shims, and the little-endian asset cooker toolchain.
- **[Petari Decompilation Project](https://github.com/SMGCommunity/Petari)**: For the reverse-engineering and C++ decompilation research.
- **[LukeValen](https://github.com/LukeValen)**: Special thanks for helping sort out ARM Mali GPU rendering issues and shader pipeline compatibility.

We express our deepest thanks to our contributors and the community for keeping game preservation alive.

---

## Key Features

### Native Vulkan 1.0 Pipeline (Primary & Recommended)
- **Zero-Overhead Graphics**: Fully native Vulkan 1.0 renderer featuring unified Pipeline State Objects (PSO), offscreen EFB render target creation, dynamic swapchain blitting, and synchronized fence pacing.
- **Native Vulkan 1.0 UI Overlay**: Completely native Vulkan pipeline (`mUiPipeline`) rendering touch controls, vector joystick lines, and Material 3 drawer widgets directly into the swapchain render pass.
- **OpenGL ES 3.2 Fallback**: Robust secondary backend featuring dynamic TEV stage translation, indirect coordinate warping, and cross-GPU lighting safeguards for legacy devices.

### Real-Time Hardware Performance Telemetry HUD
- Monoscopic render submission and frame-time telemetry powered by `port_perf_render`.
- Live on-screen pill badge reporting:
  - Real-time FPS (45–60+ FPS)
  - Frame delivery latency in milliseconds (`22.1 ms`)
  - Real-time CPU core utilization (`CPU 20%`)
  - Active RAM footprint (`RAM 588 MB`)
- Three HUD modes selectable in Settings: **Off**, **FPS Only**, or **Full Hardware Stats**.

### Motion & Gyroscope Star Pointer Aiming
- **Real-Time Gyro Tracking**: Aim the Star Pointer naturally by tilting and moving your phone.
- **Corrected Landscape Sensor Mapping**: Corrected coordinate transforms for Android landscape orientation (`ROTATION_90`).
- **Invert X & Invert Y Toggles**: Independent horizontal and vertical axis inversion toggles accessible directly in the Settings drawer.
- **Accelerometer Fallback**: Smooth accelerometer fallback for devices without hardware gyroscopes.

### Dual Save Mirroring & Quick Save Anywhere
- ** Quick Save Anywhere**: Save your progress, star bits, coin counts, comet schedule, and unlocked galaxies at any moment via the Settings drawer or floating on-screen shortcut.
- **Dual Persistent Mirroring**: Automatically maintains mirrored save backups between NAND internal storage and `/sdcard/GalaxyData/saves/GameData.bin`. If game NAND data is ever cleared, saves are restored on boot.
- **One-Tap Save Export**: Export saves directly to your device's `Download` folder for backup or cross-device transfer.

### Comprehensive Gamepad & Handheld Console Support
- Native plug-and-play support for Bluetooth and USB gamepads (Xbox Wireless, PlayStation DualSense / DualShock 4, Switch Pro, Razer Kishi, Backbone One, etc.).
- Configured with `android.hardware.touchscreen` optional (`required="false"`) and `android.hardware.gamepad` enabled for full compatibility with Android TV and dedicated gaming handhelds (Odin, Retroid Pocket, Logitech G Cloud).

### Frosted Glass Minimalist Touch Controls
- **Sleek Translucent Dark Glass Theme**: Semi-transparent frosted glass backplates with luminous perimeter rings and tactile depression feedback.
- **Dynamic Luminous Laser Tether Joystick**: Vector laser line connects the stick origin to your thumb knob, providing immediate visual feedback of analog magnitude and deflection angle. Supports dynamic floating stick anchor mode.
- **Directional Camera D-Pad**: Unibody circular navigation disc replicating the controller D-Pad for 45° camera rotation and recentering.
- **Direct 1:1 Star Pointer Finger Tracking**: Touch and drag anywhere on screen to collect and aim star bits without accidental joystick conflicts.
- **Interactive Layout Editor**: Reposition and scale any button cluster to fit your hands.

### True Native Hor+ Ultrawide (No Stretch, No Zoom)
- Dynamically adapts the 3D camera projection to modern mobile aspect ratios (19.5:9, 20:9, foldables, and tablets).
- Horizontally expands field of view (Hor+) without vertical cropping.
- Centers 2D HUD elements (health meter, coin counters, dialogs) within a 16:9 safe region, preserving circular geometry and preventing obstruction by camera hole-punch cutouts or rounded screen corners.

### In-Game Settings Drawer & Customization
- Slide-out drawer docked cleanly on the top-left edge of the screen.
- **Dynamic Resolution Scaling**: Native 480p, 720p HD, 1080p FHD, and 1440p 2K Ultra.
- **Game Speed Multiplier**: 1.0x, 1.5x, 2.0x, 3.0x, and 4.0x speed options.
- **In-Game Language Selection**: Auto-detect, English, Spanish, French, German, or Italian.
- **Battery Saver / Thermal Mode**: 30 FPS cap for low thermal footprint and extended battery life.

---

## Native Decompilation Capabilities & Roadmap

Unlike an emulator that interprets PowerPC assembly and emulates GameCube/Wii fixed-function hardware at runtime, having the complete reconstructed C++ source code unlocks capabilities impossible on traditional emulators:

### 1. Engine & Framerate Modernization
- **Physics Tick Decoupling (90Hz / 120Hz)**:
  - GameCube and Wii logic and animation updates are tied to 60 FPS (`16.66ms`).
  - By parameterizing delta time and adding sub-frame state interpolation in `GameSystem.cpp` and `LiveActor::calcAnim()`, the engine can run at true native 90 FPS or 120 FPS on high-refresh mobile and desktop displays without altering game physics speed.
- **Uncapped Frustum Culling & Dynamic Ultrawide (20:9 / 21:9)**:
  - Emulators produce pop-in and missing geometry when widening viewports because actors are culled by hardcoded bounding volumes.
  - Native control over `ActorClippingService.cpp` and `CameraContext` allows dynamically scaling clipping volumes and culling planes to match any aspect ratio.
- **Multithreaded Task Graph**:
  - Independent systems such as actor collision checks (`CollisionDirector::update`), bone matrix updates, and particle simulations (`ParticleDirector`) can be dispatched to worker thread pools across multiple CPU cores, freeing the main thread for zero-latency render dispatch.

### 2. Camera & Control Architecture
- **True 360-Degree Dual-Stick Camera**:
  - Original camera logic restricts angles or requires recentering. Decompiled camera controllers in `CameraDirector.cpp` allow injecting full, unconstrained 360-degree analog camera rotation across all levels.
- **Direct Hardware Sensor Mapping**:
  - Device IMUs (accelerometer and gyroscope) feed directly into pointer and tilt matrices without intermediate Bluetooth or HID emulation overhead.
- **Rich Haptics Integration**:
  - Native dispatch of device-specific tactile feedback (Android `VibrationEffect` waveforms, DualSense adaptive triggers) tied to player actions like ground pounds, spins, and surface friction.

### 3. Modern Shading & Mobile GPU Optimizations
- **Advanced Post-Processing Pipeline**:
  - With depth buffers and vertex normals readily exposed in `vk_renderer.cpp`, screen-space ambient occlusion (SSAO), depth-of-field, bloom, and HDR tone mapping can be injected directly into the swapchain pass.
- **Vulkan Tile-Based Subpass Optimization**:
  - Mobile GPUs (ARM Mali and Qualcomm Adreno) use tile-based deferred architectures. Transitioning the GameCube EFB copy mechanism to native Vulkan subpasses allows transient framebuffer operations to stay entirely within on-chip tile memory, eliminating external DRAM bandwidth and reducing thermal throttling.
- **Direct Asset Streaming**:
  - Bypassing runtime Yaz0 compression in favor of pre-decompressed, memory-mapped formats (ASTC/BC7 textures, linear vertex buffers) enables instantaneous galaxy transitions.

### 4. Modding, Extensibility & Multiplayer
- **Super Mario Galaxy 2 Mechanics Backport**:
  - Mechanics from the sequel (such as Yoshi riding, Cloud Flower, and Rock Mushroom) share the same underlying actor and collision framework and can be compiled directly into the engine.
- **Split-Screen & Local Network Co-op**:
  - Player 2 in the original title was restricted to the Star Pointer. With access to `MarioActor` and player state management, full secondary playable characters (Luigi, Rosalina) can be spawned for true cooperative play.
- **Integrated Debugger & Level Tools**:
  - Built-in live inspection tools (`LayoutViewer`, scene graphs, free-flight debug camera, gravity vector visualizers, and collision wireframe renderers).

---

## System Requirements

| Specification | Minimum Requirement | Recommended |
|---|---|---|
| **Operating System** | **Android 11 or newer (API Level 30+)** | Android 12, 13, 14, or 15 |
| **CPU Architecture** | 64-bit ARM (`arm64-v8a`) | 64-bit ARM (`arm64-v8a`) |
| **Graphics API** | Vulkan 1.0 or OpenGL ES 3.2 | Vulkan 1.0 (Hardware Driver) |
| **Audio Subsystem** | AAudio | AAudio |
| **RAM** | 3 GB | 4 GB or higher |
| **Storage** | ~2.5 GB (without movies) / ~5 GB (with movies) | Fast UFS internal storage |

---

> [!IMPORTANT]
> **Legal Notice:**
> **You must provide your own legally dumped copy of the original optical disc.** Neither this repository nor the compiled APK contains any copyrighted game assets, levels, audio, textures, or cinematics. You extract and convert your own legally owned disc once on your phone or PC.

---

## Quick Start: How to Run on Android

For detailed instructions, see the complete [Installation Guide](docs/INSTALLATION.md).

### 1-Step Setup on Your Phone

With the unified **GalaxyAndroid** package:

1. **Place your game dump on your phone**:
   - Copy your legally dumped optical disc image (`.rvz`, `.wbfs`, `.iso`, or `.wia`) into your phone's **`Download`** folder.
2. **Install the unified APK**:
   - Install **`GalaxyAndroid.apk`**.
3. **Launch & Play**:
   - Launch the app, grant storage access when prompted, select your game file, and enjoy native 60 FPS gameplay on Android!

---

## Documentation

- [Decompilation Capabilities & Technical Roadmap](docs/DECOMP_ROADMAP.md) - Deep dive into 90Hz/120Hz physics decoupling, 360 camera, Vulkan subpasses, and co-op plan
- [Controls & Input Reference](docs/CONTROLS.md) - Touch overlay, pointer aim, gyroscope, and gamepad layouts
- [Technical Architecture](docs/TECHNICAL.md) - 32-bit address reservation, Vulkan 1.0 / GLES 3.2 translation, Hor+ ultrawide, and telemetry
- [Installation Guide](docs/INSTALLATION.md) - Comprehensive setup instructions
- [Desktop Pipeline Guide](docs/DESKTOP_INSTALLER.md) - Automated PC extraction and conversion
- [Decompilation Notes](docs/decomp/getting_started.md) - Decompilation symbol mappings and splits

---

## Building from Source

### Requirements
- Android NDK `r25`, `r26`, `r27`, or `r28`
- Android SDK (API 30+ and Build-Tools 34+)
- CMake 3.22+ and Ninja
- JDK 17+ and Python 3.8+

### Native Library & Packaging
```bash
# 1. Compile native engine library (libgame.so)
ninja -C build-mobile game

# 2. Package and sign unified APK
python package_mobile_apk.py
```
The output APK is generated at `out-mobile/GalaxyAndroid.apk`.

---

## License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)**. See the [`LICENSE`](LICENSE) file for details.
