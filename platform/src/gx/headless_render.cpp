// Offscreen rendering for bring-up: renders the latest recorded frame with
// the GL renderer into a pbuffer-backed context and writes it as a PNG.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "gl_renderer.h"
#include "headless_dump.h"
#include "../xr/vr_rig.h"
#include "gpu.h"
#include "port/port.h"

namespace {

EGLDisplay sDisplay = EGL_NO_DISPLAY;
EGLContext sContext = EGL_NO_CONTEXT;
EGLSurface sSurface = EGL_NO_SURFACE;
gpu::EfbTarget sTarget;

bool initContext() {
    if (sContext != EGL_NO_CONTEXT) {
        return true;
    }
    sDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(sDisplay, nullptr, nullptr)) {
        port_log("headless: eglInitialize failed");
        return false;
    }
    const EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                              EGL_BLUE_SIZE,       8,                  EGL_ALPHA_SIZE,   8,               EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(sDisplay, cfgAttr, &cfg, 1, &n) || n == 0) {
        port_log("headless: no EGL config");
        return false;
    }
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE, EGL_NONE};
    sContext = eglCreateContext(sDisplay, cfg, EGL_NO_CONTEXT, ctxAttr);
    const EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    sSurface = eglCreatePbufferSurface(sDisplay, cfg, pbAttr);
    if (sContext == EGL_NO_CONTEXT || !eglMakeCurrent(sDisplay, sSurface, sSurface, sContext)) {
        port_log("headless: cannot create/make current a GLES 3.2 context (0x%x)", eglGetError());
        return false;
    }
    port_log("headless: GL %s / %s", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(
        [](GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* msg, const void*) {
            if (severity != GL_DEBUG_SEVERITY_NOTIFICATION) {
                static int count;
                if (count++ < 50) port_log("gl debug (type 0x%x sev 0x%x): %s", type, severity, msg);
            }
        },
        nullptr);
    gpu::setShaderCachePath(getenv("PETARI_SHADER_CACHE") ? getenv("PETARI_SHADER_CACHE") : "/data/local/tmp/petari/shaders.bin");
    return gpu::renderer().init();
}

}  // namespace

