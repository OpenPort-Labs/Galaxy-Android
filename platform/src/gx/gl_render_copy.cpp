#include "gl_renderer_internal.h"
#include "port/port.h"

extern "C" void port_headless_write_png(const char* path, const unsigned char* rgba, int w, int h);

namespace gpu {

static const char* kCopyVs = R"(#version 320 es
precision highp float;
uniform vec4 u_rect;
out vec2 v_uv;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
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
    if (u_mode == 0) o = c;
    else if (u_mode == 1) o = vec4(c.rgb, 1.0);
    else if (u_mode == 2) o = vec4(y);
    else if (u_mode == 3) o = vec4(y, y, y, c.a);
    else if (u_mode == 4) o = vec4(c.r);
    else if (u_mode == 5) o = vec4(c.r, c.r, c.r, c.a);
    else if (u_mode == 6) o = vec4(c.a);
    else if (u_mode == 7) o = vec4(c.g);
    else if (u_mode == 8) o = vec4(c.b);
    else if (u_mode == 9) o = vec4(c.g, c.g, c.g, c.r);
    else if (u_mode == 10) o = vec4(c.b, c.b, c.b, c.g);
    else {
        float z = texture(s_depth, v_uv).r;
        o = vec4(z, fract(z * 256.0), fract(z * 65536.0), z);
    }
}
)";

void Renderer::Impl::initCopy() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, kCopyVs);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kCopyFs);
    copyProgram = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    copyRectLoc = glGetUniformLocation(copyProgram, "u_rect");
    copyModeLoc = glGetUniformLocation(copyProgram, "u_mode");
    glGenVertexArrays(1, &copyVao);
}

void Renderer::Impl::doCopy(const Item& it, EfbTarget& src, Renderer& r, bool vr) {
    u32 nativeW = cur->nativeW, nativeH = cur->nativeH;
    const u32* cp = it.copy;
    u32 id = cp[0], tl = cp[1], wh = cp[2], ctrl = cp[9];
    int x = (int)(tl & 0x3FF), y = (int)((tl >> 10) & 0x3FF);
    int w = (int)(wh & 0x3FF) + 1, h = (int)((wh >> 10) & 0x3FF) + 1;
    bool toXfb = (ctrl >> 14) & 1;
    bool clear = (ctrl >> 11) & 1;
    float fx = (float)src.width / (float)nativeW, fy = (float)src.height / (float)nativeH;

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
            case 0x0:
            case 0x1:
            case 0x8: mode = intensity ? 2 : 4; break;
            case 0x2:
            case 0x3: mode = intensity ? 3 : 5; break;
            case 0x4: mode = 1; break;
            case 0x7: mode = 6; break;
            case 0x9: mode = 7; break;
            case 0xA: mode = 8; break;
            case 0xB: mode = 9; break;
            case 0xC: mode = 10; break;
            default: mode = 0; break;
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
        const GLenum color[1] = {GL_COLOR_ATTACHMENT0};
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, color);
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
            std::vector<u8> px((size_t)dw * dh * 4);
            glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
            char path[512];
            snprintf(path, sizeof(path), "%s/copy_%llu_%x_fmt%x_%dx%d.png", debugEnv("PETARI_COPYDUMP"),
                     (unsigned long long)cur->number, id, fmt, dw, dh);
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

}  // namespace gpu
