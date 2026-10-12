> **Disclaimer: This project was coded entirely by Gemini 3.8 Flash on Google Antigravity.**
> All mobile-specific adaptations, Android phone/tablet platform layer, on-screen touch overlay, Google Material 3 slide-out settings drawer, native Hor+ ultrawide aspect ratio engine modifications, audio DSP pacing, cutscene movie player pipeline, and mobile packaging toolchain were implemented by **Gemini 3.8 Flash** on **Google Antigravity**.
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
- **Google Material 3 Settings Drawer**:
  - Smooth slide-out menu docked cleanly on the top-left edge of the screen (`⚙`).
  - Native ultrawide Fullscreen toggle switch.
  - Virtual control opacity (25%, 50%, 75%, 100%) and button scale adjustments.
  - Floating dynamic stick mode vs. fixed thumbstick mode.
  - Interactive on-screen touch layout editor (drag buttons and stick to your desired positions).
- **Responsive Touch & Star Pointer Controls**:
  - Ergonomic on-screen virtual stick and colored action buttons (A Jump, B Shoot Star Bit, Emerald Green SPIN, Violet Z Crouch/Ground Pound, + Pause).
  - Direct 1:1 Star Pointer finger tracking: touch and drag anywhere on screen to collect and aim star bits.
  - Dedicated **SPIN** button shakes the virtual Wiimote without triggering ground pounds.
  - Dedicated **Z** button handles crouching and ground pounds.
  - Android system volume buttons pass through cleanly to adjust media volume normally.
- **Physical Gamepad Support**:
  - Full plug-and-play support for Bluetooth and USB gamepads (Xbox, PlayStation, Nintendo Switch Pro, Razer Kishi, Backbone, etc.).
- **Cinematic Cutscenes & High-Quality Audio**:
  - Software-decoded THP video playback for the prologue and ending cinematics.
  - Real-time DSP mixer running through Android's low-latency AAudio subsystem.

---

> **Legal Notice:**
> **You must provide your own legally dumped copy of Super Mario Galaxy.** Neither this repository nor the compiled APK contains any copyrighted Nintendo game assets, levels, audio, textures, or cinematics. You extract and convert your own legally owned disc once on your PC and transfer the cooked data to your Android device.

---

## Getting Started: How to Run on Android

There are two ways to set up and play GalaxyMobile:
- **Method 1 (Recommended / 100% PC-Less)**: Use the companion **[GalaxyInstaller.apk](out-mobile/GalaxyInstaller.apk)** directly on your phone — feed it your `.wbfs`, `.iso`, or `.zip` game dump and it handles extraction, decryption, and cooking directly on your phone! Zero PC needed!
- **Method 2 (Advanced / PC Toolchain)**: Cook assets on your PC using Python and push them via USB (`adb` or `transfer_game.bat`).

---

### Method 1: 100% PC-Less Setup on Your Phone (Easiest)

With the dedicated `GalaxyInstaller` app, your phone does everything on-device — no PC, Python setup, or command line required:

1. **Get your Super Mario Galaxy game dump on your phone**:
   - Download or transfer your game dump directly on your Android device (into your **`Download`** folder).
   - Supported formats:
     - **`.wbfs`** (Wii Backup File System — standard format created by CleanRip / USB Loader GX)
     - **`.iso`** (Standard Wii optical disc image)
     - **`.zip`** (Archive containing game files or dump)
     - *(Or an extracted game folder from Dolphin)*
2. **Install both APKs on your phone**:
   - Install [`out-mobile/GalaxyMobile.apk`](out-mobile/GalaxyMobile.apk) (the game engine).
   - Install [`out-mobile/GalaxyInstaller.apk`](out-mobile/GalaxyInstaller.apk) (the companion installer).
3. **Open GalaxyInstaller**:
   - Open **GalaxyInstaller** from your phone's app drawer.
   - Grant storage permission when prompted.
   - The app will automatically scan your `Download` folder and detect your `.wbfs`, `.iso`, or `.zip` file!
   - *(Or tap **"Select Game Disc (.wbfs / .iso / .zip)"** to pick any file manually).*
4. **Prepare Assets (1-Tap)**:
   - Check **"Include Cutscene Movies"** if you want prologue and ending cinematics.
   - Tap **"Start Cooking Game Data"**.
   - The app automatically:
     1. Decrypts and extracts the Wii optical disc data partition on-device using its native ARM64 AES engine.
     2. Runs the embedded cooker engine to convert textures, geometry, audio streams, and levels directly into `/sdcard/Download/game`.
     3. Automatically cleans up temporary cache files so your phone storage is preserved.
5. **Launch & Play**:
   - Tap **"Launch GalaxyMobile 🚀"** and enjoy native 60 FPS Super Mario Galaxy on Android!

---

### Method 2: PC Toolchain (Command Line & ADB)

For users who prefer cooking assets ahead of time on their desktop:

#### Prerequisites
1. **An Android device**: Phone or tablet with ARM64 (`arm64-v8a`), Android 12 or newer, and ~5 GB of free internal storage.
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
*(GalaxyMobile automatically checks `/sdcard/Download/game`, `/sdcard/Download/cooked`, and `/sdcard/GalaxyQuest` on startup!)*

---

#### Step 5: Install and Run GalaxyMobile
1. Install [`out-mobile/GalaxyMobile.apk`](out-mobile/GalaxyMobile.apk) on your device:
   ```bash
   adb install --no-incremental -r out-mobile/GalaxyMobile.apk
   ```
   *(Or double-click `deploy_to_phone.bat` on Windows).*
2. Launch **GalaxyMobile** from your phone's app drawer!
3. Tap the `⚙` gear tab at the top-left of the screen to customize your controls or enable **Fullscreen (Hor+ Ultrawide)** mode.

---

## Building from Source

To compile the Android APK yourself:

### Requirements
- Android NDK `r25` or `r26`
- Android SDK (API 32+ and Build-Tools 34+)
- CMake 3.22+ and Ninja
- JDK 17+ (for `keytool` and `apksigner`)

### Build Commands

#### 1. Building GalaxyMobile (Game APK)
1. **Configure and compile the native library (`libgame.so`)**:
   ```bash
   # From Git Bash or Linux shell:
   ./build_mobile.sh
   ```
2. **Package and sign the APK**:
   ```bash
   ./package_mobile_apk.sh
   ```
3. The resulting APK will be saved to `out-mobile/GalaxyMobile.apk`.

#### 2. Building GalaxyInstaller (Companion Installer APK)
- **Windows (1-Click)**:
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
