// GLES 3.2 replay of recorded GX frames.
#include "gl_renderer.h"
#include "gx_builder.h"

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
    // The Vulkan renderer keeps its own images on the TexImage too (see
    // vk_renderer.cpp): release them on the render thread as well.
    queueVkTextureDelete(vkImage, vkMemory, vkView);
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


struct CopyTex {
    GLuint tex = 0, fbo = 0;
    int w = 0, h = 0;
    u64 frame = 0;  // frame whose replay last wrote it
};

}  // namespace

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
    GLuint rowTex = 0, recTex = 0, stateTex = 0, vtxRecTex = 0;
    // Each frame's data goes into the next of three buffer sets, which the
    // GPU finished drawing from frames ago: refilling buffers still in use
    // made the driver wait for the GPU (up to 15 ms per frame at 120 Hz).
    struct FrameBuffers {
        GLuint buf[6] = {};    // vbo, recVbo, ibo, rowBuf, recBuf, stateBuf
        size_t cap[6] = {};    // bytes allocated
        GLuint tex[4] = {};    // rowTex, recTex, stateTex, vtxRecTex
        u32 rowCapH = 0;
        u32 recCapH = 0;
        u32 stateCapH = 0;
        u32 vtxRecCapH = 0;
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
        glGenTextures(4, fb.tex);
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
    GLint maxTexBufSize = 0, maxVertexTexUnits = 0, maxCombinedTexUnits = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTexBufSize);
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &maxVertexTexUnits);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxCombinedTexUnits);
    port_log("gl: %d vertex attribs, %d/%d storage blocks (vs/fs), %d MB max block, texBuf max size %d, vtx tex units %d, combined tex units %d",
             attribs, vsBlocks, fsBlocks, blockSize >> 20, maxTexBufSize, maxVertexTexUnits, maxCombinedTexUnits);
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
    if (fsBlocks < 1 || maxVertexTexUnits < 4) {
        port_log("gl: this GPU lacks the fragment storage blocks or vertex texture units the renderer needs");
    }
    if (maxTexBufSize < 64) {
        port_log("gl: this GPU has insufficient texture buffer size (%d bytes, need >= 64)", maxTexBufSize);
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
    if (sShaderCachePath.empty() || cacheOut) {
        return;
    }
    GLint numBinFmts = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &numBinFmts);
    if (numBinFmts <= 0) {
        port_log("gl: program binary formats not supported on this GPU driver (GL_NUM_PROGRAM_BINARY_FORMATS = %d)", numBinFmts);
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
    if (!prog) {
        return;
    }
    GLint numBinFmts = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &numBinFmts);
    if (numBinFmts <= 0) {
        return;
    }
    if (!cacheOut && !sShaderCachePath.empty()) {
        cacheOut = fopen(sShaderCachePath.c_str(), "ab");
    }
    if (!cacheOut) {
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
    if (!mips) {
        glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, 0.0f);
        glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, 0.0f);
    } else {
        glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, std::min((float)(levels - 1), (mode1 & 0xFF) / 16.0f));
        glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, std::min((float)(levels - 1), ((mode1 >> 8) & 0xFF) / 16.0f));
    }
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
    const u64 kKeepFrames = 16;
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
// Upload
// ---------------------------------------------------------------------------

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
        }
        glBufferData(target, (GLsizeiptr)fb.cap[i], nullptr, GL_DYNAMIC_DRAW);
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

    // Upload rows as 2D texture (width 2048, each texel is vec4/RGBA32F)
    u32 rowCount = (u32)(p.rows.size() / 4);
    u32 rowH = (rowCount + 2047) / 2048;
    if (rowH < 1) rowH = 1;
    p.rows.resize((size_t)rowH * 2048 * 4, 0.0f);
    glBindTexture(GL_TEXTURE_2D, fb.tex[0]);
    if (rowH > fb.rowCapH) {
        fb.rowCapH = rowH + 64;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 2048, (GLsizei)fb.rowCapH, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2048, (GLsizei)rowH, GL_RGBA, GL_FLOAT, p.rows.data());

    // Upload recs as 2D texture (width 2048, each texel is u32/R32UI)
    u32 recCount = (u32)p.recs.size();
    u32 recH = (recCount + 2047) / 2048;
    if (recH < 1) recH = 1;
    p.recs.resize((size_t)recH * 2048, 0u);
    glBindTexture(GL_TEXTURE_2D, fb.tex[1]);
    if (recH > fb.recCapH) {
        fb.recCapH = recH + 64;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 2048, (GLsizei)fb.recCapH, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2048, (GLsizei)recH, GL_RED_INTEGER, GL_UNSIGNED_INT, p.recs.data());

    // Upload states as 2D texture (width 2048, each texel is vec4/RGBA32F)
    u32 stCount = (u32)(p.states.size() / 4);
    u32 stH = (stCount + 2047) / 2048;
    if (stH < 1) stH = 1;
    p.states.resize((size_t)stH * 2048 * 4, 0.0f);
    glBindTexture(GL_TEXTURE_2D, fb.tex[2]);
    if (stH > fb.stateCapH) {
        fb.stateCapH = stH + 64;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 2048, (GLsizei)fb.stateCapH, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2048, (GLsizei)stH, GL_RGBA, GL_FLOAT, p.states.data());

    // Upload vtxRec as 2D texture (width 2048, each texel is u32/R32UI)
    u32 vtxRecCount = (u32)p.vtxRec.size();
    u32 vtxRecH = (vtxRecCount + 2047) / 2048;
    if (vtxRecH < 1) vtxRecH = 1;
    p.vtxRec.resize((size_t)vtxRecH * 2048, 0u);
    glBindTexture(GL_TEXTURE_2D, fb.tex[3]);
    if (vtxRecH > fb.vtxRecCapH) {
        fb.vtxRecCapH = vtxRecH + 64;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 2048, (GLsizei)fb.vtxRecCapH, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2048, (GLsizei)vtxRecH, GL_RED_INTEGER, GL_UNSIGNED_INT, p.vtxRec.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    vbo = fb.buf[0];
    recVbo = fb.buf[1];
    ibo = fb.buf[2];
    rowBuf = fb.buf[3];
    recBuf = fb.buf[4];
    stateBuf = fb.buf[5];
    rowTex = fb.tex[0];
    recTex = fb.tex[1];
    stateTex = fb.tex[2];
    vtxRecTex = fb.tex[3];

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

// logItems is implemented in gx_builder.cpp

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
            r.mXfbFbo = xfb.fbo;
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

    if (clear && !toXfb) {
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
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
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

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, rowTex);
    glBindSampler(8, 0);

    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, recTex);
    glBindSampler(9, 0);

    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, stateTex);
    glBindSampler(10, 0);

    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, vtxRecTex);
    glBindSampler(11, 0);

    glActiveTexture(GL_TEXTURE0);

    int stopAfter = gDebugStopAfterDraws;
    static u64 sLastLoggedFrame = 0;
    bool shouldLog = (this->cur->number % 300 == 1 && this->cur->number != sLastLoggedFrame);
    if (shouldLog) {
        sLastLoggedFrame = this->cur->number;
        port_log("execute: frame %llu, items %zu, vrEye=%d, native %ux%u", (unsigned long long)this->cur->number, items.size(), vrEye ? 1 : 0, nativeW, nativeH);
    }
    int drawCount = 0;
    for (const Item& it : items) {
        if (shouldLog) {
            if (it.type == IT_DRAW) {
                drawCount++;
                if (drawCount <= 20 || !it.hud) {
                    port_log("  draw #%d: prim=%x idxCount=%u useVr=%d hud=%d hudSafe=%d prog=%u vp=[%.0f,%.0f,%.0f,%.0f] sc=[%d,%d,%d,%d] zTest=%d zWrite=%d zFunc=%x cull=%d",
                             drawCount, it.prim, it.idxCount, it.useVr, it.hud, it.hudSafe, it.program,
                             it.vp[0], it.vp[1], it.vp[2], it.vp[3],
                             it.sc[0], it.sc[1], it.sc[2], it.sc[3],
                             it.depthTest, it.depthWrite, it.depthFunc, it.cull);
                }
            } else if (it.type == IT_COPY) {
                const u32* cp = it.copy;
                port_log("  copy: toXfb=%d clear=%d rect=[%u,%u %ux%u] cmode=0x%x zmode=0x%x",
                         (cp[9] >> 14) & 1, (cp[9] >> 11) & 1,
                         cp[1] & 0x3FF, (cp[1] >> 10) & 0x3FF,
                         (cp[2] & 0x3FF) + 1, ((cp[2] >> 10) & 0x3FF) + 1,
                         it.copyCmode, it.copyZmode);
            } else if (it.type == IT_MARKER) {
                port_log("  marker: %u", it.marker);
            }
        }
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
        GLuint prog = it.program;
        if (!prog && it.uid < this->cur->uids.size()) {
            prog = program(this->cur->uids[it.uid], true);
        }
        if (!prog || !it.idxCount) {
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
                u32 addr = (tr & 0x7FFFFFFF) << 5;
                auto cit = copies.find(copyKey(copySlot, addr));
                if (cit == copies.end() && copySlot != 0) {
                    cit = copies.find(copyKey(0, addr));
                }
                GLuint ctex = (cit != copies.end()) ? cit->second.tex : 0;
                bindTexture(u, ctex, sampler(it.texMode0[u], it.texMode1[u], 1));
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
        destroyTarget(t);
        return {};
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

void setShaderCachePath(const char* path) {
    sShaderCachePath = path ? path : "";
    if (sImpl && sImpl->ready && !sShaderCachePath.empty() && !sImpl->cacheOut) {
        sImpl->loadShaderCache();
    }
}

Renderer& renderer() {
    static Renderer r;
    return r;
}

}  // namespace gpu
