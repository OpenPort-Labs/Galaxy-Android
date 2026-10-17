# Super Mario Galaxy Decompilation: Technical Capabilities & Execution Plan

## 1. Executive Summary

Having full C++ decompilation source code transforms this project from a compatibility shim into a native modern engine port. Emulators (such as Dolphin) must dynamically translate PowerPC assembly, simulate GameCube/Wii fixed-function graphics hardware (TEV, EFB, FIFO), and preserve original architecture quirks. 

Because we compile the decompiled C++ source directly to ARM64 and x86_64 binaries, we have direct control over:
- The core game loop and delta-time updates.
- Scene graph traversal and actor lifecycle management.
- The 3D camera projection and clipping math.
- The rendering backends (Vulkan 1.0 and OpenGL ES 3.2).
- Input abstraction and physical sensor pipelines.

This document outlines the highest-impact architectural enhancements enabled by native decompilation, complete with targeted source files and an execution plan.

---

## 2. Architectural Pillars

```
+---------------------------------------------------------------------------------------+
|                                GALAXY DECOMPILATION ROADMAP                           |
+---------------------------+---------------------------+-------------------------------+
| Engine & Framerate        | Controls & Camera         | Graphics & Vulkan             |
| - Physics tick decoupling | - 360 dual-stick camera   | - Vulkan tile subpasses       |
| - 90Hz / 120Hz display    | - IMU direct integration  | - SSAO & screen-space bloom   |
| - Ultrawide culling math  | - Rich haptics waveforms  | - Direct ASTC/BC7 streaming   |
| - Multi-core task graph   |                           |                               |
+---------------------------+---------------------------+-------------------------------+
|                             Gameplay & Expansion                                      |
|                             - SMG2 mechanics backport (Yoshi, Cloud, Rock)            |
|                             - True 2-player split-screen / network co-op              |
|                             - Built-in debugger, free-cam & collision visualizer      |
+---------------------------------------------------------------------------------------+
```

---

## 3. High-Impact Capabilities Breakdown

### Pillar 1: Engine & Framerate Modernization

#### A. Physics Tick Decoupling (90Hz / 120Hz)
- **Problem**: GameCube and Wii titles hardcode the main loop and physics to a fixed 60 FPS (`16.666 ms`) time step. Running the display at 90Hz or 120Hz on an emulator accelerates game speed by 1.5x to 2.0x.
- **Decomp Solution**:
  - Implement sub-tick interpolation in `decomp/src/Game/System/GameSystem.cpp` and `LiveActor::calcAnim()`.
  - Decouple the game update loop (`GameSystem::frameLoop`) into a fixed simulation tick (`60Hz`) while the render loop runs variable frame pacing with linear interpolation between previous and current transform matrices (`mPosition`, `mRotation`).
  - Target files:
    - `decomp/src/Game/System/GameSystem.cpp`
    - `decomp/src/Game/LiveActor/LiveActor.cpp`
    - `platform/src/main.cpp` / `platform/mobile/mobile_app.cpp`

#### B. Dynamic Frustum Culling & Ultrawide (20:9 / 21:9)
- **Problem**: When expanding field of view beyond 16:9, objects near screen edges suddenly vanish or pop in. This happens because the Wii actor clipping system tests against hardcoded 16:9/4:3 view frustum volumes.
- **Decomp Solution**:
  - Update `ClippingActorInfo::judgeClipping()` and `ClippingDirector` to dynamically derive frustum test planes from the actual projection matrix rather than static camera constants.
  - Scale horizontal actor bounding spheres proportionally to aspect ratio expansion.
  - Target files:
    - `decomp/src/Game/LiveActor/ClippingActorInfo.cpp`
    - `decomp/src/Game/LiveActor/ClippingActorHolder.cpp`
    - `decomp/src/Game/LiveActor/ClippingDirector.cpp`
    - `decomp/src/Game/Camera/CameraContext.cpp`

