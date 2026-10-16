---
title: Galaxy Android - Control Schemes and Input Mapping
description: Complete input reference guide for Galaxy Android, detailing touch controls, pointer tracking, physical Bluetooth/USB gamepads, motion gyroscope aiming, and custom layout configuration.
tags:
  - android
  - controls
  - gamepad
  - touch-overlay
  - star-pointer
  - gyroscope
  - input-mapping
  - galaxy-android
---

# Galaxy Android - Controls & Input Guide

> **Acknowledgements & References:**
> Galaxy Android builds upon the Wii controller abstractions reconstructed in the [Petari Decompilation Project](https://github.com/SMGCommunity/Petari) and the custom platform input layer pioneered by [bigmak94/GalaxyQuest](https://github.com/bigmak94/GalaxyQuest). We extend our deepest thanks to both projects and their contributors.

Galaxy Android translates Android touchscreen interactions, physical Bluetooth/USB controller events, and hardware gyroscope motion into native Wii Remote + Nunchuk signals with zero emulation latency.

---

## 1. On-Screen Touch Controls

The on-screen overlay is engineered with a modern frosted translucent dark glass aesthetic with neon tactile micro-accents.

### Touch Layout Overview

| On-Screen Element | Controller Equivalent | In-Game Action |
|---|---|---|
| **Analog Thumbstick** (Left) | Analog Stick | 360° character movement, swimming, rolling |
| **A Button** (Cyan `#38BDF8`) | Button **A** | Jump, confirm in menus, talk, hold to fast-forward dialogue |
| **SPIN Button** (Emerald `#34D399`) | Motion Shake | Star Spin attack, spin jump, activate Launch Stars |
| **B Button** (Amber `#FBBF24`) | Trigger **B** | Shoot collected Star Bits at enemies / obstacles |
| **Z Button** (Violet `#A78BFA`) | Trigger **Z** | Crouch, ground pound (in air), long jump / backflip (with A) |
| **Camera D-Pad** (Dial) | **D-Pad** | Rotate camera (Left/Right), first-person view (Up), reset view (Down) |
| **Pause Button (+)** | **+** / Start | Pause menu, star checklist, options |
| **Screen Surface Drag** | IR Pointer | 1:1 direct finger aim: gather Star Bits, activate Pull Stars |

---

### Precision Reticle Joystick & Laser Tether

- **Dynamic Floating Mode (Default):** Tap and drag anywhere on the left third of your screen. The thumbstick anchors dynamically under your finger.
- **Fixed Mode:** Locks the joystick in a fixed physical position on screen (can be repositioned in the Layout Editor).
- **Luminous Laser Tether:** A vector laser line connects the joystick center to the thumb puck, providing immediate visual feedback of analog angle and deflection magnitude without screen clutter.

---

### Star Pointer & Interactive Touch Tracking

- **Direct Touch Gathering:** Sweep your finger over Star Bits floating in space to pull them directly into your inventory.
- **Pull Stars:** Touch and hold Pull Stars directly on screen to grapple toward them.
- **Star Bit Shooting:** Tap the **B** button to fire Star Bits toward the player's facing direction, or tap-and-hold the screen with a second finger to rapid-fire directly at specific targets.

---

### Directional Camera Dial

- Replicates controller D-Pad camera controls:
  - **Left / Right Chevrons (◀ ▶):** Orbit the camera 45° around the player.
  - **Top Chevron (▲):** Switch to first-person perspective.
  - **Bottom Chevron (▼):** Recenter the camera behind the player.
  - **Center Camera Shutter Hub:** Snaps camera orientation directly behind the player.
- Fully toggleable in settings (can be placed on either the left or right side).

---

## 2. Motion & Gyroscope Aiming

Galaxy Android features motion controls powered by Android's `ASensorManager`:

### Star Pointer Gyro Aiming
- **Natural Motion Tracking:** Move and tilt your phone to steer the star pointer across the cosmos. Gather star bits and target pull stars effortlessly.
- **Invert X / Invert Y Controls:** Independent horizontal and vertical direction toggles in Row 19b of the Settings drawer let you configure aiming to match your natural muscle memory.
- **Accelerometer Fallback:** Devices without a dedicated gyroscope sensor automatically switch to accelerometer vector tracking.

### Special Stage Tilting (Star Ball & Surfing Ray)
- **Touch Virtual Tilt (Default):** Touch and slide on the virtual direction ring to steer the Star Ball and Ray with responsive precision.
- **Device Gyroscope (Supported Devices):** Tilt your phone naturally to balance the Star Ball or carve turns on the Surfing Ray.

---

## 3. Physical Gamepads (Bluetooth & USB)

Galaxy Android automatically detects physical gamepads connected via Bluetooth or USB-C (including Xbox Wireless, PlayStation DualShock 4 / DualSense, Switch Pro Controller, Razer Kishi, and Backbone One).

### Standard Gamepad Mapping

| Gamepad Input | Controller Signal | In-Game Action |
|---|---|---|
| **Left Analog Stick** | Analog Stick | 360° Movement |
| **Right Analog Stick** | Pointer Aim | Analog Star Pointer aiming |
| **A / Cross (✕)** | Button **A** | Jump, talk, confirm |
| **X / Square (□)** | Shake / Motion | Star Spin attack |
| **Right Trigger (RT / R2)** | Trigger **B** | Shoot Star Bits |
| **Left Trigger (LT / L2)** | Trigger **Z** | Crouch, ground pound, long jump |
| **Left Bumper (LB / L1)** | Recenter | Recenter camera behind player |
| **Right Bumper (RB / R1)** | Shake / Motion | Spin attack (alternative) |
| **D-Pad (Left / Right)** | Wii Remote D-Pad (L/R) | Rotate camera |
| **D-Pad Up** | Wii Remote D-Pad Up | First-person view |
| **D-Pad Down** | Wii Remote D-Pad Down | Recenter camera / Page down |
| **Start / Options / +** | Wii Remote **+** | Pause menu |
| **Select / Share / −** | Wii Remote **−** | Pause menu (alternative) |

---

## 4. Customizing Controls & Settings Drawer

Open the slide-out **Material 3 Settings Drawer** by tapping the gear icon (`Gear`) at the top-left edge of the screen:

- ** Quick Save Anywhere:** Instantly write your game state, coins, star bits, and comet schedule to persistent storage without opening the pause menu or finishing the stage. Confirmed with an animated HUD toast.
- **Touch Controls Visibility:** Toggle on/off entirely when using a physical gamepad or playing on a dedicated gaming handheld (Retroid Pocket, Odin, Logitech G Cloud).
- **Button Opacity:** Adjust between 25%, 50%, 75%, and 100% transparency.
- **Button Scale:** Scale all touch buttons between 0.75x and 1.5x for phones or tablets.
- **Layout Editor:** Tap **"Edit On-Screen Layout"** to drag any button or joystick cluster to your preferred position. Positions are saved permanently to `settings.ini`.
- **Gyro Aiming & Axis Inversion:** Toggle gyro aiming, Invert X, and Invert Y.
- **Performance HUD Mode:** Switch between Off, FPS Only, and Full Hardware Stats.

---

## References

- [Technical Documentation](TECHNICAL.md) - Architecture of the native port
- [Installation Guide](INSTALLATION.md) - How to install and play
- [Petari Repository](https://github.com/SMGCommunity/Petari) - Decompilation research project
- [GalaxyQuest Repository](https://github.com/bigmak94/GalaxyQuest) - Standalone port architecture
