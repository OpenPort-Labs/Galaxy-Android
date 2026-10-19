#include "gl_renderer_internal.h"
#include "port/heap_routing.h"
#include "port/port.h"

namespace gpu {

int gDebugStopAfterDraws = debugEnv("PETARI_GLSTOP") ? atoi(debugEnv("PETARI_GLSTOP")) : -1;
Renderer::Impl* sImpl = nullptr;

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

    initCopy();
    loadShaderCache();
    ready = true;
}

bool Renderer::init() {
    if (!sImpl) {
        sImpl = new Impl();
        sImpl->init();
    }
    return sImpl->ready;
}

void Renderer::setFrame(std::shared_ptr<const Frame> frame) {
    flushDeletes();
    if (!frame || !sImpl) return;
    Impl& im = *sImpl;
    if (im.cur && im.cur->number == frame->number) return;
    std::unique_ptr<Prepared> p = im.spare ? std::move(im.spare) : std::unique_ptr<Prepared>(new Prepared());
    im.syncBuilder.build(std::move(frame), *p);
    im.upload(*p, true);
    im.spare = std::move(im.cur);
    im.cur = std::move(p);
    im.evictCopies();
}

bool Renderer::update() {
    flushDeletes();
    if (!sImpl) return false;
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
    if (!done) return false;
    im.upload(*done, false);
    std::unique_ptr<Prepared> old = std::move(im.cur);
    im.cur = std::move(done);
    im.evictCopies();
    if (old) {
        old->frame.reset();
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

Renderer& renderer() {
    static Renderer r;
    return r;
}

}  // namespace gpu
