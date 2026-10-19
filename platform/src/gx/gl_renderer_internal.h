#pragma once

#include "gl_renderer.h"
#include "gx_builder.h"
#include "gl_shadergen.h"
#include "gpu.h"

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gpu {

struct CopyTex {
    GLuint tex = 0, fbo = 0;
    int w = 0, h = 0;
    u64 frame = 0;
};

struct Renderer::Impl {
    bool ready = false;
    std::mutex progLock;
    std::unordered_map<ShaderUid, GLuint, ShaderUidHash> programs;
    std::unordered_set<ShaderUid, ShaderUidHash> pendingPrograms;
    std::deque<ShaderUid> compileQueue;
    std::condition_variable compileCv;
    std::thread compiler;
    FILE* cacheOut = nullptr;
    std::vector<ShaderUid> staleUids;
    std::unordered_map<u32, GLuint> vaos;
    std::unordered_map<u64, GLuint> samplers;
    std::unordered_map<u64, CopyTex> copies;
    u32 copySlot = 0;
    GLuint vbo = 0, recVbo = 0, ibo = 0, eyeUbo = 0;
    GLuint eyeUbos[8] = {};
    int eyeUboNext = 0;
    GLuint rowBuf = 0, recBuf = 0, stateBuf = 0;
    GLuint rowTex = 0, recTex = 0, stateTex = 0, vtxRecTex = 0;

    struct FrameBuffers {
        GLuint buf[6] = {};
        size_t cap[6] = {};
        GLuint tex[4] = {};
        u32 rowCapH = 0;
        u32 recCapH = 0;
        u32 stateCapH = 0;
        u32 vtxRecCapH = 0;
    };
    FrameBuffers frameBufs[3];
    int frameBufNext = 0;
    GLuint copyProgram = 0, copyVao = 0;
    GLint copyRectLoc = -1, copyModeLoc = -1;

    std::unique_ptr<Prepared> cur;
    Builder syncBuilder;

    std::thread worker;
    std::mutex workLock;
    std::condition_variable workCv;
    std::shared_ptr<const Frame> wanted;
    std::unique_ptr<Prepared> built;
    std::unique_ptr<Prepared> spare;
    u64 lastRequested = 0;
    Builder workerBuilder;

    std::unordered_map<GLuint, GLint> ordinalLocs;
    bool snapshotTaken = false;
    int hudDraws = 0;
    bool dumpingCopies = false;
    u64 copyDumpFrame = ~0ull;

    void init();
    void initCopy();
    GLuint program(const ShaderUid& uid, bool sync);
    GLuint buildProgram(const ShaderUid& uid);
    static void generateSources(const ShaderUid& uid, std::string* vs, std::string* fs);
    static u64 sourceHash(const ShaderUid& uid);
    void loadShaderCache();
    void storeBinary(const ShaderUid& uid, GLuint prog);
    void startCompiler();
    GLint firstOrdinalLoc(GLuint prog);
    GLuint vao(u32 flags);
    GLuint sampler(u32 mode0, u32 mode1, u32 levels);
    void upload(Prepared& p, bool syncPrograms);
    void workerMain();
    void requestLatest();
    void execute(EfbTarget& efb, const EyeView* eye, EfbTarget* hud, HudMode hudMode, Renderer& r);
    void doCopy(const Item& it, EfbTarget& src, Renderer& r, bool vr);
    CopyTex& copyTex(u64 key, int w, int h);
    void evictCopies();
    void drawItem(const Item& it, struct GlStateTracker& gs, bool toHudTarget, bool tagPlayer,
                  bool cutActive, const std::vector<GLuint>& cutProgs,
                  const std::vector<std::shared_ptr<TexImage>>& textures,
                  EfbTarget& t, const EyeView* eye, bool vrEye, GLuint& lastProgram,
                  GLuint& lastVao, GLint& ordinalLoc);
};

struct GlStateTracker {
    int vp[4];
    int scissorOn;
    int sc[4];
    int colorMask;
    int blendOn;
    GLenum blend[6];
    float blendAlpha;
    int depthOn, depthMask;
    GLenum depthFunc;
    int cullOn;
    GLenum cullFace;
    int stencilMask, stencilRef;
    GLuint tex[8], samp[8];
    int unit;

    void forget();
    void setViewport(int x, int y, int w, int h);
    void setScissor(bool on, int x, int y, int w, int h);
    void setColorMask(int m);
    void setBlend(bool on, GLenum srcRGB, GLenum dstRGB, GLenum srcA, GLenum dstA, GLenum eqRGB, GLenum eqA, float constAlpha);
    void setDepth(bool on, GLenum func, bool write);
    void setCull(bool on, GLenum face);
    void bindTexture(int u, GLuint t, GLuint s);
    void setStencil(bool write, int ref);
};

extern Renderer::Impl* sImpl;
extern std::string sShaderCachePath;

void queueGlTextureDelete(uint32_t tex);
void flushDeletes();
void onFramePublished();
GLuint compileShader(GLenum type, const std::string& src);
GLuint linkProgram(GLuint vs, GLuint fs);

}  // namespace gpu