#### C. Multi-Core Task Graph (Asynchronous Collision & Particle Updates)
- **Problem**: Galaxy single-threads actor logic, collision queries, and particle updates on the CPU, causing frame drops during large galaxy explosions or dense physics encounters.
- **Decomp Solution**:
  - Partition actor groups (`LiveActorGroup`) into parallel worker jobs using a thread pool.
  - Dispatch collision sweeps in `CollisionDirector::update()` across worker threads prior to matrix transformation passes.
  - Target files:
    - `decomp/src/Game/LiveActor/LiveActorGroup.cpp`
    - `decomp/src/Game/Map/CollisionDirector.cpp`
    - `decomp/src/Game/Effect/ParticleDirector.cpp`

---

### Pillar 2: Controls & Camera Overhaul

#### A. True 360-Degree Dual-Stick Camera
- **Problem**: The original camera restricts rotation to preset 45-degree chunks or outright locks rotation during specific platforming sections, forcing reliance on pointer recentering.
- **Decomp Solution**:
  - Inject an unconstrained spherical orbital camera mode in `CameraDirector` and `CameraMan`.
  - Read right-stick analog deflection directly from `PortPadState` (`fs.stick`) and compute orbit yaw/pitch around Mario with spherical obstacle collision raycasts.
  - Target files:
    - `decomp/src/Game/Camera/CameraDirector.cpp`
    - `decomp/src/Game/Camera/CameraMan.cpp`
    - `decomp/src/Game/Camera/CameraLocalUtil.cpp`
    - `platform/src/input/wpad.cpp`

#### B. Direct IMU Hardware Integration & Rich Haptics
- **Problem**: Emulators pass motion through simulated Wiimote Bluetooth packets, introducing latency, jitter, and quantization artifacts.
- **Decomp Solution**:
  - Directly feed Android NDK sensor events (`ASensorEventQueue`) into the camera/pointer coordinate transforms in `mobile_app.cpp`.
  - Connect Mario physics events (wall kicks, ground pounds, spin attacks, damage) directly to Android `VibrationEffect` waveform APIs with frequency/amplitude modulation.
  - Target files:
    - `platform/mobile/mobile_app.cpp`
    - `decomp/src/Game/Player/MarioActor.cpp`
    - `decomp/src/Game/Util/SoundUtil.cpp`

---

### Pillar 3: Next-Gen Shading & Mobile GPU Optimizations

#### A. Vulkan Tile-Based Transient Subpasses
- **Problem**: The GameCube EFB (Embedded Framebuffer) copy pipeline forces copying render targets into texture memory, triggering costly external DRAM read/write roundtrips that throttle mobile thermal envelopes.
- **Decomp Solution**:
  - On tile-based mobile architectures (ARM Mali and Qualcomm Adreno), convert depth and color blits into Vulkan subpasses with `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT` and `VK_ATTACHMENT_STORE_OP_DONT_CARE`.
  - Keeps intermediate render targets completely within fast on-chip SRAM/tile memory, reducing memory bandwidth by up to 40% and preventing thermal throttling.
  - Target files:
    - `platform/src/gx/vk_renderer.cpp`
    - `platform/src/gx/vk_target.cpp`

#### B. Screen-Space Ambient Occlusion (SSAO) & Dynamic Post-Processing
- **Problem**: GameCube lighting is largely vertex-based or single-stage directional, lacking ambient depth cues in complex geometry.
- **Decomp Solution**:
  - Implement a fullscreen post-processing render pass using linear depth (`v_gxz`) and reconstructed view-space normals to compute ambient occlusion in screen space.
  - Target files:
    - `platform/src/gx/vk_renderer.cpp`
    - `platform/src/gx/vk_shadergen.cpp`

