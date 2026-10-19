#include "gl_renderer_internal.h"
#include "gl_shadergen.h"
#include "port/heap_routing.h"
#include "port/port.h"

namespace gpu {

std::string sShaderCachePath;
static const u32 kCacheMagic = 0x32434750;  // "PGC2"

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

void Renderer::Impl::loadShaderCache() {
    if (sShaderCachePath.empty() || cacheOut) return;
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
                u32 hdr[3];
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
    if (!prog) return;
    GLint numBinFmts = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &numBinFmts);
    if (numBinFmts <= 0) return;
    if (!cacheOut && !sShaderCachePath.empty()) {
        cacheOut = fopen(sShaderCachePath.c_str(), "ab");
    }
    if (!cacheOut) return;
    GLint len = 0;
    glGetProgramiv(prog, GL_PROGRAM_BINARY_LENGTH, &len);
    if (len <= 0) return;
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
            glFinish();
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
        if (it != programs.end()) return it->second;
        if (!sync && compiler.joinable()) {
            if (pendingPrograms.insert(uid).second) {
                compileQueue.push_front(uid);
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
    static std::mutex genLock;
    std::lock_guard<std::mutex> lock(genLock);
    *vs = genVertexShader(uid);
    *fs = genFragmentShader(uid);
}

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

void setShaderCachePath(const char* path) {
    sShaderCachePath = path ? path : "";
    if (sImpl && sImpl->ready && !sShaderCachePath.empty() && !sImpl->cacheOut) {
        sImpl->loadShaderCache();
    }
}

}  // namespace gpu
