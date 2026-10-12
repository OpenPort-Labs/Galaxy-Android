// GLES 3.2 replay of recorded GX frames.
#include "gl_renderer.h"

#include <EGL/egl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gl_shadergen.h"
#include "gpu.h"
#include "port/heap_routing.h"
#include "port/port.h"

extern "C" void port_headless_write_png(const char* path, const unsigned char* rgba, int w, int h);

namespace gpu {

int gDebugStopAfterDraws = debugEnv("PETARI_GLSTOP") ? atoi(debugEnv("PETARI_GLSTOP")) : -1;

// ---------------------------------------------------------------------------
// Deferred GL deletes (texture images die on whichever thread drops them)
// ---------------------------------------------------------------------------
static std::mutex sDeleteLock;
static std::vector<GLuint> sDeleteTextures;

void queueGlTextureDelete(uint32_t tex) {
    if (tex) {
        std::lock_guard<std::mutex> lock(sDeleteLock);
        sDeleteTextures.push_back(tex);
    }
}

TexImage::~TexImage() {
    queueGlTextureDelete(glTex);
}

static void flushDeletes() {
    std::vector<GLuint> list;
    {
        std::lock_guard<std::mutex> lock(sDeleteLock);
        list.swap(sDeleteTextures);
    }
    if (!list.empty()) {
        glDeleteTextures((GLsizei)list.size(), list.data());
    }
}

namespace {

inline float bitsToFloat(u32 v) {
    float f;
    memcpy(&f, &v, 4);
    return f;
}

void rgba8ToVec(u32 c, float* out) {  // R in the high byte
    out[0] = ((c >> 24) & 255) / 255.0f;
    out[1] = ((c >> 16) & 255) / 255.0f;
    out[2] = ((c >> 8) & 255) / 255.0f;
    out[3] = (c & 255) / 255.0f;
}

inline float s11(u32 v) {
    int x = (int)(v & 0x7FF);
    if (x & 0x400) x -= 0x800;
    return x / 255.0f;
}

GLuint compileShader(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        port_log("gl: %s shader compile failed: %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        // Dump the source in chunks (logcat lines are limited).
        for (size_t i = 0; i < src.size(); i += 900) {
            port_log("gl: src: %s", src.substr(i, 900).c_str());
        }
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glProgramParameteri(p, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        port_log("gl: program link failed: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// Uniform/vertex data uploaded once per prepared frame.
struct ByteBuffer {
    std::vector<u8> data;
    size_t push(const void* p, size_t n, size_t align) {
        size_t off = (data.size() + align - 1) & ~(align - 1);
        data.resize(off + n);
        memcpy(data.data() + off, p, n);
        return off;
    }
};

enum ItemType : u8 { IT_DRAW, IT_COPY, IT_MARKER };

// IT_DRAW is a batch: consecutive GX draws with identical GL state, drawn
// with one glDrawElements.  Per-draw transforms and constants are reached
// through each vertex's draw record.
struct Item {
    ItemType type;
    // Batch
    u32 uid;           // index into Prepared::uids
    GLuint program;    // resolved by the render thread
    GLenum prim;       // GL_TRIANGLES, GL_LINES or GL_POINTS
    u8 useVr, hud, hudSafe;
    u8 player;         // between the player markers (SpaceWarp stencil tag)
    u32 flags;
    u32 firstWord;     // first vertex, as a word offset into the frame's vertex data
    u32 endWord;       // end of the last merged draw's vertices
    u32 firstOrdinal;  // first vertex ordinal (record index stream)
    u32 idxOffset, idxCount;
    u32 draws;         // GX draws in the batch
    u32 texMask;       // texture units the program samples
    // GL state
    u8 blend, colorMask, depthTest, depthWrite, cull, dstAlpha;
    GLenum srcRGB, dstRGB, eq, depthFunc, cullFace;
    float dstAlphaValue;
    float vp[4];  // x, y, w, h in native EFB coordinates (top-left origin)
    int sc[4];    // scissor left, top, right, bottom (native EFB)
    u32 tex[8];   // TexImage index, or 0x80000000 | EFB copy address >> 5; ~0u unused
    u32 texMode0[8], texMode1[8];
    // Copy: BP 0x49..0x52 values plus PE state at that point.
    u32 copy[10];
    u32 copyPixFmt, copyCmode, copyZmode;
    u32 marker;
};

bool sameBatchState(const Item& a, const Item& b) {
    if (a.uid != b.uid || a.prim != b.prim || a.useVr != b.useVr || a.hud != b.hud || a.hudSafe != b.hudSafe || a.player != b.player || a.flags != b.flags) return false;
    if (a.blend != b.blend || a.colorMask != b.colorMask || a.depthTest != b.depthTest || a.depthWrite != b.depthWrite || a.cull != b.cull ||
        a.dstAlpha != b.dstAlpha)
        return false;
    if ((a.blend || a.dstAlpha) && (a.srcRGB != b.srcRGB || a.dstRGB != b.dstRGB || a.eq != b.eq)) return false;
    if (a.dstAlpha && a.dstAlphaValue != b.dstAlphaValue) return false;
    if (a.depthTest && a.depthFunc != b.depthFunc) return false;
    if (a.cull && a.cullFace != b.cullFace) return false;
    if (memcmp(a.vp, b.vp, sizeof(a.vp)) != 0 || memcmp(a.sc, b.sc, sizeof(a.sc)) != 0) return false;
    if (a.texMask != b.texMask) return false;
    for (int t = 0; t < 8; t++) {
        if ((a.texMask & (1u << t)) && (a.tex[t] != b.tex[t] || a.texMode0[t] != b.texMode0[t] || a.texMode1[t] != b.texMode1[t])) return false;
    }
    return true;
}

// Texture units read by the TEV stages and indirect stages.
u32 usedTexUnits(const u32* bp) {
    u32 gen = bp[0x00];
    u32 stages = ((gen >> 10) & 15) + 1, ind = (gen >> 16) & 7;
    u32 mask = 0;
    for (u32 st = 0; st < stages; st++) {
        u32 order = (bp[0x28 + st / 2] >> ((st & 1) * 12)) & 0xFFF;
        if ((order >> 6) & 1) mask |= 1u << (order & 7);
    }
    for (u32 st = 0; st < ind; st++) mask |= 1u << ((bp[0x27] >> (st * 6)) & 7);
    return mask;
}

u64 hashWords(const u32* w, size_t n) {
    u64 h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ w[i]) * 1099511628211ull;
    return h;
}

struct CopyTex {
    GLuint tex = 0, fbo = 0;
    int w = 0, h = 0;
    u64 frame = 0;  // frame whose replay last wrote it
};

// EFB copies land in textures keyed like texture memory: by destination
// address, one set per render slot (flat view, left eye, right eye), so a
// texture read before this frame's copy to its address still holds the
// previous frame's copy of the same view, as on the console.  The EFB->XFB
// copy (flat view only) has a key of its own.
inline u64 copyKey(u32 slot, u32 addr) { return ((u64)slot << 32) | (addr >> 5); }
const u64 kXfbCopyKey = ~0ull;

}  // namespace

// A frame turned into batches and shader data (gl_shadergen.h).  Built from
// a recording on any thread; uploaded and replayed by the render thread.
struct Prepared {
    std::shared_ptr<const Frame> frame;
    u64 number = 0;
    std::vector<Item> items;
    std::vector<ShaderUid> uids;
    std::vector<GLuint> cutProgs;  // cutaway variants of uids' programs, 0 until resolved
    std::vector<float> rows;    // vec4 matrix rows
    std::vector<u32> recs;      // kRecWords per draw record
    std::vector<float> states;  // vec4 light / projection / pixel states
    std::vector<u32> vtxRec;    // draw record per vertex ordinal
    std::vector<u32> indices;
    CameraInfo cam;
    u32 clearAR = 0, clearGB = 0, clearZ = 0xFFFFFF;
    u32 nativeW = 640, nativeH = 456;
};

void logItems(const Prepared& p);

// Replays a recording's register writes to build a Prepared.
struct Builder {
    std::vector<Item> items;
    std::vector<ShaderUid> uids;
    std::vector<float> rows, states;
    std::vector<u32> recs, vtxRec, indices;
    CameraInfo cam;
    u32 clearAR = 0, clearGB = 0, clearZ = 0xFFFFFF;
    u32 nativeW = 640, nativeH = 456;

    // Shadows of the GPU registers.
    u32 bp[256];
    u32 tevReg[8], tevKonst[8];
    u32 xfRegs[256];
    float xfMem[0x1000];
    u32 boundTex[8];
    bool inHud = false;
    bool inSky = false;     // between sky markers: drawn around the game camera
    bool inPlayer = false;  // between player markers
    bool inPointer = false; // between pointer markers: the pointer's 2D cursor
    // The shader key of the last draw, reused while nothing it depends on
    // changed (hashing a key for each of ~40,000 draws was most of the
    // preparation time).
    bool uidDirty = true;
    u32 lastUid = 0, lastUidFlags = ~0u;
    bool lastUidHud = false;
    // Draws in a row with no command in between (the strips of a model's
    // display list) share all state: a draw after one of the same format
    // joins its batch without the state being worked out again.
    bool drawStateDirty = true;
    bool lastDrawReusable = false;  // not with a shader key per draw
    u32 lastDrawFlags = 0, lastDrawRec = 0;
    GLenum lastDrawPrim = 0;

    std::unordered_map<u64, std::vector<u32>> stateIndex;  // content hash -> bases
    std::unordered_map<ShaderUid, u32, ShaderUidHash> uidIndex;
    // Snapshot bases in rows[] of each 3-row matrix slot (~0u: changed since).
    u32 posSlot[22], nrmSlot[11], postSlot[64];
    u32 lightBase = ~0u, projBase = ~0u, pixBase = ~0u;

    void build(std::shared_ptr<const Frame> frame, Prepared& out);
    u32 screenTexGens(const Frame& f, const Item& it, u32 first, const ShaderUid& uid);
    u32 uidFor(const ShaderUid& uid);
    u32 appendRows(const float* src, int n, int stride);
    u32 addState(const float* data, size_t vec4s);
    void invalidateXf(u32 a0, u32 a1);
    u32 drawRecord(bool useVr, bool sky, bool pointer, bool dualTex, u32 numTexGens);
    void appendIndices(u32 kind, u32 base, u32 count);
    void fillLights(LightState& b);
    void fillProj(ProjState& b);
    void fillPix(PixelState& b);
};

static std::string sShaderCachePath;

struct Renderer::Impl {
    bool ready = false;
    // Programs by shader key (0: failed to build), shared with the compile
    // thread; guarded by progLock.
    std::mutex progLock;
    std::unordered_map<ShaderUid, GLuint, ShaderUidHash> programs;
    std::unordered_set<ShaderUid, ShaderUidHash> pendingPrograms;
    std::deque<ShaderUid> compileQueue;
    std::condition_variable compileCv;
    std::thread compiler;
    FILE* cacheOut = nullptr;
    // Cached programs whose code the generator now writes differently: the
    // compile thread rebuilds them as soon as it starts (during the title
    // screen) rather than when a scene first needs them.
    std::vector<ShaderUid> staleUids;
    std::unordered_map<u32, GLuint> vaos;
    std::unordered_map<u64, GLuint> samplers;
    std::unordered_map<u64, CopyTex> copies;  // by copyKey()
    u32 copySlot = 0;                          // of the render in progress
    // Buffers of the current frame (one of frameBufs).
    GLuint vbo = 0, recVbo = 0, ibo = 0, eyeUbo = 0;
    // The per-render camera block rotates through a few buffers, so writing
    // it never waits for the GPU to finish the renders before.
    GLuint eyeUbos[8] = {};
    int eyeUboNext = 0;
    GLuint rowBuf = 0, recBuf = 0, stateBuf = 0;
    // Each frame's data goes into the next of three buffer sets, which the
    // GPU finished drawing from frames ago: refilling buffers still in use
    // made the driver wait for the GPU (up to 15 ms per frame at 120 Hz).
    struct FrameBuffers {
        GLuint buf[6] = {};    // vbo, recVbo, ibo, rowBuf, recBuf, stateBuf
        size_t cap[6] = {};    // bytes allocated
    };
    FrameBuffers frameBufs[3];
    int frameBufNext = 0;
    GLuint copyProgram = 0, copyVao = 0;
    GLint copyRectLoc = -1, copyModeLoc = -1, copyDepthLoc = -1;

    // The frame being replayed.
    std::unique_ptr<Prepared> cur;
    Builder syncBuilder;  // setFrame()

    // Background preparation (update()).
    std::thread worker;
    std::mutex workLock;
    std::condition_variable workCv;
    std::shared_ptr<const Frame> wanted;  // newest frame to build
    std::unique_ptr<Prepared> built;      // finished, not yet uploaded
    std::unique_ptr<Prepared> spare;      // recycled storage
    u64 lastRequested = 0;
    Builder workerBuilder;

    void init();
    // The program for `uid`: built now if `sync`, otherwise queued for the
    // compile thread (returns 0 until it is ready).
    GLuint program(const ShaderUid& uid, bool sync);
    GLuint buildProgram(const ShaderUid& uid);
    static void generateSources(const ShaderUid& uid, std::string* vs, std::string* fs);
    static u64 sourceHash(const ShaderUid& uid);
    void loadShaderCache();
    void storeBinary(const ShaderUid& uid, GLuint prog);
    void startCompiler();
    std::unordered_map<GLuint, GLint> ordinalLocs;
    GLint firstOrdinalLoc(GLuint prog) {
        auto it = ordinalLocs.find(prog);
        if (it != ordinalLocs.end()) return it->second;
        GLint loc = glGetUniformLocation(prog, "u_firstOrdinal");
        ordinalLocs[prog] = loc;
        return loc;
    }
    GLuint vao(u32 flags);
    GLuint sampler(u32 mode0, u32 mode1, u32 levels);
    void upload(Prepared& p, bool syncPrograms);
    void workerMain();
    void requestLatest();
    void execute(EfbTarget& efb, const EyeView* eye, EfbTarget* hud, HudMode hudMode, Renderer& r);
    void doCopy(const Item& it, EfbTarget& src, Renderer& r, bool vr);
    CopyTex& copyTex(u64 key, int w, int h);
    void evictCopies();
    bool snapshotTaken = false;    // the last execute() filled eye->depthSnapshot
    int hudDraws = 0;              // draws the last execute() sent to the HUD target
    bool dumpingCopies = false;    // PETARI_COPYDUMP, this replay
    u64 copyDumpFrame = ~0ull;     // last frame dumped
};

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
static const char* kCopyVs = R"(#version 320 es
precision highp float;
uniform vec4 u_rect;  // src rect in target UV: x0, y0, x1, y1
out vec2 v_uv;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    // Texture rows run top-down like texture memory (row 0 = top of the
    // copied rectangle); the EFB is stored bottom-up.
    v_uv = mix(u_rect.xy, u_rect.zw, vec2(p.x, 1.0 - p.y));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

static const char* kCopyFs = R"(#version 320 es
precision highp float;
layout(binding = 0) uniform sampler2D s_color;
layout(binding = 1) uniform sampler2D s_depth;
uniform int u_mode;
in vec2 v_uv;
out vec4 o;
void main() {
    vec4 c = texture(s_color, v_uv);
    float y = dot(c.rgb, vec3(0.257, 0.504, 0.098)) + 16.0 / 255.0;
    if (u_mode == 0) o = c;                              // RGBA8 / RGB5A3
    else if (u_mode == 1) o = vec4(c.rgb, 1.0);          // RGB565
    else if (u_mode == 2) o = vec4(y);                   // I4/I8
    else if (u_mode == 3) o = vec4(y, y, y, c.a);        // IA4/IA8
    else if (u_mode == 4) o = vec4(c.r);                 // R4/R8
    else if (u_mode == 5) o = vec4(c.r, c.r, c.r, c.a);  // RA4/RA8
    else if (u_mode == 6) o = vec4(c.a);                 // A8
    else if (u_mode == 7) o = vec4(c.g);                 // G8
    else if (u_mode == 8) o = vec4(c.b);                 // B8
    else if (u_mode == 9) o = vec4(c.g, c.g, c.g, c.r);  // RG8
    else if (u_mode == 10) o = vec4(c.b, c.b, c.b, c.g); // GB8
    else {                                               // Z copies
        float z = texture(s_depth, v_uv).r;
        o = vec4(z, fract(z * 256.0), fract(z * 65536.0), z);
    }
}
)";

void Renderer::Impl::init() {
    for (FrameBuffers& fb : frameBufs) {
        glGenBuffers(6, fb.buf);
    }
    glGenBuffers(8, eyeUbos);
    for (GLuint b : eyeUbos) {
        glBindBuffer(GL_UNIFORM_BUFFER, b);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(EyeBlock), nullptr, GL_DYNAMIC_DRAW);
    }

    GLint attribs = 0, vsBlocks = 0, fsBlocks = 0, blockSize = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &attribs);
    glGetIntegerv(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, &vsBlocks);
    glGetIntegerv(GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS, &fsBlocks);
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &blockSize);
    port_log("gl: %d vertex attribs, %d/%d storage blocks (vs/fs), %d MB max block", attribs, vsBlocks, fsBlocks, blockSize >> 20);
    GLint numExt = 0, numBinFmt = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &numExt);
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &numBinFmt);
    std::string exts;
    for (GLint i = 0; i < numExt; i++) {
        const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (strstr(e, "parallel") || strstr(e, "binary") || strstr(e, "multiview") || strstr(e, "buffer_storage") || strstr(e, "QCOM") || strstr(e, "foveat")) {
            exts += e;
            exts += ' ';
        }
    }
    port_log("gl: %d program binary formats; extensions: %s", numBinFmt, exts.c_str());
    if (vsBlocks < 4 || fsBlocks < 1) {
        port_log("gl: this GPU lacks the vertex attributes or storage blocks the renderer needs");
    }

    GLuint vs = compileShader(GL_VERTEX_SHADER, kCopyVs), fs = compileShader(GL_FRAGMENT_SHADER, kCopyFs);
    copyProgram = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    copyRectLoc = glGetUniformLocation(copyProgram, "u_rect");
    copyModeLoc = glGetUniformLocation(copyProgram, "u_mode");
    glGenVertexArrays(1, &copyVao);
    loadShaderCache();
    ready = true;
}