#### C. Lossless ASTC & BC7 Direct Texture Streaming
- **Problem**: Assets are compressed with Nintendo's proprietary Yaz0 and GameCube BTI/TPL formats, requiring runtime CPU decoding before GPU upload.
- **Decomp Solution**:
  - Convert textures during disc cooking directly into ASTC (Android) and BC7 (PC) blocks.
  - Memory-map compressed texture payloads straight into Vulkan `VkBuffer` staging areas, eliminating CPU texture conversion entirely and achieving near-zero load times.
  - Target files:
    - `platform/src/gx/gx_texture.cpp`
    - `tools/cook_disc.py`

---

### Pillar 4: Gameplay Expansion & Modding

#### A. Super Mario Galaxy 2 Mechanics Backport
- **Architecture**: Super Mario Galaxy 2 is built on the identical underlying framework (`JSystem`, `LiveActor`, `MRUtil`, `Nerve`, `CollisionDirector`).
- **Decomp Solution**:
  - Port actor source files from SMG2 decompilation (Yoshi ride physics, Cloud Flower platform generation, Rock Mushroom roll acceleration).
  - Register them in `decomp/src/Game/Scene/SceneNameObjListExecutor.cpp` to create hybrid custom levels or run SMG2 galaxies natively within the engine.

#### B. True Two-Player Cooperative Multiplayer
- **Problem**: Player 2 in SMG1 is restricted to an on-screen Star Pointer assistant.
- **Decomp Solution**:
  - Instantiate a second `MarioActor` instance (configured with Luigi or Rosalina model resources and separate controller channel `WPAD_CHAN1`).
  - Implement split-screen viewport partitioning in `CameraContext` or shared-screen dynamic framing.
  - Target files:
    - `decomp/src/Game/Player/MarioActor.cpp`
    - `decomp/src/Game/Scene/GameScene.cpp`
    - `decomp/src/Game/Camera/CameraContext.cpp`

#### C. Integrated In-Engine Developer & Practice Suite
- **Decomp Solution**:
  - Expose Nintendo's internal developer tooling:
    - `LayoutViewer`: Inspect and edit 2D UI panes in real time.
    - Free-flight debug camera with collision freeze.
    - Live gravity vector visualizer drawing directional vectors on planets.
    - Real-time collision triangle mesh wireframe rendering.
  - Target files:
    - `decomp/src/Game/Screen/LayoutViewer.cpp`
    - `decomp/src/Game/Util/ObjUtil.cpp`
    - `platform/src/gx/vk_renderer.cpp`

---

## 4. Phased Execution Plan

```
Phase 1: Camera & Ultrawide Fixes  -->  Phase 2: 90/120Hz Decoupling  -->  Phase 3: Vulkan Tile Subpasses  -->  Phase 4: Gameplay & Co-op
   - Dual-stick 360 camera                 - Fixed 60Hz physics tick             - Transient tile memory                  - SMG2 mechanics backport
   - Frustum clipping expansion            - Variable render interpolation       - SSAO post-pass pass                    - True 2-player local co-op
   - In-engine debug free cam              - Parallel collision threads          - Direct ASTC texture load               - Custom galaxy creation
```

### Phase 1: Camera Modernization & Ultrawide Clipping Expansion
1. **Unconstrained Right-Stick Camera**:
   - In `decomp/src/Game/Camera/CameraDirector.cpp`, add an analog orbit override method that intercepts camera updates when right-stick deflection exceeds deadzone threshold (`0.15f`).
   - Calculate spherical orbit coordinates (`theta`, `phi`, `distance`) around `MarioActor::mPosition`.
2. **Frustum Culling Expansion**:
   - In `decomp/src/Game/LiveActor/ClippingActorInfo.cpp`, update `ClippingActorInfo::judgeClipping()` to accept dynamic aspect ratios and expand horizontal culling margins from 16:9 to match current display aspect ratio (e.g. 20:9).
3. **Verification**:
   - Test camera in tight galaxies (e.g., Good Egg Galaxy house, Battlerock tunnels) and verify zero actor pop-in at ultra-wide edges.

