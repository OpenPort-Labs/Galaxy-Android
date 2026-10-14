> **Disclaimer: This project was coded entirely by Gemini 3.8 Flash on Google Antigravity.**
> All mobile-specific adaptations, Android phone/tablet platform layer, on-screen touch overlay, Modenr UI slide-out settings drawer, native Hor+ ultrawide aspect ratio engine modifications, audio DSP pacing, cutscene movie player pipeline, and mobile packaging toolchain were implemented by **Gemini 3.8 Flash** on **Google Antigravity**.
> This project is completely based off the [GalaxyQuest](https://github.com/bigmak94/GalaxyQuest) source code by bigmak94 and the [Petari Super Mario Galaxy decompilation project](https://github.com/SMGCommunity/Petari).

# GalaxyMobile

**Play Super Mario Galaxy natively on your Android phone or tablet.**

GalaxyMobile is a **native Android ARM64 port** of Super Mario Galaxy, compiled directly from reconstructed C++ source code. It is **not an emulator**: the game code executes natively on your phone's processor using OpenGL ES 3.2 and AAudio, delivering fluid 60 FPS performance, low battery consumption, and zero emulation overhead.

---

## Features

- **True Native Hor+ Ultrawide (No Stretch, No Zoom)**:
  - Dynamically adapts the 3D camera projection to modern mobile aspect ratios (such as 19.5:9, 20:9, and foldable aspect ratios).
  - Horizontally expands field of view (Hor+) so you see more of the galaxy around Mario without vertical cropping.
  - Keeps 2D HUD elements (health circle, coin counters, star bit count, dialogs) centered in a 16:9 safe zone so circular UI elements remain perfect circles and are never obscured by camera hole-punch cutouts or rounded screen corners.
- **Modern Settings Drawer**:
  - Smooth slide-out menu docked cleanly on the top-left edge of the screen (`⚙`).
  - Native ultrawide Fullscreen toggle switch.
  - Virtual control opacity (25%, 50%, 75%, 100%) and button scale adjustments.
  - Floating dynamic stick mode vs. fixed thumbstick mode.
  - Interactive on-screen touch layout editor (drag buttons and stick to your desired positions).
- **Modern Minimalist Frosted Touch Controls**:
  - **Sleek Translucent Dark Glass Theme**: Inspired by modern minimalist mobile gamepads (Apple Arcade, Delta clean skins, Nothing OS), buttons feature semi-transparent frosted glass backplates with ultra-thin luminous perimeter rings and tactile depression feedback.
  - **Signature Micro-Accents**:
    - **A (Jump)**: Luminous Electric Cyan (`#38BDF8`)
    - **B (Star Bit Shoot)**: Luminous Star Amber (`#FBBF24`)
    - **SPIN (Star Spin)**: Luminous Mint Emerald (`#34D399`) with procedural **Galaxy Cyclone Swirl** icon
    - **Z (Crouch / Ground Pound)**: Luminous Lavender Violet (`#A78BFA`)
    - **+ (Pause / Menu)**: Minimalist Pearl Slate (`#E2E8F0`) with precision vector cross
  - **Precision Minimalist Reticle Joystick**:
    - Feather-light translucent glass plate with razor-thin outer boundary ring.
    - 4 cardinal precision ticks (N, S, E, W) and center deadzone target ring.
    - **Dynamic Luminous Laser Tether**: Vector laser line connects the stick origin to your thumb knob, providing immediate visual feedback of analog magnitude and deflection angle with zero screen clutter.
    - Floating frosted thumb puck with concentric grip groove.
  - **Directional Precision Dial Camera D-Pad**:
    - Unibody circular navigation disc with hairline quadrant dividers.
    - 4 modern vector **Chevrons (◀ ▶ ▲ ▼)** that illuminate and scale with glowing neon pulses upon touch.
    - Centered concave hub featuring a crisp minimalist **Camera Shutter Glyph (📷)**.
    - Replicates the Wii Remote D-Pad for Super Mario Galaxy's camera controls (Left/Right rotate, Down recenter, Up first-person look).
    - Fully customizable in Settings: toggle visibility, switch between Left and Right sides, or drag anywhere in the Layout Editor!
  - Direct 1:1 Star Pointer finger tracking: touch and drag anywhere on screen to collect and aim star bits.
- **Physical Gamepad Support**:
  - Full plug-and-play support for Bluetooth and USB gamepads (Xbox, PlayStation, Nintendo Switch Pro, Razer Kishi, Backbone, etc.).
- **Cinematic Cutscenes & High-Quality Audio**:
  - Software-decoded THP video playback for the prologue and ending cinematics.
  - Real-time DSP mixer running through Android's low-latency AAudio subsystem.

---

## System Requirements

| Specification | Minimum Requirement | Recommended |
|---|---|---|
| **Operating System** | **Android 11 or newer (API Level 30+)** | Android 12, 13, 14, or 15 |
| **CPU Architecture** | 64-bit ARM (`arm64-v8a`) | 64-bit ARM (`arm64-v8a`) |
| **Graphics API** | OpenGL ES 3.2 | OpenGL ES 3.2 |
| **Audio Subsystem** | AAudio | AAudio |
| **RAM** | 3 GB | 4 GB or higher |
| **Storage** | ~2.5 GB (without movies) / ~5 GB (with movies) | Fast UFS internal storage |

---

> **Legal Notice:**
> **You must provide your own legally dumped copy of Super Mario Galaxy.** Neither this repository nor the compiled APK contains any copyrighted Nintendo game assets, levels, audio, textures, or cinematics. You extract and convert your own legally owned disc once on your PC and transfer the cooked data to your Android device.

---

## Getting Started: How to Run on Android

There are two ways to set up and play GalaxyMobile:
- **Method 1 (Recommended / 100% PC-Less)**: Use the companion **[GalaxyInstaller.apk](out-mobile/GalaxyInstaller.apk)** directly on your phone — feed it your `.rvz`, `.wbfs`, `.iso`, or `.wia` game dump and it handles extraction, decryption, and cooking directly on your phone! Zero PC needed!
- **Method 2 (Advanced / PC Toolchain)**: Cook assets on your PC using Python and push them via USB (`adb` or `transfer_game.bat`).

---

### Method 1: 100% PC-Less Setup on Your Phone (Easiest)

With the dedicated `GalaxyInstaller` app, your phone does everything on-device — no PC, Python setup, or command line required:

1. **Get your Super Mario Galaxy game dump on your phone**:
   - Download or transfer your game dump directly onto your Android device (into your **`Download`** folder).
   - Supported disc formats:
     - **`.rvz`** (Dolphin modern compressed format — auto-extracted directly on device)
     - **`.wbfs`** (Wii Backup File System — standard format created by CleanRip / USB Loader GX)
     - **`.iso`** (Standard Wii optical disc image)
     - **`.wia`** (Wii ISO Archive format)
     - **`.zip`** (Archive containing game dump files)
     - *(Or an extracted game folder from Dolphin)*
2. **Install the APKs on your phone (Android 11 or newer)**:
   - **GalaxyMobile** ([`out-mobile/GalaxyMobile.apk`](out-mobile/GalaxyMobile.apk), package `com.galaxy.mobile`): The pure vanilla experience with **0 mods** and clean standard controls.
   - **GalaxyMobile+** ([`out-mobile/GalaxyMobilePlus.apk`](out-mobile/GalaxyMobilePlus.apk), package `com.galaxy.mobile.plus`): The enhanced edition featuring the tabbed **[ Advanced ]** mods drawer (Fly Mode, Cutscene Prevention, Infinite Health + void teleport).
   - **GalaxyInstaller** ([`out-mobile/GalaxyInstaller.apk`](out-mobile/GalaxyInstaller.apk), package `com.galaxy.installer`): The on-device companion installer.
   *(Both GalaxyMobile and GalaxyMobile+ can be installed side-by-side and automatically share cooked game data!)*
3. **Open GalaxyInstaller**:
   - Open **GalaxyInstaller** from your phone's app drawer.
   - Grant storage permission when prompted.
   - The app will automatically scan your `Download` folder and detect your `.rvz`, `.wbfs`, `.iso`, or `.wia` dump!
   - *(Or tap **"Select Game Disc (.wbfs / .iso / .zip)"** to pick any file manually).*
4. **Prepare Assets (1-Tap)**:
   - Check **"Include Cutscene Movies"** if you want prologue and ending cinematics (~2.3 GB).
   - Tap **"Start Cooking Game Data"**.
   - The app automatically:
     1. Unpacks and decrypts the optical disc partition on-device using its native ARM64 extraction engine (`libnodtool.so`).
     2. Runs multi-threaded asset cooking powered by an embedded ARM64 Python runtime and hardware-accelerated native C Yaz0 decompression (`libinstaller.so`), converting textures, geometry, audio streams, and levels directly into `/sdcard/GalaxyData`.
     3. Preserves temporary extraction cache across retries and cleans it up only upon successful completion.
5. **Launch & Play**:
   - Tap **"Launch GalaxyMobile"** or **GalaxyMobile+** and enjoy native 60 FPS Super Mario Galaxy on Android!

---

### Method 2: PC Toolchain (Command Line & ADB)

For users who prefer cooking assets ahead of time on their desktop:

#### Prerequisites
1. **An Android device**: Phone or tablet with ARM64 (`arm64-v8a`), Android 11 or newer (API 30+), and ~5 GB of free internal storage.
2. **A PC** (Windows, macOS, or Linux) with:
   - [Python 3.8 or newer](https://www.python.org/downloads/)
   - [Android SDK Platform-Tools (`adb`)](https://developer.android.com/tools/releases/platform-tools)
   - [Dolphin Emulator](https://dolphin-emu.org/) (used to extract your dumped game disc)
   - A USB-C data cable to connect your phone to your PC
3. **Your Super Mario Galaxy game disc**: Dumped as an ISO, RVZ, or WBFS file (e.g., using CleanRip on a Wii).

---

#### Step 1: Enable USB Debugging on Your Android Phone
1. Open **Settings > About phone**.
2. Tap **Build number** 7 times until you see "You are now a developer!".
3. Go to **Settings > System > Developer options** (or **Developer options** directly).
4. Enable **USB debugging**.
5. Connect your phone to your PC with a USB cable. When prompted on your phone, check **Always allow from this computer** and tap **Allow**.

Verify connection on your PC terminal:
```bash
adb devices
```
*(Your phone should be listed with the status `device`.)*

---

#### Step 2: Extract Your Game Disc
1. Open **Dolphin Emulator** on your PC.
2. In Dolphin's game list, right-click **Super Mario Galaxy** > **Properties** > **Filesystem**.
3. Right-click the disc entry at the very top of the tree > **Extract Entire Disc...**.
4. Choose an empty folder named `extracted` inside your working directory.
   *(When finished, `extracted` will contain a `DATA` folder with `files` and `sys`.)*

Alternatively, using `DolphinTool` via command-line:
```bash
DolphinTool extract -i "Super Mario Galaxy.rvz" -o extracted
```

---

#### Step 3: Convert (Cook) the Game Assets
Run the asset converter from the repository root:

- **Windows (Command Prompt / PowerShell)**:
  ```cmd
  py tools\cook\cook.py extracted cooked --with-movies
  ```
- **macOS / Linux**:
  ```bash
  python3 tools/cook/cook.py extracted cooked --with-movies
  ```

This processes textures, geometry, audio streams, and cinematics into format-optimized data inside a new `cooked` folder (~3.3 GB).

---

#### Step 4: Transfer Game Files to Your Phone

##### Option A: 1-Click Automated Script (Recommended on Windows)
1. Ensure your phone is plugged in via USB with USB debugging enabled.
2. Double-click [`transfer_game.bat`](transfer_game.bat) (or run `.\transfer_game.bat` in terminal).
3. The script automatically bundles the ~3.3 GB cooked assets and streams them directly onto your device in about 2 minutes with zero manual setup.

##### Option B: Command Line (Cross-Platform)
- **Windows**:
  ```cmd
  py tools\push_data.py cooked
  ```
- **macOS / Linux**:
  ```bash
  python3 tools/push_data.py cooked
  ```

##### Option C: Drag-and-Drop via File Explorer (No ADB Needed)
1. Compress your `cooked` folder into a zip file (`game.zip`).
2. Plug your phone into your PC and drag `game.zip` into your phone's **`Download`** folder.
3. On your phone, open your file manager (Samsung My Files / Google Files) and extract `game.zip`.
*(GalaxyMobile automatically checks `/sdcard/GalaxyData`, `/sdcard/Download/game`, and `/sdcard/GalaxyQuest` on startup!)*

---

#### Step 5: Install and Run GalaxyMobile or GalaxyMobile+
1. Install your preferred flavor on your device:
   - **GalaxyMobile (Vanilla)**:
     ```bash
     adb install -r -t -g out-mobile/GalaxyMobile.apk
     ```
     *(Package name: `com.galaxy.mobile`)*
   - **GalaxyMobile+ (Mods Enabled)**:
     ```bash
     adb install -r -t -g out-mobile/GalaxyMobilePlus.apk
     ```
     *(Package name: `com.galaxy.mobile.plus`)*
2. Launch your selected app from your phone's app drawer!
3. Tap the `⚙` gear tab at the top-left of the screen to open the Material 3 Settings Drawer:
   - In **GalaxyMobile**: Clean, pure vanilla settings (HUD visibility, Camera D-Pad toggles & positioning, Dynamic vs. Fixed stick, opacity, button scale, layout repositioning, and Fullscreen Hor+ Ultrawide). All mods are 100% disabled.
   - In **GalaxyMobile+**: The drawer features **[ General ]** and **[ Advanced ]** tabs:
     - **General Tab**: All standard HUD, D-Pad, stick, and display settings.
     - **Advanced Tab**: Toggle experimental gameplay mods (disabled by default):
       - **Fly Mode**: Hold the **A** button to fly up, hold the **Z** button to fly down, and steer freely in 360 degrees.
       - **Cutscene Prevention**: Instantly skips video cutscenes and THP cinematics without delay.
       - **Infinite Health**: Mario becomes invincible and never loses health; falling into black holes or abysses teleports Mario safely back to the stage's starting point.

---

## Building from Source

To compile the Android APKs yourself:

### Requirements
- Android NDK `r25` or `r26` (or newer)
- Android SDK (API 30+ and Build-Tools 34+)
- CMake 3.22+ and Ninja
- JDK 17+ (for `javac`, `d8`, and `apksigner`)
- Python 3.8+

### Build Commands

#### 1. Building GalaxyMobile (`com.galaxy.mobile`)
1. **Configure and compile the native engine library (`libgame.so`)**:
   - **Windows / Linux / macOS**:
     ```bash
     cmake --build build-mobile --target game
     ```
     *(Or run `./build_mobile.sh` via Git Bash).*
2. **Package, align (16KB page-size compatible), and sign the APK**:
   - **Windows (Python 3)**:
     ```cmd
     py package_mobile_apk.py
     ```
   - **Linux / macOS**:
     ```bash
     ./package_mobile_apk.sh
     ```
3. The resulting APK will be saved to `out-mobile/GalaxyMobile.apk`.

#### 2. Building GalaxyInstaller (`com.galaxy.installer`)
- **Windows (1-Click Batch)**:
  ```cmd
  build_installer.bat
  ```
- **Cross-Platform / Python**:
  ```bash
  python installer-mobile/build.py
  ```
- The resulting APK will be saved to `out-mobile/GalaxyInstaller.apk`.

---

## Credits & Acknowledgements

- **Gemini 3.8 Flash on Google Antigravity**: Autonomous architecture, coding, debugging, touch UI implementation, native Hor+ ultrawide rendering pipeline, and build system engineering.
- **[bigmak94 / GalaxyQuest](https://github.com/bigmak94/GalaxyQuest)**: The original standalone VR/port project that pioneered custom platform layers for Super Mario Galaxy.
- **[Petari Decompilation Project](https://github.com/SMGCommunity/Petari)**: The incredible community reverse-engineering and C++ decompilation of Super Mario Galaxy.
- **Nintendo**: Super Mario Galaxy is © Nintendo. This project is a non-commercial open-source research and preservation effort.
