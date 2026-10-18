#include "EglContext.h"

#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GalaxyEGL", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GalaxyEGL", __VA_ARGS__)

namespace mobile_render {

EglContext::~EglContext() {
    destroySurface();
    if (mDisplay != EGL_NO_DISPLAY) {
        if (mContext != EGL_NO_CONTEXT) {
            eglDestroyContext(mDisplay, mContext);
            mContext = EGL_NO_CONTEXT;
        }
        eglTerminate(mDisplay);
        mDisplay = EGL_NO_DISPLAY;
    }
}

bool EglContext::init() {
    if (mEglReady) return true;

    mDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (mDisplay == EGL_NO_DISPLAY || !eglInitialize(mDisplay, nullptr, nullptr)) {
        LOGE("eglInitialize failed");
        return false;
    }

    const EGLint cfgAttr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };

    EGLint count = 0;
    if (!eglChooseConfig(mDisplay, cfgAttr, &mConfig, 1, &count) || count == 0) {
        LOGE("eglChooseConfig failed");
        return false;
    }

    const EGLint ctxAttr[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 2,
        EGL_NONE
    };

    mContext = eglCreateContext(mDisplay, mConfig, EGL_NO_CONTEXT, ctxAttr);
    if (mContext == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed (0x%x)", eglGetError());
        return false;
    }

    mEglReady = true;
    return true;
}

bool EglContext::createSurface(ANativeWindow* window, int& outWidth, int& outHeight) {
    if (!window || !mEglReady) return false;

    EGLint format = 0;
    eglGetConfigAttrib(mDisplay, mConfig, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(window, 0, 0, format);

    mSurface = eglCreateWindowSurface(mDisplay, mConfig, window, nullptr);
    if (mSurface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed (0x%x)", eglGetError());
        return false;
    }

    if (!eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)) {
        LOGE("eglMakeCurrent failed (0x%x)", eglGetError());
        return false;
    }

    eglQuerySurface(mDisplay, mSurface, EGL_WIDTH, &outWidth);
    eglQuerySurface(mDisplay, mSurface, EGL_HEIGHT, &outHeight);
    eglSwapInterval(mDisplay, 1);

    LOGI("Window surface ready: %dx%d (GL: %s)", outWidth, outHeight,
         (const char*)glGetString(GL_RENDERER));
    return true;
}

void EglContext::destroySurface() {
    if (mDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (mSurface != EGL_NO_SURFACE) {
            eglDestroySurface(mDisplay, mSurface);
            mSurface = EGL_NO_SURFACE;
        }
    }
}

bool EglContext::swapBuffers() {
    if (mDisplay != EGL_NO_DISPLAY && mSurface != EGL_NO_SURFACE) {
        return eglSwapBuffers(mDisplay, mSurface) == EGL_TRUE;
    }
    return false;
}

}  // namespace mobile_render
