// Frame recordings: the game thread turns the GX command stream into a
// self-contained list of render commands (register writes, draws with
// decoded vertices, texture bindings, EFB copies).  The render thread
// replays a finished recording, possibly several times (per eye, per
// display refresh), without touching emulated memory.
#pragma once

#include <stdint.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace gpu {

// std::allocator that leaves new elements uninitialized: the recorder
// writes every word of the frame buffers right after growing them.
template <class T>
struct NoInitAllocator : std::allocator<T> {
    template <class U>
    struct rebind {
        using other = NoInitAllocator<U>;
    };
    NoInitAllocator() = default;
    template <class U>
    NoInitAllocator(const NoInitAllocator<U>&) noexcept {}
    template <class U>
    void construct(U* p) noexcept {
        ::new ((void*)p) U;
    }
    template <class U, class... Args>
    void construct(U* p, Args&&... args) {
        ::new ((void*)p) U(static_cast<Args&&>(args)...);
    }
};
using WordBuffer = std::vector<uint32_t, NoInitAllocator<uint32_t>>;

// Decoded vertex layout (flags stored with each draw).  Every attribute
// occupies whole 32-bit words: position (3 floats), position matrix index
// (1 uint), normal (3), binormal (3), tangent (3), color0/1 (1 word each,
// RGBA8), tex coord n (2 floats each), tex matrix indices (2 words, 8 x u8).
enum VtxFlags : uint32_t {
    VF_POSMTX = 1u << 0,
    VF_NRM = 1u << 1,
    VF_NBT = 1u << 2,
    VF_CLR0 = 1u << 3,
    VF_CLR1 = 1u << 4,
    VF_TEX0 = 1u << 5,  // VF_TEX0 << n for tex coord n
    VF_TEXMTX = 1u << 13,
};

inline uint32_t vtxWords(uint32_t flags) {
    uint32_t w = 3;
    if (flags & VF_POSMTX) w += 1;
    if (flags & VF_NRM) w += 3;
    if (flags & VF_NBT) w += 6;
    if (flags & VF_CLR0) w += 1;
    if (flags & VF_CLR1) w += 1;
    for (int i = 0; i < 8; i++) {
        if (flags & (VF_TEX0 << i)) w += 2;
    }
    if (flags & VF_TEXMTX) w += 2;
    return w;
}

// A decoded texture shared between the recorder and the renderer.
struct TexImage {
    uint32_t width = 0, height = 0, levels = 1;
    std::vector<uint32_t> rgba;  // all levels, level 0 first
    uint64_t version = 0;        // bumped when the pixels change
    // Renderer-side upload state (render thread only).
    uint32_t glTex = 0;
    uint64_t glVersion = 0;
    // Vulkan-side upload state (render thread only).  Opaque handles so this
    // header stays independent of the Vulkan headers.
    uint64_t vkImage = 0;
    uint64_t vkMemory = 0;
    uint64_t vkView = 0;
    uint64_t vkVersion = 0;

    ~TexImage();  // queues glTex / the Vulkan images for deletion on the render thread
};

// GL objects released by other threads, deleted by the render thread.
void queueGlTextureDelete(uint32_t tex);

// Vulkan objects released by other threads, deleted by the render thread.
void queueVkTextureDelete(uint64_t image, uint64_t memory, uint64_t view);

enum Cmd : uint32_t {
    CMD_XF = 1,        // addr, count, values[count]
    CMD_BP,            // reg << 24 | value
    CMD_DRAW,          // primitive, flags, first word, vertex count
    CMD_TEX,           // unit, texture index into Frame::textures (or ~0u for an EFB copy)
    CMD_TEX_EFB,       // unit, EFB copy id, width, height, destination address
    CMD_EFB_COPY,      // copy id, BP 0x49, 0x4A, 0x4B, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52
    CMD_MARKER,        // kind
    CMD_CAMERA,        // 33 words: view[12] (world->view, 3x4 rows), pos[3], watch[3], up[3], watchUp[3], fovy, flags, aspect, player[3], playerUp[3]
};

enum : uint32_t { kCameraWords = 33 };

enum Marker : uint32_t {
    MARK_HUD_BEGIN = 1,
    MARK_HUD_END = 2,
    MARK_SKY_BEGIN = 3,
    MARK_SKY_END = 4,
    MARK_PLAYER_BEGIN = 5,
    MARK_PLAYER_END = 6,
    MARK_SCENE_DEPTH = 7,
    MARK_POINTER_BEGIN = 8,
    MARK_POINTER_END = 9,
};

struct Frame {
    // GPU register state when the frame started (replay starts from here).
    uint32_t startBp[256];
    uint32_t startTevReg[8];
    uint32_t startTevKonst[8];
    uint32_t startXfRegs[256];
    float startXfMem[0x1000];

    WordBuffer cmds;
    WordBuffer verts;
    std::vector<std::shared_ptr<TexImage>> textures;
    uint64_t number = 0;
    uint32_t xfbCopies = 0;  // EFB->XFB copies in this frame
    uint32_t draws = 0;

    void clear() {
        cmds.clear();
        verts.clear();
        textures.clear();
        xfbCopies = 0;
        draws = 0;
    }
};

// Hand-off of finished recordings from the game thread to the renderer.
class FrameQueue {
public:
    // Game thread: publishes `f` as the latest complete frame and returns an
    // empty frame to record the next one into.
    std::unique_ptr<Frame> publish(std::unique_ptr<Frame> f);
    // Render thread: the most recent complete frame (shared so it can be
    // replayed while the game records the next ones).
    std::shared_ptr<const Frame> latest();
    uint64_t latestNumber() const { return mLatestNumber.load(); }
    // Called on the game thread after each publish (the renderer starts
    // preparing the frame at once instead of at its next refresh).
    void setListener(void (*fn)()) { mListener.store(fn); }

private:
    std::mutex mLock;
    std::shared_ptr<const Frame> mLatest;
    std::vector<std::unique_ptr<Frame>> mFree;
    std::atomic<uint64_t> mLatestNumber{0};
    std::atomic<void (*)()> mListener{nullptr};
};

FrameQueue& frameQueue();

// Installs the recording backend (game thread side of the renderer).
void useRecorderBackend();

}  // namespace gpu