// ---------------------------------------------------------------------------
// Programs: disk cache of linked binaries + background compilation
// ---------------------------------------------------------------------------
static const u32 kCacheMagic = 0x32434750;  // "PGC2"

void Renderer::Impl::loadShaderCache() {
    if (sShaderCachePath.empty()) {
        return;
    }
    FILE* in = fopen(sShaderCachePath.c_str(), "rb");
    size_t loaded = 0, stale = 0;
    if (in) {
        u32 magic = 0;
        if (fread(&magic, 4, 1, in) == 1 && magic == kCacheMagic) {
            for (;;) {
                u32 hdr[3];  // key size, binary format, binary length
                u64 srcHash = 0;
                if (fread(hdr, 4, 3, in) != 3 || hdr[0] != sizeof(ShaderUid) || hdr[2] > (64u << 20)) break;
                ShaderUid uid;
                std::vector<u8> bin(hdr[2]);
                if (fread(&srcHash, 8, 1, in) != 1 || fread(&uid, sizeof(uid), 1, in) != 1 || fread(bin.data(), 1, bin.size(), in) != bin.size()) break;
                if (srcHash != sourceHash(uid)) {
                    stale++;
                    staleUids.push_back(uid);
                    continue;
                }
                GLuint prog = glCreateProgram();
                glProgramBinary(prog, hdr[1], bin.data(), (GLsizei)bin.size());
                GLint ok = 0;
                glGetProgramiv(prog, GL_LINK_STATUS, &ok);
                if (ok) {
                    programs[uid] = prog;
                    loaded++;
                } else {
                    glDeleteProgram(prog);
                    stale++;
                    staleUids.push_back(uid);
                }
            }
        }
        fclose(in);
    }
    // Rewrite the cache with the entries that loaded; new programs are
    // appended as they are built.
    cacheOut = fopen(sShaderCachePath.c_str(), "wb");
    if (cacheOut) {
        fwrite(&kCacheMagic, 4, 1, cacheOut);
        for (auto& kv : programs) {
            storeBinary(kv.first, kv.second);
        }
    }
    port_log("gl: shader cache %s: %zu programs loaded, %zu stale", sShaderCachePath.c_str(), loaded, stale);
}

void Renderer::Impl::storeBinary(const ShaderUid& uid, GLuint prog) {
    if (!cacheOut || !prog) {
        return;
    }
    GLint len = 0;
    glGetProgramiv(prog, GL_PROGRAM_BINARY_LENGTH, &len);
    if (len <= 0) {
        return;
    }
    std::vector<u8> bin((size_t)len);
    GLenum format = 0;
    GLsizei got = 0;
    glGetProgramBinary(prog, len, &got, &format, bin.data());
    u32 hdr[3] = {(u32)sizeof(ShaderUid), (u32)format, (u32)got};
    u64 srcHash = sourceHash(uid);
    fwrite(hdr, 4, 3, cacheOut);
    fwrite(&srcHash, 8, 1, cacheOut);
    fwrite(&uid, sizeof(uid), 1, cacheOut);
    fwrite(bin.data(), 1, (size_t)got, cacheOut);
    fflush(cacheOut);
}

void Renderer::Impl::startCompiler() {
    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLContext share = eglGetCurrentContext();
    EGLint cfgId = 0, n = 0;
    eglQueryContext(dpy, share, EGL_CONFIG_ID, &cfgId);
    const EGLint cfgAttr[] = {EGL_CONFIG_ID, cfgId, EGL_NONE};
    EGLConfig cfg = nullptr;
    eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n);
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, share, ctxAttr);
    const EGLint pb[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb);
    if (ctx == EGL_NO_CONTEXT || surf == EGL_NO_SURFACE) {
        port_log("gl: no shared context for background compiles (0x%x); compiling inline", eglGetError());
        return;
    }
    PortHostAllocScope hostAlloc;
    compiler = std::thread([this, dpy, ctx, surf] {
        PortHostAllocScope scope;
        eglMakeCurrent(dpy, surf, surf, ctx);
        for (;;) {
            ShaderUid uid;
            {
                std::unique_lock<std::mutex> lock(progLock);
                compileCv.wait(lock, [&] { return !compileQueue.empty(); });
                uid = compileQueue.front();
                compileQueue.pop_front();
            }
            GLuint prog = buildProgram(uid);
            glFinish();  // complete before the render context uses it
            std::lock_guard<std::mutex> lock(progLock);
            programs[uid] = prog;
            pendingPrograms.erase(uid);
            storeBinary(uid, prog);
        }
    });
    std::lock_guard<std::mutex> lock(progLock);
    for (const ShaderUid& uid : staleUids) {
        if (!programs.count(uid) && pendingPrograms.insert(uid).second) {
            compileQueue.push_back(uid);
        }
    }
    if (!staleUids.empty()) {
        port_log("gl: rebuilding %zu cached programs in the background", staleUids.size());
        compileCv.notify_one();
    }
    staleUids.clear();
}

GLuint Renderer::Impl::program(const ShaderUid& uid, bool sync) {
    {
        std::lock_guard<std::mutex> lock(progLock);
        auto it = programs.find(uid);
        if (it != programs.end()) {
            return it->second;
        }
        // Queued for the compile thread; without one it is built right here.
        if (!sync && compiler.joinable()) {
            if (pendingPrograms.insert(uid).second) {
                compileQueue.push_front(uid);  // ahead of any background rebuilds
                compileCv.notify_one();
            }
            return 0;
        }
    }
    GLuint prog = buildProgram(uid);
    std::lock_guard<std::mutex> lock(progLock);
    programs[uid] = prog;
    pendingPrograms.erase(uid);
    storeBinary(uid, prog);
    return prog;
}

void Renderer::Impl::generateSources(const ShaderUid& uid, std::string* vs, std::string* fs) {
    static std::mutex genLock;  // the generator keeps scratch buffers
    std::lock_guard<std::mutex> lock(genLock);
    *vs = genVertexShader(uid);
    *fs = genFragmentShader(uid);
}

// Identifies the generated code, so cached binaries from an older generator
// are not reused.
u64 Renderer::Impl::sourceHash(const ShaderUid& uid) {
    std::string vs, fs;
    generateSources(uid, &vs, &fs);
    u64 h = 1469598103934665603ull;
    for (char ch : vs) h = (h ^ (u8)ch) * 1099511628211ull;
    for (char ch : fs) h = (h ^ (u8)ch) * 1099511628211ull;
    return h;
}

