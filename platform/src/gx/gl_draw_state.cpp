#include "gl_renderer_internal.h"

namespace gpu {

void GlStateTracker::forget() {
    memset(this, 0xFF, sizeof(*this));
}

void GlStateTracker::setViewport(int x, int y, int w, int h) {
    if (vp[0] != x || vp[1] != y || vp[2] != w || vp[3] != h) {
        glViewport(x, y, w, h);
        vp[0] = x;
        vp[1] = y;
        vp[2] = w;
        vp[3] = h;
    }
}

void GlStateTracker::setScissor(bool on, int x, int y, int w, int h) {
    if (scissorOn != (int)on) {
        on ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
        scissorOn = on;
    }
    if (on && (sc[0] != x || sc[1] != y || sc[2] != w || sc[3] != h)) {
        glScissor(x, y, w, h);
        sc[0] = x;
        sc[1] = y;
        sc[2] = w;
        sc[3] = h;
    }
}

void GlStateTracker::setColorMask(int m) {
    if (colorMask != m) {
        glColorMask(m & 1, (m >> 1) & 1, (m >> 2) & 1, (m >> 3) & 1);
        colorMask = m;
    }
}

void GlStateTracker::setBlend(bool on, GLenum srcRGB, GLenum dstRGB, GLenum srcA, GLenum dstA, GLenum eqRGB, GLenum eqA, float constAlpha) {
    if (blendOn != (int)on) {
        on ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        blendOn = on;
    }
    if (!on) {
        return;
    }
    if (blend[0] != srcRGB || blend[1] != dstRGB || blend[2] != srcA || blend[3] != dstA) {
        glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA);
        blend[0] = srcRGB;
        blend[1] = dstRGB;
        blend[2] = srcA;
        blend[3] = dstA;
    }
    if (blend[4] != eqRGB || blend[5] != eqA) {
        glBlendEquationSeparate(eqRGB, eqA);
        blend[4] = eqRGB;
        blend[5] = eqA;
    }
    bool usesConst = srcA == GL_CONSTANT_ALPHA || dstA == GL_CONSTANT_ALPHA;
    if (usesConst && !(blendAlpha == constAlpha)) {
        glBlendColor(0, 0, 0, constAlpha);
        blendAlpha = constAlpha;
    }
}

void GlStateTracker::setDepth(bool on, GLenum func, bool write) {
    if (depthOn != (int)on) {
        on ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
        depthOn = on;
    }
    if (on && depthFunc != func) {
        glDepthFunc(func);
        depthFunc = func;
    }
    if (depthMask != (int)write) {
        glDepthMask(write);
        depthMask = write;
    }
}

void GlStateTracker::setCull(bool on, GLenum face) {
    if (cullOn != (int)on) {
        on ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
        cullOn = on;
    }
    if (on && cullFace != face) {
        glCullFace(face);
        cullFace = face;
    }
}

void GlStateTracker::bindTexture(int u, GLuint t, GLuint s) {
    if (tex[u] != t) {
        if (unit != u) {
            glActiveTexture(GL_TEXTURE0 + u);
            unit = u;
        }
        glBindTexture(GL_TEXTURE_2D, t);
        tex[u] = t;
    }
    if (samp[u] != s) {
        glBindSampler(u, s);
        samp[u] = s;
    }
}

void GlStateTracker::setStencil(bool write, int ref) {
    int mask = write ? 0xFF : 0;
    if (stencilMask != mask) {
        glStencilMask(mask);
        stencilMask = mask;
    }
    if (write && stencilRef != ref) {
        glStencilFunc(GL_ALWAYS, ref, 0xFF);
        stencilRef = ref;
    }
}

}  // namespace gpu