// Renders the latest recorded frame at `scale` x the native EFB size and
// writes `path` (PNG).  Returns the frame number, or 0 if nothing was drawn.
extern "C" unsigned long long port_headless_render(const char* path, int scale) {
    if (!initContext()) {
        return 0;
    }
    auto frame = gpu::frameQueue().latest();
    if (!frame) {
        return 0;
    }
    gpu::Renderer& r = gpu::renderer();
    auto msSince = [](const timespec& a) {
        timespec b;
        clock_gettime(CLOCK_MONOTONIC, &b);
        return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6;
    };
    timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (gpu::debugEnv("PETARI_ASYNC")) {
        // Exercise the worker-thread path the headset uses.
        for (int i = 0; i < 200 && !r.update(); i++) {
            usleep(5000);
        }
    } else {
        r.setFrame(frame);
    }
    glFinish();
    double prepMs = msSince(t0);
    int w = 640 * scale, h = 456 * scale;
    if (sTarget.width != w || sTarget.height != h) {
        if (sTarget.fbo) {
            r.destroyTarget(sTarget);
        }
        sTarget = r.createTarget(w, h);
    }
    // PETARI_GLSTEPS=a,b,...: also write <path>_s<N>.png stopped after N draws.
    if (const char* steps = gpu::debugEnv("PETARI_GLSTEPS")) {
        std::vector<unsigned char> sp((size_t)w * h * 4);
        for (const char* p = steps; *p;) {
            int n = atoi(p);
            gpu::gDebugStopAfterDraws = n;
            r.render(sTarget, nullptr, nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, sTarget.fbo);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, sp.data());
            for (size_t i = 3; i < sp.size(); i += 4) sp[i] = 255;
            std::string sp_path = std::string(path).substr(0, strlen(path) - 4) + "_s" + std::to_string(n) + ".png";
            headless_write_png(sp_path.c_str(), sp.data(), w, h);
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
        gpu::gDebugStopAfterDraws = -1;
    }
    // PETARI_VRTEST: also render <path>_vr.png through the VR rig, from a
    // head at the stage origin (PETARI_VRHEAD="x,y,z,yawDeg,pitchDeg").
    if (gpu::debugEnv("PETARI_VRTEST") && r.camera().valid) {
        vr::RigState rig;
        vr::RigParams params;
        xm::Mat4 stageFromView = vr::updateRig(rig, params, r.camera(), 0.0f);
        float hx = 0, hy = 0, hz = 0, yaw = 0, pitch = 0;
        if (const char* hp = gpu::debugEnv("PETARI_VRHEAD")) {
            sscanf(hp, "%f,%f,%f,%f,%f", &hx, &hy, &hz, &yaw, &pitch);
        }
        float cy = cosf(yaw * 0.5f * 3.14159265f / 180.0f), sy = sinf(yaw * 0.5f * 3.14159265f / 180.0f);
        float cp = cosf(pitch * 0.5f * 3.14159265f / 180.0f), sp = sinf(pitch * 0.5f * 3.14159265f / 180.0f);
        xm::Quat qy{0, sy, 0, cy}, qp{sp, 0, 0, cp};
        xm::Quat q{qy.w * qp.x, qy.y * qp.w, -qy.y * qp.x, qy.w * qp.w};  // yaw * pitch
        xm::Mat4 eyeFromStage = xm::inversePose(q, {hx, hy, hz});
        float tv = (float)h / (float)w;
        xm::Mat4 proj = xm::projectionFov(-0.7853982f, 0.7853982f, atanf(tv), -atanf(tv), 0.05f, 1000.0f);
        gpu::EyeView ev;
        vr::toRowMajor(eyeFromStage * stageFromView, ev.view);
        vr::toRowMajor(proj, ev.proj);
        bool cutTest = gpu::debugEnv("PETARI_CUTTEST") != nullptr;
        vr::setCutaway(ev, eyeFromStage * stageFromView, r.camera(), cutTest || (r.camera().flags & PORT_GX_CAMERA_OCCLUDED) ? 1.0f : 0.0f,
                       cutTest ? 3.0f : 1.0f);
        if (const char* vs = gpu::debugEnv("PETARI_VRSIZE")) {
            struct Timed {
                int w, h;
                float gain;
                gpu::EfbTarget t;
            };
            static std::vector<Timed> sEyeTs;
            std::vector<float> gains;
            if (const char* fv = gpu::debugEnv("PETARI_FOVEATE")) {
                for (const char* p = fv; *p;) {
                    gains.push_back((float)atof(p));
                    while (*p && *p != ',') p++;
                    if (*p == ',') p++;
                }
            } else {
                gains.push_back(0.0f);
            }
            for (const char* p = vs; *p;) {
                int ew = 0, eh = 0;
                sscanf(p, "%dx%d", &ew, &eh);
                while (*p && *p != ',') p++;
                if (*p == ',') p++;
                if (ew <= 0 || eh <= 0) continue;
                for (float gain : gains) {
                    Timed* tm = nullptr;
                    for (Timed& x : sEyeTs) {
                        if (x.w == ew && x.h == eh && x.gain == gain) tm = &x;
                    }
                    if (!tm) {
                        sEyeTs.push_back({ew, eh, gain, r.createTarget(ew, eh)});
                        tm = &sEyeTs.back();
                        if (gain > 0.0f && !r.setFoveation(tm->t, 0.0f, 0.0f, gain, 0.0f)) {
                            port_log("headless: foveation unsupported");
                        }
                    }
                    r.render(tm->t, &ev, nullptr, gpu::HudMode::Skip);
                    glFinish();
                    timespec ta;
                    clock_gettime(CLOCK_MONOTONIC, &ta);
                    for (int i = 0; i < 4; i++) r.render(tm->t, &ev, nullptr, gpu::HudMode::Skip);
                    glFinish();
                    long clk = 0;
                    if (FILE* g = fopen("/sys/class/kgsl/kgsl-3d0/gpuclk", "r")) {
                        if (fscanf(g, "%ld", &clk) != 1) clk = 0;
                        fclose(g);
                    }
                    port_log("headless: VR eye %dx%d foveation %.1f replay %.2f ms (GPU %ld MHz)", ew, eh, gain, msSince(ta) / 4.0, clk / 1000000);
                }
            }
        }
        if (const char* steps = gpu::debugEnv("PETARI_GLSTEPS")) {
            std::vector<unsigned char> sp((size_t)w * h * 4);
            for (const char* p = steps; *p;) {
                int n = atoi(p);
                gpu::gDebugStopAfterDraws = n;
                r.render(sTarget, &ev, nullptr, gpu::HudMode::Skip);
                glBindFramebuffer(GL_FRAMEBUFFER, sTarget.fbo);
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, sp.data());
                for (size_t i = 3; i < sp.size(); i += 4) sp[i] = 255;
                std::string sp_path = std::string(path).substr(0, strlen(path) - 4) + "_vr_s" + std::to_string(n) + ".png";
                headless_write_png(sp_path.c_str(), sp.data(), w, h);
                while (*p && *p != ',') p++;
                if (*p == ',') p++;
            }
            gpu::gDebugStopAfterDraws = -1;
        }
        r.render(sTarget, &ev, nullptr, gpu::HudMode::Skip);
        std::vector<unsigned char> vp((size_t)w * h * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, sTarget.fbo);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, vp.data());
        for (size_t i = 3; i < vp.size(); i += 4) vp[i] = 255;
        std::string vpath = std::string(path).substr(0, strlen(path) - 4) + "_vr.png";
        headless_write_png(vpath.c_str(), vp.data(), w, h);
        const gpu::CameraInfo& c = r.camera();
        const float* v = c.view;
        const float* pl = c.player;
        xm::Vec3 inView = {v[0] * pl[0] + v[1] * pl[1] + v[2] * pl[2] + v[3], v[4] * pl[0] + v[5] * pl[1] + v[6] * pl[2] + v[7],
                           v[8] * pl[0] + v[9] * pl[1] + v[10] * pl[2] + v[11]};
        xm::Vec3 ps = xm::transformPoint(stageFromView, inView);
        port_log("headless: VR view -> %s (flags %x aspect %.2f fovy %.1f, watch %.0f %.0f %.0f, up %.2f %.2f %.2f, player at stage %.2f %.2f %.2f)", vpath.c_str(),
                 c.flags, c.aspect, c.fovy, c.watch[0], c.watch[1], c.watch[2], c.watchUp[0], c.watchUp[1], c.watchUp[2], ps.x, ps.y, ps.z);
    }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < 3; i++) {
        r.render(sTarget, nullptr, nullptr);
    }
    glFinish();
    double renderMs = msSince(t0) / 3.0;
    port_log("headless: prepare %.2f ms, replay %.2f ms", prepMs, renderMs);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        port_log("headless: GL error 0x%x after render", err);
    }
    std::vector<unsigned char> px((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, sTarget.fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (size_t i = 3; i < px.size(); i += 4) {
        px[i] = 255;
    }
    headless_write_png(path, px.data(), w, h);
    port_log("headless: frame %llu (%u draws, %zu textures) -> %s", (unsigned long long)frame->number, frame->draws, frame->textures.size(), path);
    return frame->number;
}