### Phase 2: Frame Pacing, 90Hz/120Hz Display & Multithreading
1. **Fixed Physics Tick vs Render Interpolation**:
   - In `platform/src/main.cpp` and `mobile_app.cpp`, decouple `GameSystem::frameLoop()` into an accumulator-based loop:
     ```cpp
     const double dt = 1.0 / 60.0;
     while (accumulator >= dt) {
         gameUpdate();
         accumulator -= dt;
     }
     renderInterpolated(accumulator / dt);
     ```
   - Store `mPrevPosition` and `mPrevRotation` in `LiveActor` to blend transforms during render submission.
2. **Worker Job Queue for Collisions & Particles**:
   - Implement a lightweight task graph using a fixed thread pool (`std::thread`, work stealing).
   - Dispatch `CollisionDirector::update()` across active collision parts concurrently.
3. **Verification**:
   - Verify 90 FPS and 120 FPS display pacing on high-refresh Android devices with unchanged platforming jump arcs and enemy speeds.

### Phase 3: Mobile Vulkan Optimizations & Enhanced Shading
1. **Transient Subpass Implementation**:
   - Modify `vk_renderer.cpp` to use Vulkan subpass dependencies for EFB color/depth sampling.
   - Configure attachments with `VK_ATTACHMENT_LOAD_OP_DONT_CARE` and `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`.
2. **Post-Processing SSAO Pass**:
   - Add a fullscreen compute or fragment pass before swapchain presentation.
   - Sample the existing depth attachment to apply ambient occlusion in planet crevices and geometry junctions.
3. **Verification**:
   - Profile with Snapdragon Profiler / Mali Graphics Debugger to measure DRAM bandwidth reduction and verify ambient occlusion quality.

### Phase 4: Mechanics Backport, Co-op & Modding
1. **Developer Overlay Suite**:
   - Build a toggleable in-engine debug menu in `platform/mobile/mobile_ui.cpp` exposing collision wireframes, level warp selectors, and gravity visualizers.
2. **Second Playable Character (Co-op)**:
   - Instantiate a secondary player actor inheriting from `MarioActor` using Luigi's parameter table (`LuigiParam`).
   - Assign pad channel 1 to player 2 gamepad.
3. **SMG2 Content Integration**:
   - Port SMG2 ride actor interfaces (`Yoshi.cpp`) into `decomp/src/Game/Ride/` and hook into actor sensor interaction messages.

---

## 5. Summary Table of Target Codebases

| Component | Target Source Files | Primary Benefit |
|---|---|---|
| **Physics Tick** | `decomp/src/Game/System/GameSystem.cpp`, `LiveActor.cpp` | Native 90 FPS / 120 FPS high-refresh support without speedup |
| **Clipping** | `decomp/src/Game/LiveActor/ClippingActorInfo.cpp` | Zero pop-in on 19.5:9, 20:9, 21:9 ultrawide screens |
| **Camera** | `decomp/src/Game/Camera/CameraDirector.cpp`, `CameraMan.cpp` | Full 360-degree analog right-stick control everywhere |
| **Haptics & IMU** | `platform/mobile/mobile_app.cpp`, `wpad.cpp` | Zero-latency pointer/tilt steering and rich physical haptics |
| **Vulkan Subpasses**| `platform/src/gx/vk_renderer.cpp`, `vk_target.cpp` | 40% memory bandwidth savings and reduced thermal throttling |
| **Post-Processing**| `platform/src/gx/vk_shadergen.cpp`, `vk_renderer.cpp` | Ambient occlusion (SSAO), bloom, and dynamic depth of field |
| **Multiplayer** | `decomp/src/Game/Player/MarioActor.cpp`, `GameScene.cpp` | Real 2-player cooperative split-screen / network gameplay |
| **Expansion** | `decomp/src/Game/Ride/`, `SceneNameObjListExecutor.cpp` | Backporting SMG2 actors (Yoshi, Cloud, Rock) and custom levels |
