#include "gl_renderer_internal.h"
#include "port/port.h"

namespace gpu {

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
    if (vkGeneration == vkDeviceGeneration()) {
        queueVkTextureDelete(vkImage, vkMemory, vkView);
    }
}

void flushDeletes() {
    std::vector<GLuint> list;
    {
        std::lock_guard<std::mutex> lock(sDeleteLock);
        list.swap(sDeleteTextures);
    }
    if (!list.empty()) {
        glDeleteTextures((GLsizei)list.size(), list.data());
    }
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

EfbTarget Renderer::createTarget(int width, int height) {
    EfbTarget t;
    t.width = width;
    t.height = height;
    glGenTextures(1, &t.color);
    glBindTexture(GL_TEXTURE_2D, t.color);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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
    const GLenum kFeatureBits = 0x8BFB;
    const GLint kEnableBits = 0x1 | 0x2;
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

}  // namespace gpu
