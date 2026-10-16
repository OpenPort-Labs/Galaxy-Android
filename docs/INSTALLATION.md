---
title: Galaxy Android - Comprehensive Installation & Setup Guide
description: Step-by-step instructions for installing and running Galaxy Android on Android devices, covering 100% PC-less on-device setup with the unified APK and PC desktop cooking pipelines.
tags:
  - android
  - installation
  - setup
  - tutorial
  - galaxy-android
  - apk
  - cooking
---

# Galaxy Android - Installation & Setup Guide

This guide explains how to install and play **Galaxy Android** on your Android phone, tablet, or gaming handheld.

> **Acknowledgements & Foundation:**
> The cooking and extraction pipeline is based on the toolchain developed by [bigmak94/GalaxyQuest](https://github.com/bigmak94/GalaxyQuest) and the decompilation efforts of [Petari](https://github.com/SMGCommunity/Petari). Special thanks to both teams for making native ports possible.

> [!IMPORTANT]
> **Legal Disclaimer:**
> You must provide your own legally dumped copy of the original optical disc image. This project does not distribute any proprietary game assets, binaries, or copyrighted data.

---

## System Requirements

- **Android Version:** Android 11 or newer (API Level 30+).
- **Architecture:** 64-bit ARM (`arm64-v8a`).
- **RAM:** Minimum 3 GB (4 GB+ recommended).
- **Free Storage:**
  - Game data without cinematics: ~2.5 GB.
  - Full game with cinematics (movies): ~5.0 GB.

---

## Method 1: 100% PC-Less On-Device Setup (Recommended)

With the unified **GalaxyAndroid** package, your phone extracts, decrypts, and converts your game disc directly on-device with zero PC required.

### Step 1: Place Your Disc Image on Your Phone
1. Transfer or download your legally dumped game disc into your phone's **`Download`** folder.
2. Supported disc formats:
   - **`.rvz`** (Modern compressed Dolphin format - recommended)
   - **`.wbfs`** (Wii Backup File System format)
   - **`.iso`** (Standard uncompressed Wii disc image)
   - **`.wia`** / **`.ciso`** / **`.zip`**

### Step 2: Install the Unified APK
Download or copy **`GalaxyAndroid.apk`** from [`out-mobile/GalaxyAndroid.apk`](../out-mobile/GalaxyAndroid.apk) (or from the [Releases](https://github.com/OpenPort-Labs/Galaxy-Android/releases)) and install it.

### Step 3: Launch & Setup
1. Launch **Galaxy Android** from your app drawer.
2. Grant requested storage access permissions (`All Files Access` / `MANAGE_EXTERNAL_STORAGE`) so the app can read your disc image and write game data to `/sdcard/GalaxyData`.
3. If game data is not yet cooked, the built-in Disc Cooker screen appears.
4. Tap **"Select Disc Image"** and choose your `.rvz`, `.iso`, or `.wbfs` file.
5. Check **"Include Cutscene Movies"** if you want prologue and ending cinematics.
6. Tap **"Start Cooking Game Data"**.
7. Once extraction finishes (typically 1–3 minutes), the game launches automatically!

---

## Method 2: PC Desktop Workflow (USB / ADB)

If you prefer to cook game assets on your computer for faster extraction:

### Prerequisites on PC
- Windows, macOS, or Linux
- [Python 3.8+](https://www.python.org/)
- [Android Platform-Tools (`adb`)](https://developer.android.com/tools/releases/platform-tools)
- [Dolphin Emulator](https://dolphin-emu.org/) (for extracting optical disc partitions)

### Step 1: Enable USB Debugging on Phone
1. Go to **Settings > About phone** and tap **Build number** 7 times.
2. Navigate to **Developer options** and turn on **USB debugging**.
3. Connect your phone via USB cable and tap **Always allow from this computer**.

### Step 2: Extract Your Game Disc with Dolphin
Using the command-line tool in [`tools/dolphin/`](../tools/dolphin/):
```bash
tools/dolphin/DolphinTool.exe extract -i "path/to/game.rvz" -o extracted
```
*(This creates an `extracted/DATA` folder containing `files` and `sys`.)*

### Step 3: Cook Assets to Little-Endian
From the project root:
- **Windows:**
  ```cmd
  py tools\cook\cook.py extracted cooked --with-movies
  ```
- **macOS / Linux:**
  ```bash
  python3 tools/cook/cook.py extracted cooked --with-movies
  ```

### Step 4: Stream Data to Device
- **Windows (1-Click):**
  Double-click [`transfer_game.bat`](../transfer_game.bat)
- **Manual ADB Command:**
  ```bash
  adb push cooked/files /sdcard/GalaxyData/
  adb push cooked/sys /sdcard/GalaxyData/
  ```

### Step 5: Install & Play
```bash
adb install -r out-mobile/GalaxyAndroid.apk
adb shell am start -n com.openport.galaxyandroid/.MainActivity
```

---

## Troubleshooting

### App crashes on startup
- Ensure you granted storage permission so the app can access `/sdcard/GalaxyData`.
- Make sure you are using release `v0.0.1cx` or newer, which resolves uninitialized target swapchain presentation bugs.

### Star Pointer moves vertically when turning phone
- Open the Settings drawer (`Gear`), scroll to Row 19b, and adjust **Invert X** / **Invert Y** or toggle **Gyro Aiming**.

### Low Frame Rate on Budget Devices
- Open the Settings drawer (`Gear`), set **Render Resolution** to **720p HD** or **480p Native**, or toggle **Battery Saver** mode.

---

## References

- [Controls & Input Reference](CONTROLS.md) - Input mapping and customization
- [Technical Architecture](TECHNICAL.md) - Deep-dive into engine architecture
- [Desktop Pipeline Guide](DESKTOP_INSTALLER.md) - Automated PC extraction
- [Petari Repository](https://github.com/SMGCommunity/Petari)
- [GalaxyQuest Repository](https://github.com/bigmak94/GalaxyQuest)