GLuint Renderer::Impl::buildProgram(const ShaderUid& uid) {
    GLuint prog = 0;
    std::string vsrc, fsrc;
    generateSources(uid, &vsrc, &fsrc);
    if (debugEnv("PETARI_GLSRC")) {
        std::string vbody = vsrc.substr(vsrc.find("layout(location"));
        for (char& ch : vbody) {
            if (ch == 10) ch = 124;
        }
        for (size_t i = 0; i < vbody.size(); i += 900) {
            port_log("glvs: %s", vbody.substr(i, 900).c_str());
        }
        size_t start = fsrc.find("void main()");
        std::string body = fsrc.substr(start == std::string::npos ? 0 : start);
        for (char& ch : body) {
            if (ch == 10) ch = 124;
        }
        for (size_t i = 0; i < body.size(); i += 900) {
            port_log("glsrc: %s", body.substr(i, 900).c_str());
        }
    }
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsrc);
    if (vs && fs) {
        prog = linkProgram(vs, fs);
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    static std::atomic<int> built{0};
    if ((++built % 50) == 0) {
        port_log("gl: %d shader programs built", built.load());
    }
    return prog;
}

GLuint Renderer::Impl::vao(u32 flags) {
    auto it = vaos.find(flags);
    if (it != vaos.end()) {
        return it->second;
    }
    GLuint v;
    glGenVertexArrays(1, &v);
    glBindVertexArray(v);
    u32 off = 0;
    auto attr = [&](GLuint loc, GLint size, GLenum type, bool normalized, bool integer, u32 bytes) {
        glEnableVertexAttribArray(loc);
        if (integer) {
            glVertexAttribIFormat(loc, size, type, off);
        } else {
            glVertexAttribFormat(loc, size, type, normalized, off);
        }
        glVertexAttribBinding(loc, 0);
        off += bytes;
    };
    attr(0, 3, GL_FLOAT, false, false, 12);
    if (flags & VF_POSMTX) attr(1, 1, GL_UNSIGNED_INT, false, true, 4);
    if (flags & VF_NRM) attr(2, 3, GL_FLOAT, false, false, 12);
    if (flags & VF_NBT) {
        attr(3, 3, GL_FLOAT, false, false, 12);
        attr(4, 3, GL_FLOAT, false, false, 12);
    }
    if (flags & VF_CLR0) attr(5, 4, GL_UNSIGNED_BYTE, true, false, 4);
    if (flags & VF_CLR1) attr(6, 4, GL_UNSIGNED_BYTE, true, false, 4);
    for (int i = 0; i < 8; i++) {
        if (flags & (VF_TEX0 << i)) attr(7 + i, 2, GL_FLOAT, false, false, 8);
    }
    if (flags & VF_TEXMTX) attr(15, 2, GL_UNSIGNED_INT, false, true, 8);
    glBindVertexArray(0);
    vaos[flags] = v;
    return v;
}

GLuint Renderer::Impl::sampler(u32 mode0, u32 mode1, u32 levels) {
    u64 key = ((u64)(mode0 & 0xFFFFFF) << 32) | ((mode1 & 0xFFFF) << 8) | (levels > 1 ? 1 : 0);
    auto it = samplers.find(key);
    if (it != samplers.end()) {
        return it->second;
    }
    GLuint s;
    glGenSamplers(1, &s);
    static const GLenum wrap[4] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT};
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, wrap[mode0 & 3]);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_T, wrap[(mode0 >> 2) & 3]);
    glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, ((mode0 >> 4) & 1) ? GL_LINEAR : GL_NEAREST);
    u32 minf = (mode0 >> 5) & 7;
    GLenum min;
    bool mips = levels > 1;
    switch (minf) {
    case 0:
        min = GL_NEAREST;
        break;
    case 1:
        min = mips ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
        break;
    case 2:
        min = mips ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST;
        break;
    case 5:
        min = mips ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR;
        break;
    case 6:
        min = mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
        break;
    default:
        min = GL_LINEAR;
        break;
    }
    glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, min);
    glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, (mode1 & 0xFF) / 16.0f);
    glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, ((mode1 >> 8) & 0xFF) / 16.0f);
    samplers[key] = s;
    return s;
}

