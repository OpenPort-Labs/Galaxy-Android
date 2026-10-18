#pragma once

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <android/native_window.h>
#include "gx/gl_renderer.h"

namespace mobile_render {

class EglContext {
public:
    EglContext() = default;
    ~EglContext();

    // Initializes EGL display and GLES 3.2 configuration
    bool init();

    // Creates an EGL window surface and binds it to the current context
    bool createSurface(ANativeWindow* window, int& outWidth, int& outHeight);

    // Destroys the active EGL surface
    void destroySurface();

    // Swaps buffers to present rendered frame
    bool swapBuffers();

    bool isReady() const { return mEglReady; }
    bool hasSurface() const { return mSurface != EGL_NO_SURFACE; }

    EGLDisplay display() const { return mDisplay; }
    EGLSurface surface() const { return mSurface; }
    EGLContext context() const { return mContext; }

private:
    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    EGLSurface mSurface = EGL_NO_SURFACE;
    EGLContext mContext = EGL_NO_CONTEXT;
    EGLConfig mConfig = nullptr;
    bool mEglReady = false;
};

}  // namespace mobile_render