CopyTex& Renderer::Impl::copyTex(u64 key, int w, int h) {
    CopyTex& c = copies[key];
    if (c.tex && (c.w != w || c.h != h)) {
        glDeleteTextures(1, &c.tex);
        glDeleteFramebuffers(1, &c.fbo);
        c.tex = c.fbo = 0;
    }
    if (!c.tex) {
        glGenTextures(1, &c.tex);
        glBindTexture(GL_TEXTURE_2D, c.tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
        glGenFramebuffers(1, &c.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, c.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c.tex, 0);
        c.w = w;
        c.h = h;
    }
    c.frame = cur->number;
    return c;
}

// Copy targets of addresses (or views) no longer copied to: the recorder
// forgets a copy after 8 frames, and a scene's buffers are copied to every
// frame.
void Renderer::Impl::evictCopies() {
    const u64 kKeepFrames = 120;
    for (auto it = copies.begin(); it != copies.end();) {
        if (it->first != kXfbCopyKey && it->second.frame + kKeepFrames < cur->number) {
            glDeleteTextures(1, &it->second.tex);
            glDeleteFramebuffers(1, &it->second.fbo);
            it = copies.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// Uniform blocks from the register shadows
// ---------------------------------------------------------------------------
void Builder::fillLights(LightState& b) {
    for (int l = 0; l < 8; l++) {
        const float* m = xfMem + 0x600 + l * 16;
        u32 col;
        memcpy(&col, &m[3], 4);
        rgba8ToVec(col, b.lights[l][0]);
        for (int k = 0; k < 3; k++) {
            b.lights[l][1][k] = m[4 + k];
            b.lights[l][2][k] = m[7 + k];
            b.lights[l][3][k] = m[10 + k];
            b.lights[l][4][k] = m[13 + k];
        }
        b.lights[l][1][3] = b.lights[l][2][3] = b.lights[l][3][3] = b.lights[l][4][3] = 0.0f;
    }
    rgba8ToVec(xfRegs[0x0C], b.matColor[0]);
    rgba8ToVec(xfRegs[0x0D], b.matColor[1]);
    rgba8ToVec(xfRegs[0x0A], b.ambColor[0]);
    rgba8ToVec(xfRegs[0x0B], b.ambColor[1]);
}

void Builder::fillProj(ProjState& b) {
    float p[6];
    for (int i = 0; i < 6; i++) p[i] = bitsToFloat(xfRegs[0x20 + i]);
    memset(b.proj, 0, sizeof(b.proj));
    if (xfRegs[0x26] & 1) {  // orthographic
        b.proj[0][0] = p[0];
        b.proj[0][3] = p[1];
        b.proj[1][1] = p[2];
        b.proj[1][3] = p[3];
        b.proj[2][2] = p[4];
        b.proj[2][3] = p[5];
        b.proj[3][3] = 1.0f;
    } else {
        b.proj[0][0] = p[0];
        b.proj[0][2] = p[1];
        b.proj[1][1] = p[2];
        b.proj[1][2] = p[3];
        b.proj[2][2] = p[4];
        b.proj[2][3] = p[5];
        b.proj[3][2] = -1.0f;
    }
    b.depthParams[0] = bitsToFloat(xfRegs[0x1C]);
    b.depthParams[1] = bitsToFloat(xfRegs[0x1F]);
    b.depthParams[2] = b.depthParams[3] = 0.0f;
}

void Builder::fillPix(PixelState& b) {
    for (int i = 0; i < 4; i++) {
        u32 lo = tevReg[i * 2], hi = tevReg[i * 2 + 1];
        b.tevReg[i][0] = s11(lo);
        b.tevReg[i][3] = s11(lo >> 12);
        b.tevReg[i][2] = s11(hi);
        b.tevReg[i][1] = s11(hi >> 12);
        u32 klo = tevKonst[i * 2], khi = tevKonst[i * 2 + 1];
        b.konst[i][0] = (klo & 0xFF) / 255.0f;
        b.konst[i][3] = ((klo >> 12) & 0xFF) / 255.0f;
        b.konst[i][2] = (khi & 0xFF) / 255.0f;
        b.konst[i][1] = ((khi >> 12) & 0xFF) / 255.0f;
    }
    b.alphaRef[0] = (float)(bp[0xF3] & 0xFF);
    b.alphaRef[1] = (float)((bp[0xF3] >> 8) & 0xFF);
    b.alphaRef[2] = (float)(bp[0xF4] & 0xFFFFFF);
    b.alphaRef[3] = 0.0f;
    u32 fc = bp[0xF2];
    b.fogColor[0] = ((fc >> 16) & 255) / 255.0f;
    b.fogColor[1] = ((fc >> 8) & 255) / 255.0f;
    b.fogColor[2] = (fc & 255) / 255.0f;
    b.fogColor[3] = 1.0f;
    // Fog A and C are stored as the top 20 bits of a float; B as a 24-bit
    // magnitude and a shift.
    auto fogFloat = [](u32 reg) {
        u32 bits = (reg & 0xFFFFF) << 12;
        float f;
        memcpy(&f, &bits, 4);
        return f;
    };
    b.fogParams[0] = fogFloat(bp[0xEE]);
    b.fogParams[1] = fogFloat(bp[0xF1]);
    b.fogParams[2] = (float)(bp[0xEF] & 0xFFFFFF);
    b.fogParams[3] = (float)(bp[0xF0] & 0x1F);
    memset(b.fogRange, 0, sizeof(b.fogRange));
    static const u8 base[8] = {0x88, 0x89, 0x8A, 0x8B, 0xA8, 0xA9, 0xAA, 0xAB};
    static const u8 mode0[8] = {0x80, 0x81, 0x82, 0x83, 0xA0, 0xA1, 0xA2, 0xA3};
    for (int i = 0; i < 8; i++) {
        u32 img = bp[base[i]];
        float w = (float)((img & 0x3FF) + 1), h = (float)(((img >> 10) & 0x3FF) + 1);
        // The texture's LOD bias (TX_SETMODE0 bits 9-16, signed, 1/32 steps):
        // GLES samplers have none, so the shader passes it to texture().
        // Many textures sharpen (grass, floor tiles) or soften (water) their
        // mip choice with it.
        int bias = (int)((bp[mode0[i]] >> 9) & 0xFF);
        b.texSize[i][0] = (float)(bias >= 0x80 ? bias - 0x100 : bias) / 32.0f;
        b.texSize[i][1] = h;
        b.texSize[i][2] = 1.0f / w;
        b.texSize[i][3] = 1.0f / h;
    }
    for (int m = 0; m < 3; m++) {
        u32 ra = bp[0x06 + m * 3], rb = bp[0x07 + m * 3], rc = bp[0x08 + m * 3];
        int scale = (int)(((ra >> 22) & 3) | (((rb >> 22) & 3) << 2) | (((rc >> 22) & 1) << 4)) - 17;
        float sc = ldexpf(1.0f, scale) / 1024.0f;
        auto sv = [](u32 v) {
            int x = (int)(v & 0x7FF);
            if (x & 0x400) x -= 0x800;
            return (float)x;
        };
        b.indMtx[m][0][0] = sv(ra) * sc;
        b.indMtx[m][0][1] = sv(rb) * sc;
        b.indMtx[m][0][2] = sv(rc) * sc;
        b.indMtx[m][0][3] = 0.0f;
        b.indMtx[m][1][0] = sv(ra >> 11) * sc;
        b.indMtx[m][1][1] = sv(rb >> 11) * sc;
        b.indMtx[m][1][2] = sv(rc >> 11) * sc;
        b.indMtx[m][1][3] = 0.0f;
    }
    b.efbSize[0] = (float)nativeW;
    b.efbSize[1] = (float)nativeH;
    b.efbSize[2] = b.efbSize[3] = 0.0f;
}

// Appends n rows (stride floats each, padded to vec4) to rows[]; returns the
// index of the first.
u32 Builder::appendRows(const float* src, int n, int stride) {
    u32 base = (u32)(rows.size() / 4);
    for (int r = 0; r < n; r++) {
        for (int k = 0; k < 4; k++) rows.push_back(k < stride ? src[r * stride + k] : 0.0f);
    }
    return base;
}

// Adds a state block (vec4s) to states[] unless an identical one exists;
// returns its vec4 index.
u32 Builder::addState(const float* data, size_t vec4s) {
    const u32* w = (const u32*)data;
    u64 h = hashWords(w, vec4s * 4) ^ vec4s;
    std::vector<u32>& bases = stateIndex[h];
    for (u32 b : bases) {
        if (memcmp(&states[(size_t)b * 4], data, vec4s * 16) == 0) return b;
    }
    u32 base = (u32)(states.size() / 4);
    states.insert(states.end(), data, data + vec4s * 4);
    bases.push_back(base);
    return base;
}

// Marks the snapshots covering XF addresses [a0, a1) stale.
void Builder::invalidateXf(u32 a0, u32 a1) {
    if (a0 < 0x100) {
        u32 r0 = a0 / 4, r1 = ((a1 < 0x100 ? a1 : 0x100) - 1) / 4;
        for (u32 slot = r0 / 3; slot <= r1 / 3 && slot < 22; slot++) posSlot[slot] = ~0u;
    }
    if (a1 > 0x400 && a0 < 0x460) {
        u32 r0 = ((a0 > 0x400 ? a0 : 0x400) - 0x400) / 3, r1 = ((a1 < 0x460 ? a1 : 0x460) - 0x400 - 1) / 3;
        for (u32 slot = r0 / 3; slot <= r1 / 3 && slot < 11; slot++) nrmSlot[slot] = ~0u;
    }
    if (a1 > 0x500 && a0 < 0x600) {
        int r0 = (int)((a0 > 0x500 ? a0 : 0x500) - 0x500) / 4, r1 = (int)((a1 < 0x600 ? a1 : 0x600) - 0x500 - 1) / 4;
        for (int start = r0 - 2; start <= r1; start++) {
            if (start >= 0 && start < 64) postSlot[start] = ~0u;
        }
    }
    if (a1 > 0x600 && a0 < 0x680) lightBase = ~0u;
    if (a1 > 0x100A && a0 < 0x100E) lightBase = ~0u;
    if (a1 > 0x101C && a0 < 0x1027) projBase = ~0u;
}

// The draw record for the current register state.
u32 Builder::drawRecord(bool useVr, bool sky, bool pointer, bool dualTex, u32 numTexGens) {
    u32 rec[kRecWords];
    memset(rec, 0, sizeof(rec));
    for (int i = 0; i < 22; i++) {
        if (posSlot[i] == ~0u) posSlot[i] = appendRows(xfMem + i * 12, 3, 4);
        rec[kRecPosSlots + i] = posSlot[i];
    }
    for (int i = 0; i < 11; i++) {
        if (nrmSlot[i] == ~0u) nrmSlot[i] = appendRows(xfMem + 0x400 + i * 9, 3, 3);
        rec[kRecNrmSlots + i] = nrmSlot[i];
    }
    if (dualTex) {
        for (u32 i = 0; i < numTexGens && i < 8; i++) {
            u32 p = xfRegs[0x50 + i] & 63;
            if (postSlot[p] == ~0u) postSlot[p] = appendRows(xfMem + 0x500 + p * 4, 3, 4);
            rec[kRecPost + i] = postSlot[p];
        }
    }
    if (lightBase == ~0u) {
        LightState ls;
        fillLights(ls);
        lightBase = addState(&ls.lights[0][0][0], sizeof(ls) / 16);
    }
    if (projBase == ~0u) {
        ProjState ps;
        fillProj(ps);
        projBase = addState(&ps.proj[0][0], sizeof(ps) / 16);
    }
    if (pixBase == ~0u) {
        PixelState px;
        fillPix(px);
        pixBase = addState(&px.tevReg[0][0], sizeof(px) / 16);
    }
    rec[kRecLights] = lightBase;
    rec[kRecProj] = projBase;
    rec[kRecPixel] = pixBase;
    rec[kRecMtxIdxA] = xfRegs[0x18];
    rec[kRecMtxIdxB] = xfRegs[0x19];
    rec[kRecFlags] = (useVr ? 1u : 0u) | (useVr && sky ? 2u : 0u) | (pointer ? 4u : 0u);
    size_t n = recs.size();
    if (n >= kRecWords && memcmp(&recs[n - kRecWords], rec, sizeof(rec)) == 0) {
        return (u32)(n / kRecWords) - 1;
    }
    recs.insert(recs.end(), rec, rec + kRecWords);
    return (u32)(n / kRecWords);
}

// Texgens of a 3D draw that map each position to the spot where it lands on
// the screen: how water and heat haze sample a capture of the screen.  In VR
// that capture holds the eye's view, so the shader swaps these coordinates
// for the eye's screen position.  Tested with points in front of the camera
// rather than the draw's own vertices, because a planar mirror's texgen
// (which samples a capture made from the mirrored camera) also matches on
// the mirror plane itself.
u32 Builder::screenTexGens(const Frame& f, const Item& it, u32 first, const ShaderUid& uid) {
    u32 candidates = 0;
    for (u32 i = 0; i < uid.numTexGens && i < 8; i++) {
        u32 info = uid.texMtxInfo[i];
        bool stq = (info >> 1) & 1, regular = ((info >> 4) & 7) == 0, fromPos = ((info >> 7) & 31) == 0;
        if (stq && regular && fromPos) candidates |= 1u << i;
    }
    if (!candidates) return 0;

    // Matrix indices of the draw's first vertex.
    const u32* vx = f.verts.data() + first;
    u32 mA = xfRegs[0x18], mB = xfRegs[0x19];
    u32 off = 3, pidx = mA & 63;
    if (it.flags & VF_POSMTX) pidx = vx[off++];
    if (it.flags & VF_NRM) off += 3;
    if (it.flags & VF_NBT) off += 6;
    if (it.flags & VF_CLR0) off++;
    if (it.flags & VF_CLR1) off++;
    for (int t = 0; t < 8; t++) {
        if (it.flags & (VF_TEX0 << t)) off += 2;
    }
    const u32* texmtxIdx = (it.flags & VF_TEXMTX) ? vx + off : nullptr;
    if (pidx > 61) return 0;

    // Model space <- view space: invert the position matrix.
    const float* m = xfMem + pidx * 4;
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], g = m[6], h = m[8], k = m[9], l = m[10];
    float det = a * (e * l - g * k) - b * (d * l - g * h) + c * (d * k - e * h);
    if (!(fabsf(det) > 1e-20f)) return 0;
    float inv[9] = {(e * l - g * k) / det, (c * k - b * l) / det, (b * g - c * e) / det, (g * h - d * l) / det, (a * l - c * h) / det,
                    (c * d - a * g) / det, (d * k - e * h) / det, (b * h - a * k) / det, (a * e - b * d) / det};
    ProjState ps;
    fillProj(ps);

    static const float kView[3][3] = {{0.0f, 0.0f, -500.0f}, {120.0f, -80.0f, -1500.0f}, {-900.0f, 500.0f, -6000.0f}};
    u32 mask = candidates;
    for (int p = 0; p < 3 && mask; p++) {
        const float* v = kView[p];
        float rel[3] = {v[0] - m[3], v[1] - m[7], v[2] - m[11]};
        float mp[4] = {inv[0] * rel[0] + inv[1] * rel[1] + inv[2] * rel[2], inv[3] * rel[0] + inv[4] * rel[1] + inv[5] * rel[2],
                       inv[6] * rel[0] + inv[7] * rel[1] + inv[8] * rel[2], 1.0f};
        float clip[4];
        for (int r = 0; r < 4; r++) clip[r] = ps.proj[r][0] * v[0] + ps.proj[r][1] * v[1] + ps.proj[r][2] * v[2] + ps.proj[r][3];
        if (!(clip[3] > 0.0f)) return 0;
        float sx = (it.vp[0] + (clip[0] / clip[3] * 0.5f + 0.5f) * it.vp[2]) / nativeW;
        float sy = (it.vp[1] + (0.5f - clip[1] / clip[3] * 0.5f) * it.vp[3]) / nativeH;
        for (u32 i = 0; i < 8; i++) {
            if (!((mask >> i) & 1)) continue;
            u32 info = uid.texMtxInfo[i];
            u32 midx = texmtxIdx ? (texmtxIdx[i / 4] >> ((i % 4) * 8)) & 255 : i < 4 ? (mA >> (6 * (i + 1))) & 63 : (mB >> (6 * (i - 4))) & 63;
            float src[4] = {mp[0], mp[1], ((info >> 2) & 1) ? mp[2] : 1.0f, 1.0f};
            float tc[3];
            bool ok = midx <= 61;
            for (int r = 0; ok && r < 3; r++) {
                const float* row = xfMem + (midx + r) * 4;
                tc[r] = row[0] * src[0] + row[1] * src[1] + row[2] * src[2] + row[3] * src[3];
            }
            if (ok && uid.dualTex) {
                u32 post = xfRegs[0x50 + i];
                if ((post >> 8) & 1) {
                    float len = sqrtf(tc[0] * tc[0] + tc[1] * tc[1] + tc[2] * tc[2]);
                    if (len > 0.0f) tc[0] /= len, tc[1] /= len, tc[2] /= len;
                }
                const float* pm = xfMem + 0x500 + (post & 63) * 4;
                float t2[3];
                for (int r = 0; r < 3; r++) t2[r] = pm[r * 4] * tc[0] + pm[r * 4 + 1] * tc[1] + pm[r * 4 + 2] * tc[2] + pm[r * 4 + 3];
                memcpy(tc, t2, sizeof(tc));
            }
            ok = ok && fabsf(tc[2]) > 1e-6f && fabsf(tc[0] / tc[2] - sx) < 0.01f && fabsf(tc[1] / tc[2] - sy) < 0.01f;
            if (!ok) mask &= ~(1u << i);
        }
    }
    return mask;
}

// ---------------------------------------------------------------------------
// Prepare: frame recording -> item list + GPU buffers
// ---------------------------------------------------------------------------
static bool isTevColorReg(u32 reg) { return reg >= 0xE0 && reg <= 0xE7; }

u32 Builder::uidFor(const ShaderUid& uid) {
    auto it = uidIndex.find(uid);
    if (it != uidIndex.end()) return it->second;
    u32 i = (u32)uids.size();
    uids.push_back(uid);
    uidIndex.emplace(uid, i);
    return i;
}

// Index list of a GX primitive of `count` vertices from `base`.
void Builder::appendIndices(u32 kind, u32 base, u32 count) {
    u32 n;
    switch (kind) {
    case 0x80:
    case 0x88:  // quads
        n = count / 4 * 6;
        break;
    case 0x90:  // triangles
        n = count / 3 * 3;
        break;
    case 0x98:  // strip
    case 0xA0:  // fan
        n = count >= 3 ? (count - 2) * 3 : 0;
        break;
    case 0xA8:  // lines
        n = count / 2 * 2;
        break;
    case 0xB0:  // line strip
        n = count >= 2 ? (count - 1) * 2 : 0;
        break;
    default:  // points
        n = count;
        break;
    }
    size_t at = indices.size();
    indices.resize(at + n);
    u32* o = indices.data() + at;
    switch (kind) {
    case 0x80:
    case 0x88:
        for (u32 v = base; v + 4 <= base + count; v += 4, o += 6) {
            o[0] = v, o[1] = v + 1, o[2] = v + 2, o[3] = v, o[4] = v + 2, o[5] = v + 3;
        }
        break;
    case 0x98:  // GL winding order: odd triangles swap their first two
        for (u32 i = 0; i + 2 < count; i++, o += 3) {
            o[0] = base + i + (i & 1), o[1] = base + i + 1 - (i & 1), o[2] = base + i + 2;
        }
        break;
    case 0xA0:
        for (u32 i = 1; i + 1 < count; i++, o += 3) {
            o[0] = base, o[1] = base + i, o[2] = base + i + 1;
        }
        break;
    case 0xB0:
        for (u32 i = 0; i + 1 < count; i++, o += 2) {
            o[0] = base + i, o[1] = base + i + 1;
        }
        break;
    default:  // triangles, lines, points: the vertices in order
        for (u32 i = 0; i < n; i++) {
            o[i] = base + i;
        }
        break;
    }
}

void Builder::build(std::shared_ptr<const Frame> frame, Prepared& out) {
    const Frame& f = *frame;
    items.clear();
    uids.clear();
    uidIndex.clear();
    rows.clear();
    recs.clear();
    states.clear();
    vtxRec.clear();
    indices.clear();
    stateIndex.clear();
    memcpy(bp, f.startBp, sizeof(bp));
    memcpy(tevReg, f.startTevReg, sizeof(tevReg));
    memcpy(tevKonst, f.startTevKonst, sizeof(tevKonst));
    memcpy(xfRegs, f.startXfRegs, sizeof(xfRegs));
    memcpy(xfMem, f.startXfMem, sizeof(xfMem));
    for (u32& v : posSlot) v = ~0u;
    for (u32& v : nrmSlot) v = ~0u;
    for (u32& v : postSlot) v = ~0u;
    lightBase = projBase = pixBase = ~0u;
    inHud = false;
    inSky = false;
    inPlayer = false;
    inPointer = false;
    uidDirty = true;
    lastUidFlags = ~0u;
    drawStateDirty = true;
    cam = CameraInfo();
    for (u32& t : boundTex) t = ~0u;
    clearAR = f.startBp[0x4F];
    clearGB = f.startBp[0x50];
    clearZ = f.startBp[0x51] & 0xFFFFFF;
    u32 ordinal = 0;

    const u32* c = f.cmds.data();
    const u32* end = c + f.cmds.size();
    while (c < end) {
        u32 op = *c++;
        if (op != CMD_DRAW) {
            drawStateDirty = true;
        }
        switch (op) {
        case CMD_XF: {
            u32 addr = c[0], count = c[1];
            c += 2;
            for (u32 i = 0; i < count; i++) {
                u32 a = addr + i;
                if (a < 0x1000) {
                    xfMem[a] = bitsToFloat(c[i]);
                } else if (a < 0x1100) {
                    xfRegs[a - 0x1000] = c[i];
                }
            }
            invalidateXf(addr, addr + count);
            // Registers in the shader key: channel counts and controls, dual
            // texture, texgen count and texgens.
            auto touches = [&](u32 lo, u32 hi) { return addr < hi && addr + count > lo; };
            if (touches(0x1009, 0x100A) || touches(0x100E, 0x1013) || touches(0x103F, 0x1048) || touches(0x1050, 0x1058)) {
                uidDirty = true;
            }
            c += count;
            break;
        }
        case CMD_BP: {
            u32 v = *c++;
            u32 reg = v >> 24;
            bp[reg] = v & 0xFFFFFF;
            if (isTevColorReg(reg)) {
                trackTevColorWrite(tevReg, tevKonst, reg, v & 0xFFFFFF);
            }
            // Registers that feed the pixel state.
            if ((reg >= 0x06 && reg <= 0x0E) || (reg >= 0x88 && reg <= 0x8B) || (reg >= 0xA8 && reg <= 0xAB) || (reg >= 0xE0 && reg <= 0xE7) ||
                (reg >= 0xEE && reg <= 0xF4)) {
                pixBase = ~0u;
            }
            // Registers in the shader key (makeShaderUid).
            if (reg == 0x00 || (reg >= 0x10 && reg <= 0x1F) || (reg >= 0x25 && reg <= 0x2F) || reg == 0x43 || (reg >= 0xC0 && reg <= 0xDF) ||
                reg == 0xF1 || reg == 0xF3 || (reg >= 0xF5 && reg <= 0xFD)) {
                uidDirty = true;
            }
            break;
        }
        case CMD_TEX:
            boundTex[c[0] & 7] = c[1];
            c += 2;
            break;
        case CMD_TEX_EFB:
            boundTex[c[0] & 7] = 0x80000000u | (c[4] >> 5);  // the copy's address
            c += 5;
            break;
        case CMD_MARKER: {
            Item it{};
            it.type = IT_MARKER;
            it.marker = *c++;
            if (it.marker == MARK_HUD_BEGIN) inHud = true;
            if (it.marker == MARK_HUD_END) inHud = false;
            if (it.marker == MARK_SKY_BEGIN) inSky = true;
            if (it.marker == MARK_SKY_END) inSky = false;
            if (it.marker == MARK_PLAYER_BEGIN || it.marker == MARK_PLAYER_END) {
                inPlayer = it.marker == MARK_PLAYER_BEGIN;
                drawStateDirty = true;  // no batch spans the marker
            }
            if (it.marker == MARK_POINTER_BEGIN || it.marker == MARK_POINTER_END) {
                inPointer = it.marker == MARK_POINTER_BEGIN;
                drawStateDirty = true;  // the draw record changes
            }
            items.push_back(it);
            break;
        }
        case CMD_CAMERA: {
            if (!cam.valid) {
                cam.valid = true;
                memcpy(cam.view, c, 12 * 4);
                memcpy(cam.pos, c + 12, 3 * 4);
                memcpy(cam.watch, c + 15, 3 * 4);
                memcpy(cam.up, c + 18, 3 * 4);
                memcpy(cam.watchUp, c + 21, 3 * 4);
                memcpy(&cam.fovy, c + 24, 4);
                cam.flags = c[25];
                memcpy(&cam.aspect, c + 26, 4);
                memcpy(cam.player, c + 27, 3 * 4);
                memcpy(cam.playerUp, c + 30, 3 * 4);
            }
            c += kCameraWords;
            break;
        }
        case CMD_EFB_COPY: {
            Item it{};
            it.type = IT_COPY;
            memcpy(it.copy, c, sizeof(it.copy));
            it.copyPixFmt = bp[0x43];
            it.copyCmode = bp[0x41];
            it.copyZmode = bp[0x40];
            c += 10;
            items.push_back(it);
            // The XFB copy's source rectangle tells us the native EFB size.
            if ((it.copy[9] >> 14) & 1) {
                nativeW = (it.copy[2] & 0x3FF) + 1;
                nativeH = ((it.copy[2] >> 10) & 0x3FF) + 1;
            }
            break;
        }
        case CMD_DRAW: {
            u32 prim = c[0], flags = c[1], first = c[2], count = c[3];
            c += 4;
            if (!count) break;
            u32 kind = prim & 0xF8;
            GLenum glPrim = kind <= 0xA0 ? GL_TRIANGLES : kind <= 0xB0 ? GL_LINES : GL_POINTS;
            if (!drawStateDirty && lastDrawReusable && flags == lastDrawFlags && glPrim == lastDrawPrim && !items.empty() &&
                items.back().type == IT_DRAW) {
                Item* b = &items.back();
                if (b->endWord != first) {
                    // Same state, vertices elsewhere: a new batch like the last.
                    Item it = *b;
                    it.firstWord = first;
                    it.firstOrdinal = ordinal;
                    it.idxOffset = (u32)indices.size();
                    it.idxCount = 0;
                    it.draws = 0;
                    items.push_back(it);
                    b = &items.back();
                }
                vtxRec.insert(vtxRec.end(), count, lastDrawRec);
                appendIndices(kind, ordinal - b->firstOrdinal, count);
                ordinal += count;
                b->idxCount = (u32)indices.size() - b->idxOffset;
                b->endWord = first + count * vtxWords(flags);
                b->draws++;
                break;
            }
            Item it{};
            it.type = IT_DRAW;
            it.flags = flags;
            it.hud = inHud;
            it.player = inPlayer;
            it.prim = glPrim;
            // Viewport (native EFB coordinates, top-left origin).
            float sx = bitsToFloat(xfRegs[0x1A]), sy = bitsToFloat(xfRegs[0x1B]);
            float ox = bitsToFloat(xfRegs[0x1D]), oy = bitsToFloat(xfRegs[0x1E]);
            float xoff = (float)((bp[0x59] & 0x3FF) * 2), yoff = (float)(((bp[0x59] >> 10) & 0x3FF) * 2);
            it.vp[0] = ox - fabsf(sx) - xoff;
            it.vp[1] = oy - fabsf(sy) - yoff;
            it.vp[2] = 2.0f * fabsf(sx);
            it.vp[3] = 2.0f * fabsf(sy);
            int soffX = (int)xoff, soffY = (int)yoff;
            it.sc[0] = (int)((bp[0x20] >> 12) & 0x7FF) - soffX;
            it.sc[1] = (int)(bp[0x20] & 0x7FF) - soffY;
            it.sc[2] = (int)((bp[0x21] >> 12) & 0x7FF) - soffX + 1;
            it.sc[3] = (int)(bp[0x21] & 0x7FF) - soffY + 1;

            bool perspective = !(xfRegs[0x26] & 1);
            bool fullscreen = it.vp[0] <= 1.0f && it.vp[1] <= 1.0f && it.vp[2] >= (float)nativeW - 2.0f && it.vp[3] >= (float)nativeH - 2.0f;
            // The VR camera applies to the main 3D scene: perspective,
            // full-screen draws after the frame's camera.
            it.useVr = perspective && fullscreen && !inHud && cam.valid;
            float p1 = bitsToFloat(xfRegs[0x21]);
            it.hudSafe = inHud && !perspective && (fabsf(p1) < 0.5f);

            if (uidDirty || flags != lastUidFlags || inHud != lastUidHud) {
                ShaderUid uid = makeShaderUid(bp, xfRegs, flags, inHud);
                // Screen texgens depend on each draw's matrices: such keys
                // are never reused.
                bool perDraw = false;
                for (u32 i = 0; i < uid.numTexGens && i < 8; i++) {
                    u32 info = uid.texMtxInfo[i];
                    perDraw |= ((info >> 1) & 1) && ((info >> 4) & 7) == 0 && ((info >> 7) & 31) == 0;
                }
                if (it.useVr && perDraw) {
                    uid.screenTexGens = screenTexGens(f, it, first, uid);
                }
                it.uid = uidFor(uid);
                uidDirty = perDraw;
                lastUid = it.uid;
                lastUidFlags = flags;
                lastUidHud = inHud;
            } else {
                it.uid = lastUid;
            }
            const ShaderUid& uid = uids[it.uid];

            // GL state.
            u32 cm = bp[0x41];
            it.colorMask = (((cm >> 3) & 1) ? 7 : 0) | (((cm >> 4) & 1) ? 8 : 0);
            if ((bp[0x43] & 7) != 1) {  // no alpha channel in the EFB
                it.colorMask &= 7;
            }
            it.blend = cm & 1;
            static const GLenum srcF[8] = {GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA,
                                           GL_ONE_MINUS_DST_ALPHA};
            static const GLenum dstF[8] = {GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA,
                                           GL_ONE_MINUS_DST_ALPHA};
            if ((cm >> 11) & 1) {  // subtract: dst - src
                it.blend = 1;
                it.srcRGB = GL_ONE;
                it.dstRGB = GL_ONE;
                it.eq = GL_FUNC_REVERSE_SUBTRACT;
            } else {
                it.srcRGB = srcF[(cm >> 8) & 7];
                it.dstRGB = dstF[(cm >> 5) & 7];
                it.eq = GL_FUNC_ADD;
            }
            it.dstAlpha = (bp[0x42] >> 8) & 1;
            it.dstAlphaValue = (bp[0x42] & 0xFF) / 255.0f;
            u32 zm = bp[0x40];
            it.depthTest = zm & 1;
            it.depthWrite = (zm >> 4) & 1;
            static const GLenum zf[8] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
            it.depthFunc = zf[(zm >> 1) & 7];
            u32 cull = (bp[0x00] >> 14) & 3;
            it.cull = cull != 0;
            it.cullFace = cull == 1 ? GL_FRONT : cull == 2 ? GL_BACK : GL_FRONT_AND_BACK;

            it.texMask = usedTexUnits(bp);
            static const u8 m0[8] = {0x80, 0x81, 0x82, 0x83, 0xA0, 0xA1, 0xA2, 0xA3};
            for (int t = 0; t < 8; t++) {
                bool used = (it.texMask >> t) & 1;
                it.tex[t] = used ? boundTex[t] : ~0u;
                it.texMode0[t] = used ? bp[m0[t]] : 0;
                it.texMode1[t] = used ? bp[m0[t] + 4] : 0;
            }

            u32 rec = drawRecord(it.useVr, inSky, inPointer, uid.dualTex != 0, uid.numTexGens);
            u32 firstOrdinal = ordinal;
            ordinal += count;
            vtxRec.insert(vtxRec.end(), count, rec);
            drawStateDirty = false;
            lastDrawReusable = !uidDirty;  // uidDirty stays set after a key with screen texgens
            lastDrawFlags = flags;
            lastDrawPrim = it.prim;
            lastDrawRec = rec;

            // Extend the previous batch when the state matches and the
            // vertices follow on; otherwise start a new one.
            Item* b = (!items.empty() && items.back().type == IT_DRAW) ? &items.back() : nullptr;
            if (!b || b->endWord != first || !sameBatchState(*b, it)) {
                it.firstWord = first;
                it.firstOrdinal = firstOrdinal;
                it.idxOffset = (u32)indices.size();
                items.push_back(it);
                b = &items.back();
            }
            appendIndices(kind, firstOrdinal - b->firstOrdinal, count);
            b->idxCount = (u32)indices.size() - b->idxOffset;
            b->endWord = first + count * vtxWords(flags);
            b->draws++;
            break;
        }
        default:
            port_log("gl: bad recording opcode %u", op);
            c = end;
            break;
        }
    }

    // Storage buffers must not be empty.
    if (rows.empty()) rows.resize(4, 0.0f);
    if (recs.empty()) recs.resize(kRecWords, 0);
    if (states.empty()) states.resize(4, 0.0f);
    if (vtxRec.empty()) vtxRec.push_back(0);

    // Hand the results over; out's old storage comes back for the next build.
    out.frame = std::move(frame);
    out.number = f.number;
    std::swap(out.items, items);
    std::swap(out.uids, uids);
    std::swap(out.rows, rows);
    std::swap(out.recs, recs);
    std::swap(out.states, states);
    std::swap(out.vtxRec, vtxRec);
    std::swap(out.indices, indices);
    out.cam = cam;
    out.clearAR = clearAR;
    out.clearGB = clearGB;
    out.clearZ = clearZ;
    out.nativeW = nativeW;
    out.nativeH = nativeH;

    // PETARI_GLITEMS=n: one line per batch / copy / marker, of every nth
    // frame (1: all).
    if (const char* every = debugEnv("PETARI_GLITEMS")) {
        int n = atoi(every) > 0 ? atoi(every) : 1;
        if (out.number % (u64)n == 0) logItems(out);
    }
}

// Render thread: uploads a built frame and resolves its programs.
void Renderer::Impl::upload(Prepared& p, bool syncPrograms) {
    const Frame& f = *p.frame;
    FrameBuffers& fb = frameBufs[frameBufNext];
    frameBufNext = (frameBufNext + 1) % 3;
    glBindVertexArray(0);  // the element buffer binding belongs to the bound VAO
    auto put = [&fb](int i, GLenum target, const void* data, size_t bytes) {
        glBindBuffer(target, fb.buf[i]);
        if (bytes > fb.cap[i]) {
            fb.cap[i] = bytes + bytes / 4 + 4096;
            glBufferData(target, (GLsizeiptr)fb.cap[i], nullptr, GL_DYNAMIC_DRAW);
        }
        if (bytes) {
            glBufferSubData(target, 0, (GLsizeiptr)bytes, data);
        }
    };
    put(0, GL_ARRAY_BUFFER, f.verts.data(), f.verts.size() * 4);
    put(1, GL_SHADER_STORAGE_BUFFER, p.vtxRec.data(), p.vtxRec.size() * 4);
    put(2, GL_ELEMENT_ARRAY_BUFFER, p.indices.data(), p.indices.size() * 4);
    put(3, GL_SHADER_STORAGE_BUFFER, p.rows.data(), p.rows.size() * 4);
    put(4, GL_SHADER_STORAGE_BUFFER, p.recs.data(), p.recs.size() * 4);
    put(5, GL_SHADER_STORAGE_BUFFER, p.states.data(), p.states.size() * 4);
    vbo = fb.buf[0];
    recVbo = fb.buf[1];
    ibo = fb.buf[2];
    rowBuf = fb.buf[3];
    recBuf = fb.buf[4];
    stateBuf = fb.buf[5];

    // New textures.
    for (auto& img : f.textures) {
        if (img->glTex && img->glVersion == img->version) {
            continue;
        }
        if (!img->glTex) {
            glGenTextures(1, &img->glTex);
        }
        glBindTexture(GL_TEXTURE_2D, img->glTex);
        const u32* px = img->rgba.data();
        u32 w = img->width, h = img->height;
        for (u32 l = 0; l < img->levels; l++) {
            glTexImage2D(GL_TEXTURE_2D, (GLint)l, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            px += (size_t)w * h;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)img->levels - 1);
        img->glVersion = img->version;
        // Images never change once recorded, so the pixels are not needed
        // again (except by the PETARI_DUMP texture dump).
        static const bool keepPixels = debugEnv("PETARI_DUMP") != nullptr;
        if (!keepPixels) {
            std::vector<uint32_t>().swap(img->rgba);
        }
    }

    std::vector<GLuint> progs(p.uids.size());
    for (size_t i = 0; i < p.uids.size(); i++) {
        progs[i] = program(p.uids[i], syncPrograms);
    }
    for (Item& it : p.items) {
        if (it.type == IT_DRAW) {
            it.program = progs[it.uid];
        }
    }
    p.cutProgs.assign(p.uids.size(), 0);
}

// Worker thread: builds the newest requested frame.
void Renderer::Impl::workerMain() {
    for (;;) {
        std::shared_ptr<const Frame> frame;
        std::unique_ptr<Prepared> out;
        {
            std::unique_lock<std::mutex> lock(workLock);
            workCv.wait(lock, [&] { return wanted != nullptr; });
            frame = std::move(wanted);
            wanted.reset();
            out = spare ? std::move(spare) : std::unique_ptr<Prepared>(new Prepared());
        }
        int64_t t0 = port_host_time_ns();
        workerBuilder.build(std::move(frame), *out);
        port_perf_prepare(port_host_time_ns() - t0);
        static const bool perfLog = debugEnv("PETARI_PERFLOG") != nullptr;
        if (perfLog && out->number % 600 == 0) {
            size_t batches = 0, copies = 0;
            for (const Item& it : out->items) {
                batches += it.type == IT_DRAW;
                copies += it.type == IT_COPY;
            }
            port_log("perf: prepared frame %llu: %zu batches, %zu EFB copies, %zu shader keys, %zu indices", (unsigned long long)out->number, batches,
                     copies, out->uids.size(), out->indices.size());
        }
        std::lock_guard<std::mutex> lock(workLock);
        if (built) {
            spare = std::move(built);  // superseded before the render thread took it
        }
        built = std::move(out);
    }
}

void logItems(const Prepared& p) {
    const std::vector<Item>& items = p.items;
    const std::vector<float>& rows = p.rows;
    const std::vector<u32>& recs = p.recs;
    const std::vector<float>& states = p.states;
    const std::vector<u32>& indices = p.indices;
    u64 preparedNumber = p.number;
    u32 nativeW = p.nativeW, nativeH = p.nativeH;
    port_log("items: frame %llu, %zu items, native %ux%u, %zu rows, %zu records, %zu state vec4s, %zu indices", (unsigned long long)preparedNumber,
             items.size(), nativeW, nativeH, rows.size() / 4, recs.size() / kRecWords, states.size() / 4, indices.size());
    for (const Item& it : items) {
        if (it.type == IT_MARKER) {
            port_log("items: marker %u", it.marker);
        } else if (it.type == IT_COPY) {
            const u32* cp = it.copy;
            u32 ctrl = cp[9];
            port_log("items: COPY src %u,%u %ux%u dst %06x %s fmt %u clear %u (color %06x %06x z %06x) cmode %06x zmode %06x", cp[1] & 0x3FF,
                     (cp[1] >> 10) & 0x3FF, (cp[2] & 0x3FF) + 1, ((cp[2] >> 10) & 0x3FF) + 1, cp[3], (ctrl >> 14) & 1 ? "XFB" : "TEX",
                     ((ctrl >> 4) & 7) | ((ctrl >> 0) & 8), (ctrl >> 11) & 1, cp[6], cp[7], cp[8], it.copyCmode, it.copyZmode);
        } else {
            port_log("items: batch %u draws prog %u prim %x flags %05x idx %u vr %u hud %u player %u blend %u(%x,%x,%x) cmask %x z %u/%u/%x cull %u vp %.0f,%.0f "
                     "%.0fx%.0f tex0 %08x tex1 %08x screen texgens %x",
                     it.draws, it.program, it.prim, it.flags, it.idxCount, it.useVr, it.hud, it.player, it.blend, it.srcRGB, it.dstRGB, it.eq, it.colorMask,
                     it.depthTest, it.depthWrite, it.depthFunc, it.cull, it.vp[0], it.vp[1], it.vp[2], it.vp[3], it.tex[0], it.tex[1],
                     p.uids[it.uid].screenTexGens);
            if (it.player) {
                // The player's shading (lighting channels, texgens, TEV).
                const ShaderUid& u = p.uids[it.uid];
                port_log("items:   player: chans %u cc %06x %06x ac %06x %06x; texgens %u: %05x %05x %05x %05x post %03x %03x; tev %u ind %u; "
                         "tex %08x %08x %08x %08x",
                         u.numChans, u.colorCtrl[0], u.colorCtrl[1], u.alphaCtrl[0], u.alphaCtrl[1], u.numTexGens, u.texMtxInfo[0],
                         u.texMtxInfo[1], u.texMtxInfo[2], u.texMtxInfo[3], u.postMtxInfo[0], u.postMtxInfo[1], u.numTevStages, u.numIndStages,
                         it.tex[0], it.tex[1], it.tex[2], it.tex[3]);
                port_log("items:   player tev: order %06x %06x; color %06x %06x %06x %06x; alpha %06x %06x %06x %06x; ksel %06x %06x %06x %06x",
                         u.tevOrder[0], u.tevOrder[1], u.tevColor[0], u.tevColor[1], u.tevColor[2], u.tevColor[3], u.tevAlpha[0], u.tevAlpha[1],
                         u.tevAlpha[2], u.tevAlpha[3], u.ksel[0], u.ksel[1], u.ksel[2], u.ksel[3]);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------
void Renderer::Impl::doCopy(const Item& it, EfbTarget& src, Renderer& r, bool vr) {
    u32 nativeW = cur->nativeW, nativeH = cur->nativeH;
    const u32* cp = it.copy;
    u32 id = cp[0], tl = cp[1], wh = cp[2], ctrl = cp[9];
    int x = (int)(tl & 0x3FF), y = (int)((tl >> 10) & 0x3FF);
    int w = (int)(wh & 0x3FF) + 1, h = (int)((wh >> 10) & 0x3FF) + 1;
    bool toXfb = (ctrl >> 14) & 1;
    bool clear = (ctrl >> 11) & 1;
    float fx = (float)src.width / (float)nativeW, fy = (float)src.height / (float)nativeH;

    // Source rectangle in target pixels (GL origin bottom-left).
    int sx0 = (int)lroundf(x * fx), sx1 = (int)lroundf((x + w) * fx);
    int sy1 = src.height - (int)lroundf(y * fy), sy0 = src.height - (int)lroundf((y + h) * fy);

    if (toXfb) {
        if (!vr) {
            int dw = sx1 - sx0, dh = sy1 - sy0;
            CopyTex& xfb = copyTex(kXfbCopyKey, dw, dh);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, src.fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, xfb.fbo);
            glDisable(GL_SCISSOR_TEST);
            glBlitFramebuffer(sx0, sy0, sx1, sy1, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            r.mXfbTex = xfb.tex;
            r.mXfbWidth = dw;
            r.mXfbHeight = dh;
        }
    } else {
        bool half = (ctrl >> 9) & 1;
        int dw = sx1 - sx0, dh = sy1 - sy0;
        if (half) {
            dw = (dw + 1) / 2;
            dh = (dh + 1) / 2;
        }
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        CopyTex& dst = copyTex(copyKey(copySlot, (cp[3] & 0xFFFFFF) << 5), dw, dh);
        u32 fmt = ((ctrl >> 4) & 7) | (((ctrl >> 3) & 1) << 3);
        bool intensity = (ctrl >> 15) & 1;
        bool zcopy = (it.copyPixFmt & 7) == 3;
        int mode;
        if (zcopy) {
            mode = 11;
        } else {
            switch (fmt) {
            case 0x0:  // R4 / I4
            case 0x1:  // R8 (I8)
            case 0x8:
                mode = intensity ? 2 : 4;
                break;
            case 0x2:
            case 0x3:
                mode = intensity ? 3 : 5;
                break;
            case 0x4:
                mode = 1;
                break;
            case 0x7:
                mode = 6;
                break;
            case 0x9:
                mode = 7;
                break;
            case 0xA:
                mode = 8;
                break;
            case 0xB:
                mode = 9;
                break;
            case 0xC:
                mode = 10;
                break;
            default:
                mode = 0;
                break;
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
        const GLenum color[1] = {GL_COLOR_ATTACHMENT0};
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, color);  // the copy covers it
        glViewport(0, 0, dw, dh);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(copyProgram);
        glUniform4f(copyRectLoc, sx0 / (float)src.width, sy0 / (float)src.height, sx1 / (float)src.width, sy1 / (float)src.height);
        glUniform1i(copyModeLoc, mode);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src.color);
        glBindSampler(0, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, src.depth);
        glBindSampler(1, 0);
        glBindVertexArray(copyVao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (dumpingCopies) {
            // PETARI_COPYDUMP=<dir>: the copies of each frame's first flat
            // replay as <dir>/copy_<frame>_<id>_fmt<f>_<w>x<h>.png (rows
            // bottom-up, so the image appears upside down).
            std::vector<u8> px((size_t)dw * dh * 4);
            glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
            char path[512];
            snprintf(path, sizeof(path), "%s/copy_%llu_%x_fmt%x_%dx%d.png", debugEnv("PETARI_COPYDUMP"), (unsigned long long)cur->number, id, fmt, dw, dh);
            port_headless_write_png(path, px.data(), dw, dh);
        }
    }

    if (clear) {
        glBindFramebuffer(GL_FRAMEBUFFER, src.fbo);
        glEnable(GL_SCISSOR_TEST);
        glScissor(sx0, sy0, sx1 - sx0, sy1 - sy0);
        u32 cm = it.copyCmode;
        bool colorUpd = (cm >> 3) & 1, alphaUpd = (cm >> 4) & 1, zUpd = (it.copyZmode >> 4) & 1;
        GLbitfield mask = 0;
        if (colorUpd || alphaUpd) {
            glColorMask(colorUpd, colorUpd, colorUpd, alphaUpd);
            float cc[4];
            u32 ar = cp[6], gb = cp[7];
            cc[0] = (ar & 0xFF) / 255.0f;
            cc[3] = ((ar >> 8) & 0xFF) / 255.0f;
            cc[1] = ((gb >> 8) & 0xFF) / 255.0f;
            cc[2] = (gb & 0xFF) / 255.0f;
            glClearColor(cc[0], cc[1], cc[2], cc[3]);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (zUpd) {
            glDepthMask(GL_TRUE);
            glClearDepthf((cp[8] & 0xFFFFFF) / 16777215.0f);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (mask) {
            glClear(mask);
        }
    }
}

void Renderer::Impl::execute(EfbTarget& efb, const EyeView* eye, EfbTarget* hud, HudMode hudMode, Renderer& r) {
    const std::vector<Item>& items = cur->items;
    const std::vector<std::shared_ptr<TexImage>>& textures = cur->frame->textures;
    u32 nativeW = cur->nativeW, nativeH = cur->nativeH;
    u32 clearAR = cur->clearAR, clearGB = cur->clearGB, clearZ = cur->clearZ;
    if (hudMode == HudMode::Target && !hud) {
        hudMode = HudMode::Inline;
    }
    // Per-eye block.
    EyeBlock eb;
    memset(&eb, 0, sizeof(eb));
    // A VR eye, as opposed to a flat replay or one of its stereo pair.
    bool vrEye = eye && !eye->flatStereo;
    if (vrEye) {
        for (int i = 0; i < 16; i++) {
            eb.vrView[i / 4][i % 4] = eye->view[i];
            eb.vrProj[i / 4][i % 4] = eye->proj[i];
        }
        eb.vrFlags[0] = 1;
        memcpy(eb.vrFocus, eye->focus, sizeof(eb.vrFocus));
        memcpy(eb.vrCut, eye->cut, sizeof(eb.vrCut));
        memcpy(eb.vrEyePos, eye->eyePos, sizeof(eb.vrEyePos));
    } else if (eye) {
        eb.vrFlags[0] = 2;
        memcpy(eb.vrStereo, eye->stereo, sizeof(eb.vrStereo));
        eb.vrStereo2[0] = eye->pointer[0];
        eb.vrStereo2[1] = eye->pointer[1];
    }
    eyeUbo = eyeUbos[eyeUboNext];
    eyeUboNext = (eyeUboNext + 1) % 8;
    glBindBuffer(GL_UNIFORM_BUFFER, eyeUbo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(eb), &eb);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, eyeUbo);
    // With the cutaway on, 3D draws use their cutaway variants, compiled in
    // the background on first use (a draw keeps its plain program until
    // then).  The variants are only used while needed: their discard costs
    // the GPU its early depth rejection.
    bool cutActive = vrEye && eye->focus[3] > 0.0f;
    std::vector<GLuint>& cutProgs = this->cur->cutProgs;
    copySlot = eye ? 1 + (eye->index & 1) : 0;
    // PETARI_COPYDUMP_EYE=1: the left eye's copies instead of the flat replay's.
    static const bool sDumpEye = debugEnv("PETARI_COPYDUMP_EYE") != nullptr;
    dumpingCopies = (sDumpEye ? (eye && eye->index == 0) : !eye) && debugEnv("PETARI_COPYDUMP") && copyDumpFrame != this->cur->number;
    if (dumpingCopies) {
        copyDumpFrame = this->cur->number;
    }
    if (cutActive) {
        const std::vector<ShaderUid>& frameUids = this->cur->uids;
        for (size_t i = 0; i < frameUids.size() && i < cutProgs.size(); i++) {
            if (!cutProgs[i] && !frameUids[i].hud) {
                ShaderUid u = frameUids[i];
                u.cutaway = 1;
                cutProgs[i] = program(u, false);
            }
        }
    }

    // Start of frame: the EFB is cleared (the previous frame's display copy
    // cleared it on the console).
    auto clearTarget = [&](EfbTarget& t, bool transparent) {
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        if (transparent) {
            glClearColor(0, 0, 0, 0);
        } else {
            glClearColor((clearAR & 255) / 255.0f, ((clearGB >> 8) & 255) / 255.0f, (clearGB & 255) / 255.0f, ((clearAR >> 8) & 255) / 255.0f);
        }
        glClearDepthf(clearZ / 16777215.0f);
        glStencilMask(0xFF);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    };
    clearTarget(efb, false);
    if (hudMode == HudMode::Target) {
        clearTarget(*hud, true);
    }
    bool inHudSection = false;
    // SpaceWarp: the player's draws tag the stencil buffer (see
    // EyeView::depthSnapshot); the stencil test itself always passes.
    bool tagPlayer = eye && eye->depthSnapshot;
    snapshotTaken = false;
    hudDraws = 0;
    if (tagPlayer) {
        glEnable(GL_STENCIL_TEST);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    }

    EfbTarget* cur = &efb;
    glBindFramebuffer(GL_FRAMEBUFFER, cur->fbo);
    GLuint lastProgram = ~0u, lastVao = ~0u;
    GLint ordinalLoc = -1;
    // GL state the batches so far left: only changes go to the driver (a
    // frame has thousands of batches, most with the state of the one
    // before).  All bits set = not known.
    struct {
        int vp[4];
        int scissorOn;
        int sc[4];
        int colorMask;
        int blendOn;
        GLenum blend[6];  // source / destination RGB and alpha, equations
        float blendAlpha;
        int depthOn, depthMask;
        GLenum depthFunc;
        int cullOn;
        GLenum cullFace;
        int stencilMask, stencilRef;
        GLuint tex[8], samp[8];
        int unit;
        void forget() { memset(this, 0xFF, sizeof(*this)); }
    } gs;
    gs.forget();
    auto setViewport = [&gs](int x, int y, int w, int h) {
        if (gs.vp[0] != x || gs.vp[1] != y || gs.vp[2] != w || gs.vp[3] != h) {
            glViewport(x, y, w, h);
            gs.vp[0] = x, gs.vp[1] = y, gs.vp[2] = w, gs.vp[3] = h;
        }
    };
    auto setScissor = [&gs](bool on, int x, int y, int w, int h) {
        if (gs.scissorOn != (int)on) {
            on ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
            gs.scissorOn = on;
        }
        if (on && (gs.sc[0] != x || gs.sc[1] != y || gs.sc[2] != w || gs.sc[3] != h)) {
            glScissor(x, y, w, h);
            gs.sc[0] = x, gs.sc[1] = y, gs.sc[2] = w, gs.sc[3] = h;
        }
    };
    auto setColorMask = [&gs](int m) {
        if (gs.colorMask != m) {
            glColorMask(m & 1, (m >> 1) & 1, (m >> 2) & 1, (m >> 3) & 1);
            gs.colorMask = m;
        }
    };
    auto setBlend = [&gs](bool on, GLenum srcRGB, GLenum dstRGB, GLenum srcA, GLenum dstA, GLenum eqRGB, GLenum eqA, float constAlpha) {
        if (gs.blendOn != (int)on) {
            on ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
            gs.blendOn = on;
        }
        if (!on) {
            return;
        }
        if (gs.blend[0] != srcRGB || gs.blend[1] != dstRGB || gs.blend[2] != srcA || gs.blend[3] != dstA) {
            glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA);
            gs.blend[0] = srcRGB, gs.blend[1] = dstRGB, gs.blend[2] = srcA, gs.blend[3] = dstA;
        }
        if (gs.blend[4] != eqRGB || gs.blend[5] != eqA) {
            glBlendEquationSeparate(eqRGB, eqA);
            gs.blend[4] = eqRGB, gs.blend[5] = eqA;
        }
        bool usesConst = srcA == GL_CONSTANT_ALPHA || dstA == GL_CONSTANT_ALPHA;
        if (usesConst && !(gs.blendAlpha == constAlpha)) {
            glBlendColor(0, 0, 0, constAlpha);
            gs.blendAlpha = constAlpha;
        }
    };
    auto setDepth = [&gs](bool on, GLenum func, bool write) {
        if (gs.depthOn != (int)on) {
            on ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
            gs.depthOn = on;
        }
        if (on && gs.depthFunc != func) {
            glDepthFunc(func);
            gs.depthFunc = func;
        }
        if (gs.depthMask != (int)write) {
            glDepthMask(write);
            gs.depthMask = write;
        }
    };
    auto setCull = [&gs](bool on, GLenum face) {
        if (gs.cullOn != (int)on) {
            on ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
            gs.cullOn = on;
        }
        if (on && gs.cullFace != face) {
            glCullFace(face);
            gs.cullFace = face;
        }
    };
    auto bindTexture = [&gs](int u, GLuint tex, GLuint samp) {
        if (gs.tex[u] != tex) {
            if (gs.unit != u) {
                glActiveTexture(GL_TEXTURE0 + u);
                gs.unit = u;
            }
            glBindTexture(GL_TEXTURE_2D, tex);
            gs.tex[u] = tex;
        }
        if (gs.samp[u] != samp) {
            glBindSampler(u, samp);
            gs.samp[u] = samp;
        }
    };
    // Where a draw writes depth, the stencil gets `ref` (1 for the player's
    // draws); elsewhere it keeps what is there.
    auto setStencil = [&gs](bool write, int ref) {
        int mask = write ? 0xFF : 0;
        if (gs.stencilMask != mask) {
            glStencilMask(mask);
            gs.stencilMask = mask;
        }
        if (write && gs.stencilRef != ref) {
            glStencilFunc(GL_ALWAYS, ref, 0xFF);
            gs.stencilRef = ref;
        }
    };
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, eyeUbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, rowBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, recBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, stateBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, recVbo);

    int stopAfter = gDebugStopAfterDraws;
    for (const Item& it : items) {
        if (stopAfter >= 0 && it.type == IT_DRAW) {
            if (stopAfter < (int)it.draws) {
                break;
            }
            stopAfter -= (int)it.draws;
        }
        if (it.type == IT_MARKER) {
            if (it.marker == MARK_SCENE_DEPTH && tagPlayer && cur == &efb) {
                // The scene's depth and the player tags, at the snapshot's
                // size (nearest sample).
                EfbTarget& snap = *eye->depthSnapshot;
                glBindFramebuffer(GL_READ_FRAMEBUFFER, efb.fbo);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, snap.fbo);
                glDisable(GL_SCISSOR_TEST);
                glBlitFramebuffer(0, 0, efb.width, efb.height, 0, 0, snap.width, snap.height, GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
                                  GL_NEAREST);
                glBindFramebuffer(GL_FRAMEBUFFER, cur->fbo);
                gs.forget();
                snapshotTaken = true;
                continue;
            }
            if (it.marker != MARK_HUD_BEGIN && it.marker != MARK_HUD_END) {
                continue;
            }
            inHudSection = it.marker == MARK_HUD_BEGIN;
            if (hudMode == HudMode::Target) {
                cur = inHudSection ? hud : &efb;
                glBindFramebuffer(GL_FRAMEBUFFER, cur->fbo);
            }
            continue;
        }
        if (inHudSection && hudMode == HudMode::Skip) {
            continue;
        }
        bool toHudTarget = inHudSection && hudMode == HudMode::Target;
        if (it.type == IT_COPY) {
            static const bool noCopies = debugEnv("PETARI_NOCOPY") != nullptr;  // perf experiments
            if (noCopies && !((it.copy[9] >> 14) & 1)) continue;
            doCopy(it, *cur, r, eye != nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, cur->fbo);
            lastProgram = ~0u;
            lastVao = ~0u;
            gs.forget();
            continue;
        }
        if (!it.program || !it.idxCount) {
            continue;
        }
        EfbTarget& t = *cur;
        float fx = (float)t.width / (float)nativeW, fy = (float)t.height / (float)nativeH;
        if (it.useVr && vrEye) {
            setViewport(0, 0, t.width, t.height);
            setScissor(false, 0, 0, 0, 0);
        } else {
            int vx, vw, sx, sr;
            float targetAspect = (float)t.width / (float)t.height;
            const float k169 = 16.0f / 9.0f;
            if (it.hudSafe && targetAspect > k169 + 0.01f) {
                float hudW = (float)t.height * k169;
                float hudOfsX = ((float)t.width - hudW) * 0.5f;
                float hudFx = hudW / (float)nativeW;
                vx = (int)lroundf(hudOfsX + it.vp[0] * hudFx);
                vw = (int)lroundf(it.vp[2] * hudFx);
                sx = (int)lroundf(hudOfsX + it.sc[0] * hudFx);
                sr = (int)lroundf(hudOfsX + it.sc[2] * hudFx);
            } else {
                vx = (int)lroundf(it.vp[0] * fx);
                vw = (int)lroundf(it.vp[2] * fx);
                sx = (int)lroundf(it.sc[0] * fx);
                sr = (int)lroundf(it.sc[2] * fx);
            }
            int vh = (int)lroundf(it.vp[3] * fy);
            int vy = t.height - (int)lroundf(it.vp[1] * fy) - vh;
            setViewport(vx, vy, vw, vh);
            int st = (int)lroundf(it.sc[1] * fy), sb = (int)lroundf(it.sc[3] * fy);
            if (it.hud && eye && eye->flatStereo && eye->stereo[3] != 0.0f) {
                // A stereo pair's HUD is shifted and widened (see the vertex
                // shader): its scissor boxes go with it.
                float shift = eye->stereo[3];
                auto moved = [&](int x) {
                    float n = (x * 2.0f / t.width - 1.0f) * (1.0f + fabsf(shift)) + shift;
                    return (int)lroundf((n + 1.0f) * 0.5f * t.width);
                };
                sx = moved(sx);
                sr = moved(sr);
            }
            setScissor(true, sx, t.height - sb, sr - sx > 0 ? sr - sx : 0, sb - st > 0 ? sb - st : 0);
        }

        if (toHudTarget) {
            hudDraws++;
            // The HUD target keeps premultiplied coverage in alpha for the
            // compositor: opaque draws write 1, blended ones accumulate.
            setColorMask((it.colorMask & 1) ? 15 : 0);
            setBlend(true, it.blend ? it.srcRGB : GL_ONE, it.blend ? it.dstRGB : GL_ZERO, it.blend ? GL_ONE : GL_CONSTANT_ALPHA,
                     it.blend ? GL_ONE_MINUS_SRC_ALPHA : GL_ZERO, it.blend ? it.eq : GL_FUNC_ADD, GL_FUNC_ADD, 1.0f);
        } else {
            setColorMask(it.colorMask);
            if (it.blend || it.dstAlpha) {
                GLenum srcA = it.srcRGB, dstA = it.dstRGB;
                if (it.dstAlpha) {
                    srcA = GL_CONSTANT_ALPHA;
                    dstA = GL_ZERO;
                }
                setBlend(true, it.blend ? it.srcRGB : GL_ONE, it.blend ? it.dstRGB : GL_ZERO, srcA, dstA, it.blend ? it.eq : GL_FUNC_ADD,
                         it.dstAlpha ? GL_FUNC_ADD : it.eq, it.dstAlphaValue);
            } else {
                setBlend(false, 0, 0, 0, 0, 0, 0, 0.0f);
            }
        }
        setDepth(it.depthTest, it.depthFunc, it.depthTest && it.depthWrite);
        setCull(it.cull, it.cullFace);
        if (tagPlayer) {
            setStencil(!toHudTarget && it.depthTest && it.depthWrite, it.player ? 1 : 0);
        }

        GLuint prog = it.program;
        if (cutActive && it.useVr && it.uid < cutProgs.size() && cutProgs[it.uid]) {
            prog = cutProgs[it.uid];
        }
        if (prog != lastProgram) {
            glUseProgram(prog);
            lastProgram = prog;
            ordinalLoc = firstOrdinalLoc(prog);
        }
        glUniform1ui(ordinalLoc, it.firstOrdinal);
        for (int u = 0; u < 8; u++) {
            u32 tr = it.tex[u];
            if (!(it.texMask & (1u << u)) || tr == ~0u) {
                continue;
            }
            if (tr & 0x80000000u) {
                auto cit = copies.find(copyKey(copySlot, (tr & 0x7FFFFFFF) << 5));
                bindTexture(u, cit != copies.end() ? cit->second.tex : 0, sampler(it.texMode0[u], it.texMode1[u], 1));
            } else if (tr < textures.size()) {
                bindTexture(u, textures[tr]->glTex, sampler(it.texMode0[u], it.texMode1[u], textures[tr]->levels));
            }
        }

        GLuint v = vao(it.flags);
        if (v != lastVao) {
            glBindVertexArray(v);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
            lastVao = v;
        }
        glBindVertexBuffer(0, vbo, (GLintptr)it.firstWord * 4, (GLsizei)(vtxWords(it.flags) * 4));
        glDrawElements(it.prim, (GLsizei)it.idxCount, GL_UNSIGNED_INT, (const void*)((uintptr_t)it.idxOffset * 4));
    }
    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    if (tagPlayer) {
        glDisable(GL_STENCIL_TEST);
    }
    glStencilMask(0xFF);
    // Nothing reads the depth buffers after the frame (the next frame starts
    // by clearing them; SpaceWarp's snapshot was taken on the way): the
    // tiler need not write them back to memory.
    const GLenum depthOnly[2] = {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
    glBindFramebuffer(GL_FRAMEBUFFER, efb.fbo);
    glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, depthOnly);
    if (hudMode == HudMode::Target) {
        glBindFramebuffer(GL_FRAMEBUFFER, hud->fbo);
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, depthOnly);
    }
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
static Renderer::Impl* sImpl;

bool Renderer::init() {
    if (!sImpl) {
        sImpl = new Impl();
        sImpl->init();
    }
    return sImpl->ready;
}

EfbTarget Renderer::createTarget(int width, int height) {
    EfbTarget t;
    t.width = width;
    t.height = height;
    glGenTextures(1, &t.color);
    glBindTexture(GL_TEXTURE_2D, t.color);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Depth with stencil: the stencil tags the player's pixels for SpaceWarp.
    glGenTextures(1, &t.depth);
    glBindTexture(GL_TEXTURE_2D, t.depth);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH24_STENCIL8, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, t.depth, 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        port_log("gl: EFB target %dx%d incomplete (0x%x)", width, height, st);
    }
    return t;
}

EfbTarget Renderer::createDepthTarget(int width, int height) {
    EfbTarget t;
    t.width = width;
    t.height = height;
    glGenTextures(1, &t.depth);
    glBindTexture(GL_TEXTURE_2D, t.depth);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH24_STENCIL8, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, t.depth, 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        port_log("gl: depth target %dx%d incomplete (0x%x)", width, height, st);
    }
    return t;
}

bool Renderer::setFoveation(EfbTarget& t, float focalX, float focalY, float gain, float foveaArea) {
    typedef void (*FoveationParams)(GLuint, GLuint, GLuint, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
    static FoveationParams params = (FoveationParams)eglGetProcAddress("glTextureFoveationParametersQCOM");
    if (!params || !t.color) {
        return false;
    }
    const GLenum kFeatureBits = 0x8BFB;   // GL_TEXTURE_FOVEATED_FEATURE_BITS_QCOM
    const GLint kEnableBits = 0x1 | 0x2;  // FOVEATION_ENABLE | FOVEATION_SCALED_BIN_METHOD
    glBindTexture(GL_TEXTURE_2D, t.color);
    glTexParameteri(GL_TEXTURE_2D, kFeatureBits, kEnableBits);
    params(t.color, 0, 0, focalX, focalY, gain, gain, foveaArea);
    return glGetError() == GL_NO_ERROR;
}

void Renderer::destroyTarget(EfbTarget& t) {
    glDeleteFramebuffers(1, &t.fbo);
    glDeleteTextures(1, &t.color);
    glDeleteTextures(1, &t.depth);
    t = EfbTarget();
}

void Renderer::setFrame(std::shared_ptr<const Frame> frame) {
    flushDeletes();
    if (!frame || !sImpl) {
        return;
    }
    Impl& im = *sImpl;
    if (im.cur && im.cur->number == frame->number) {
        return;
    }
    std::unique_ptr<Prepared> p = im.spare ? std::move(im.spare) : std::unique_ptr<Prepared>(new Prepared());
    im.syncBuilder.build(std::move(frame), *p);
    im.upload(*p, true);
    im.spare = std::move(im.cur);
    im.cur = std::move(p);
    im.evictCopies();
}

// Hands the newest recorded frame to the worker if it has not had it yet.
void Renderer::Impl::requestLatest() {
    std::lock_guard<std::mutex> lock(workLock);
    auto latest = frameQueue().latest();
    if (latest && latest->number != lastRequested) {
        lastRequested = latest->number;
        wanted = std::move(latest);
        workCv.notify_one();
    }
}

// Game thread, as each frame is presented: preparation starts right away,
// so the frame is ready by the renderer's next refresh.
static void onFramePublished() {
    if (sImpl) {
        sImpl->requestLatest();
    }
}

bool Renderer::update() {
    flushDeletes();
    if (!sImpl) {
        return false;
    }
    Impl& im = *sImpl;
    if (!im.worker.joinable()) {
        im.startCompiler();
        PortHostAllocScope hostAlloc;
        im.worker = std::thread([&im] {
            PortHostAllocScope scope;
            im.workerMain();
        });
        frameQueue().setListener(onFramePublished);
    }
    im.requestLatest();
    std::unique_ptr<Prepared> done;
    {
        std::lock_guard<std::mutex> lock(im.workLock);
        done = std::move(im.built);
    }
    if (!done) {
        return false;
    }
    im.upload(*done, false);
    std::unique_ptr<Prepared> old = std::move(im.cur);
    im.cur = std::move(done);
    im.evictCopies();
    if (old) {
        old->frame.reset();  // let the recording's memory go
        std::lock_guard<std::mutex> lock(im.workLock);
        if (!im.spare) im.spare = std::move(old);
    }
    return true;
}

bool Renderer::hasFrame() const { return sImpl && sImpl->cur; }

uint64_t Renderer::frameNumber() const { return hasFrame() ? sImpl->cur->number : 0; }

const CameraInfo& Renderer::camera() const {
    static const CameraInfo none{};
    return hasFrame() ? sImpl->cur->cam : none;
}

void Renderer::render(EfbTarget& efb, const EyeView* eye, EfbTarget* hud, HudMode hudMode) {
    if (!hasFrame()) {
        if (sImpl) {
            sImpl->snapshotTaken = false;
            sImpl->hudDraws = 0;
        }
        return;
    }
    sImpl->execute(efb, eye, hud, hudMode, *this);
}

bool Renderer::snapshotTaken() const { return sImpl && sImpl->snapshotTaken; }
int Renderer::hudDrawCount() const { return sImpl ? sImpl->hudDraws : 0; }

void setShaderCachePath(const char* path) { sShaderCachePath = path ? path : ""; }

Renderer& renderer() {
    static Renderer r;
    return r;
}

}  // namespace gpu
