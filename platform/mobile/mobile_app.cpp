// Flat-screen Android Mobile application layer (AOSP / Phones).
// Replaces the OpenXR bootstrap with ANativeWindow + EGL surface rendering
// and standard monoscopic presentation.

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include "gx/gl_renderer.h"
#include "mobile_ui.h"
#include "port/input.h"
#include "port/port.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "PetariVR", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PetariVR", __VA_ARGS__)

extern "C" void port_mem_set_reserved_window(uintptr_t base, size_t size);
extern "C" void port_boot(const char* dataRoot, const char* saveRoot);
extern std::atomic<uint64_t> gPresentedFrames;

namespace {

struct MobileApp {
    android_app* android = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLConfig config = nullptr;

    bool windowReady = false;
    bool eglReady = false;
    int screenWidth = 0;
    int screenHeight = 0;

    gpu::EfbTarget efbTarget;
    bool targetReady = false;

    bool gameBooted = false;
    std::string dataRoot;
    std::string saveRoot;

    // Viewport calculation for 16:9 pillarbox/letterbox
    int vpX = 0, vpY = 0, vpW = 0, vpH = 0;

    // Input state
    PortPadState pad{};
    bool touchActive = false;
    float touchX = 0.0f, touchY = 0.0f;

    // Virtual touch controls state
    int stickPointerId = -1;
    float stickOriginX = 0.0f;
    float stickOriginY = 0.0f;
    int spinFrames = 0;
};

MobileApp gMobile;

bool isDir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool checkGameFolder(const std::string& dir, bool* ready) {
    struct stat st;
    if (stat((dir + "/sys/fst.bin").c_str(), &st) != 0 || !S_ISREG(st.st_mode) || !isDir(dir + "/files")) {
        return false;
    }
    char magic[4] = {};
    FILE* f = fopen((dir + "/files/ObjectData/Coin.arc").c_str(), "rb");
    bool converted = f && fread(magic, 1, 4, f) == 4 && memcmp(magic, "CRAR", 4) == 0;
    if (f) fclose(f);

    bool dolData = true;
    for (const char* name : {"ErrorMessageArchive.arc", "StoryEvent.bcsv", "GalaxyID.bcsv"}) {
        dolData = dolData && stat((dir + "/sys/" + name).c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }
    bool known = port_dvd_identify(dir.c_str()) != nullptr;
    if (ready) *ready = converted && dolData && known;
    return true;
}

std::string findGameData(const std::string& extDir) {
    std::vector<std::string> candidates = {
        "/sdcard/GalaxyData",
        "/sdcard/GalaxyData/game",
        "/storage/emulated/0/GalaxyData",
        "/storage/emulated/0/GalaxyData/game",
        extDir + "/game",
        "/sdcard/Android/data/com.galaxy.mobile.plus/files/game",
        "/sdcard/Android/data/com.galaxy.mobile/files/game",
        "/sdcard/Android/data/com.galaxy.quest/files/game",
        "/sdcard/Galaxy/game",
        "/sdcard/GalaxyQuest",
        "/sdcard/GalaxyQuest/game",
        "/sdcard/Download/game",
        "/sdcard/Download/cooked",
        "/sdcard/Download/GalaxyQuest",
        "/sdcard/Documents/game",
        "/sdcard/Documents/cooked",
        "/sdcard/game",
        "/sdcard/cooked",
        "/data/local/tmp/petari/game"
    };

    if (const char* envRoot = getenv("PETARI_GAME_ROOT")) {
        candidates.insert(candidates.begin(), envRoot);
    }

    for (const auto& path : candidates) {
        bool ready = false;
        if (checkGameFolder(path, &ready) && ready) {
            LOGI("Found valid game data at: %s", path.c_str());
            return path;
        }
    }
    return "";
}

void promptStoragePermissionIfMissing(ANativeActivity* activity) {
    if (!activity || !activity->vm || !activity->clazz) return;
    JNIEnv* env = nullptr;
    if (activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;

    jclass versionClass = env->FindClass("android/os/Build$VERSION");
    if (!versionClass) return;
    jfieldID sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
    jint sdkInt = env->GetStaticIntField(versionClass, sdkIntField);

    if (sdkInt >= 30) {
        jclass envClass = env->FindClass("android/os/Environment");
        if (envClass) {
            jmethodID isManagerMethod = env->GetStaticMethodID(envClass, "isExternalStorageManager", "()Z");
            if (isManagerMethod) {
                jboolean isManager = env->CallStaticBooleanMethod(envClass, isManagerMethod);
                if (!isManager) {
                    LOGI("MANAGE_EXTERNAL_STORAGE permission missing, launching settings prompt...");
                    jclass intentClass = env->FindClass("android/content/Intent");
                    jmethodID intentCtor = env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V");

                    jclass uriClass = env->FindClass("android/net/Uri");
                    jmethodID parseMethod = env->GetStaticMethodID(uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");

                    jclass actClass = env->GetObjectClass(activity->clazz);
                    jmethodID getPackageNameMethod = env->GetMethodID(actClass, "getPackageName", "()Ljava/lang/String;");
                    jstring pkgName = (jstring)env->CallObjectMethod(activity->clazz, getPackageNameMethod);

                    const char* pkgChars = env->GetStringUTFChars(pkgName, nullptr);
                    std::string uriStr = std::string("package:") + (pkgChars ? pkgChars : "");
                    if (pkgChars) env->ReleaseStringUTFChars(pkgName, pkgChars);

                    jstring jUriStr = env->NewStringUTF(uriStr.c_str());
                    jobject jUri = env->CallStaticObjectMethod(uriClass, parseMethod, jUriStr);

                    jstring actionStr = env->NewStringUTF("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION");
                    jobject intent = env->NewObject(intentClass, intentCtor, actionStr, jUri);

                    jmethodID startActivityMethod = env->GetMethodID(actClass, "startActivity", "(Landroid/content/Intent;)V");
                    env->CallVoidMethod(activity->clazz, startActivityMethod, intent);

                    env->DeleteLocalRef(actionStr);
                    env->DeleteLocalRef(jUriStr);
                    env->DeleteLocalRef(jUri);
                    env->DeleteLocalRef(intent);
                }
            }
        }
    }
}

bool initEgl(MobileApp& a) {
    if (a.eglReady) return true;

    a.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (a.display == EGL_NO_DISPLAY || !eglInitialize(a.display, nullptr, nullptr)) {
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
    if (!eglChooseConfig(a.display, cfgAttr, &a.config, 1, &count) || count == 0) {
        LOGE("eglChooseConfig failed");
        return false;
    }

    const EGLint ctxAttr[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 2,
        EGL_NONE
    };

    a.context = eglCreateContext(a.display, a.config, EGL_NO_CONTEXT, ctxAttr);
    if (a.context == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed (0x%x)", eglGetError());
        return false;
    }

    a.eglReady = true;
    return true;
}

void updateViewport(MobileApp& a) {
    if (mobile_ui::get().settings().fullscreen) {
        a.vpX = 0;
        a.vpY = 0;
        a.vpW = a.screenWidth;
        a.vpH = a.screenHeight;
    } else {
        float targetAspect = 16.0f / 9.0f;
        float screenAspect = (float)a.screenWidth / (float)a.screenHeight;
        if (screenAspect > targetAspect) {
            a.vpH = a.screenHeight;
            a.vpW = (int)lroundf(a.screenHeight * targetAspect);
            a.vpX = (a.screenWidth - a.vpW) / 2;
            a.vpY = 0;
        } else {
            a.vpW = a.screenWidth;
            a.vpH = (int)lroundf(a.screenWidth / targetAspect);
            a.vpX = 0;
            a.vpY = (a.screenHeight - a.vpH) / 2;
        }
    }
}

bool createSurface(MobileApp& a) {
    if (!a.android->window || !a.eglReady) return false;

    EGLint format = 0;
    eglGetConfigAttrib(a.display, a.config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(a.android->window, 0, 0, format);

    a.surface = eglCreateWindowSurface(a.display, a.config, a.android->window, nullptr);
    if (a.surface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed (0x%x)", eglGetError());
        return false;
    }

    if (!eglMakeCurrent(a.display, a.surface, a.surface, a.context)) {
        LOGE("eglMakeCurrent failed (0x%x)", eglGetError());
        return false;
    }

    eglQuerySurface(a.display, a.surface, EGL_WIDTH, &a.screenWidth);
    eglQuerySurface(a.display, a.surface, EGL_HEIGHT, &a.screenHeight);
    eglSwapInterval(a.display, 1);

    LOGI("Window surface ready: %dx%d (GL: %s)", a.screenWidth, a.screenHeight, (const char*)glGetString(GL_RENDERER));

    updateViewport(a);
    LOGI("Viewport: x=%d, y=%d, w=%d, h=%d (fullscreen=%d)", a.vpX, a.vpY, a.vpW, a.vpH, mobile_ui::get().settings().fullscreen ? 1 : 0);

    // Initialize renderer and render target
    gpu::Renderer& r = gpu::renderer();
    if (!r.init()) {
        LOGE("gpu::renderer().init() failed");
        return false;
    }

    // Allocate offscreen target (aspect-matched when in fullscreen mode)
    int targetH = 1080;
    if (a.screenHeight > 1080) {
        targetH = std::min(a.screenHeight, 1440);
    }
    int targetW;
    if (mobile_ui::get().settings().fullscreen) {
        float nativeAspect = (float)a.screenWidth / (float)a.screenHeight;
        targetW = (int)lroundf(targetH * nativeAspect);
    } else {
        targetW = (targetH * 16) / 9;
    }
    if (!a.targetReady || a.efbTarget.width != targetW || a.efbTarget.height != targetH) {
        if (a.targetReady) {
            r.destroyTarget(a.efbTarget);
        }
        a.efbTarget = r.createTarget(targetW, targetH);
        a.targetReady = true;
        LOGI("Created offscreen EfbTarget: %dx%d (fullscreen=%d)", targetW, targetH, mobile_ui::get().settings().fullscreen ? 1 : 0);
    }

    a.windowReady = true;
    return true;
}

void destroySurface(MobileApp& a) {
    if (a.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(a.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (a.surface != EGL_NO_SURFACE) {
            eglDestroySurface(a.display, a.surface);
            a.surface = EGL_NO_SURFACE;
        }
    }
    a.windowReady = false;
}

int32_t onInput(android_app* app, AInputEvent* event) {
    MobileApp* a = (MobileApp*)app->userData;
    if (!a) return 0;

    int32_t eventType = AInputEvent_getType(event);
    if (eventType == AINPUT_EVENT_TYPE_MOTION) {
        int32_t source = AInputEvent_getSource(event);
        bool isJoystickSource = (source & AINPUT_SOURCE_CLASS_JOYSTICK) != 0 || (source & AINPUT_SOURCE_GAMEPAD) != 0;

        // Physical gamepad analog stick mapping only
        if (isJoystickSource) {
            float stickX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
            float stickY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            if (fabsf(stickX) > 0.15f || fabsf(stickY) > 0.15f) {
                a->pad.stickX = stickX;
                a->pad.stickY = -stickY;
                port_input_set(0, &a->pad);
                return 1;
            }
        }

        updateViewport(*a);
        int ptrX = a->vpX, ptrY = a->vpY, ptrW = a->vpW, ptrH = a->vpH;
        if (mobile_ui::get().settings().fullscreen) {
            ptrW = (int)lroundf((float)a->screenHeight * (16.0f / 9.0f));
            ptrX = (a->screenWidth - ptrW) / 2;
        }
        if (mobile_ui::get().handleInput(event, a->pad, a->spinFrames,
                                         a->screenWidth, a->screenHeight,
                                         ptrX, ptrY, ptrW, ptrH)) {
            port_input_set(0, &a->pad);
            return 1;
        }
        return 0;
    } else if (eventType == AINPUT_EVENT_TYPE_KEY) {
        int32_t keyCode = AKeyEvent_getKeyCode(event);
        // Do not intercept system volume keys: let Android OS adjust system volume normally
        if (keyCode == AKEYCODE_VOLUME_UP || keyCode == AKEYCODE_VOLUME_DOWN || keyCode == AKEYCODE_VOLUME_MUTE) {
            return 0;
        }

        int32_t keyAction = AKeyEvent_getAction(event);
        bool down = (keyAction == AKEY_EVENT_ACTION_DOWN);

        switch (keyCode) {
        case AKEYCODE_BUTTON_A:
        case AKEYCODE_DPAD_CENTER:
            a->pad.connected = 1;
            if (down) a->pad.buttons |= 0x0800;  // A
            else a->pad.buttons &= ~0x0800;
            break;
        case AKEYCODE_BUTTON_B:
            a->pad.connected = 1;
            if (down) a->pad.buttons |= 0x0400;  // B
            else a->pad.buttons &= ~0x0400;
            break;
        case AKEYCODE_BUTTON_X:
            a->pad.connected = 1;
            if (down) a->spinFrames = 6;  // Spin (Wiimote shake)
            break;
        case AKEYCODE_BUTTON_Y:
            a->pad.connected = 1;
            if (down) a->pad.buttons |= 0x1000;  // C
            else a->pad.buttons &= ~0x1000;
            break;
        case AKEYCODE_BUTTON_L1:
        case AKEYCODE_BUTTON_L2:
        case AKEYCODE_BUTTON_THUMBL:
            a->pad.connected = 1;
            if (down) a->pad.buttons |= 0x2000;  // Z (crouch / ground pound)
            else a->pad.buttons &= ~0x2000;
            break;
        case AKEYCODE_BUTTON_R1:
        case AKEYCODE_BUTTON_R2:
        case AKEYCODE_BUTTON_THUMBR:
            a->pad.connected = 1;
            if (down) a->spinFrames = 6;  // Spin (Wiimote shake)
            break;
        case AKEYCODE_BUTTON_START:
        case AKEYCODE_MENU:
            a->pad.connected = 1;
            if (down) a->pad.buttons |= 0x0010;  // Plus / Pause
            else a->pad.buttons &= ~0x0010;
            break;
        case AKEYCODE_DPAD_UP:
            a->pad.connected = 1;
            if (down) a->pad.buttons |= 0x0008;
            else a->pad.buttons &= ~0x0008;
            break;
        case AKEYCODE_DPAD_DOWN:
            a->pad.connected = 1;
            if (down) a->pad.buttons |= 0x0004;
            else a->pad.buttons &= ~0x0004;
            break;
        case AKEYCODE_DPAD_LEFT:
            a->pad.connected = 1;
            if (down) a->pad.buttons |= 0x0001;
            else a->pad.buttons &= ~0x0001;
            break;
        case AKEYCODE_DPAD_RIGHT:
            a->pad.connected = 1;
            if (down) a->pad.buttons |= 0x0002;
            else a->pad.buttons &= ~0x0002;
            break;
        case AKEYCODE_BACK:
            if (mobile_ui::get().isSettingsOpen()) {
                if (down) {
                    mobile_ui::get().closeSettings();
                }
                return 1;
            }
            return 0;
        default:
            return 0;
        }

        port_input_set(0, &a->pad);
        return 1;
    }

    return 0;
}

void onAppCmd(android_app* app, int32_t cmd) {
    MobileApp* a = (MobileApp*)app->userData;
    if (!a) return;

    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        LOGI("APP_CMD_INIT_WINDOW");
        initEgl(*a);
        createSurface(*a);
        break;
    case APP_CMD_TERM_WINDOW:
        LOGI("APP_CMD_TERM_WINDOW");
        destroySurface(*a);
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        LOGI("APP_CMD_WINDOW_RESIZED / CONFIG_CHANGED");
        if (a->surface != EGL_NO_SURFACE) {
            destroySurface(*a);
            createSurface(*a);
        }
        break;
    case APP_CMD_RESUME:
        LOGI("APP_CMD_RESUME");
        port_set_paused(0);
        break;
    case APP_CMD_PAUSE:
        LOGI("APP_CMD_PAUSE");
        port_set_paused(1);
        break;
    case APP_CMD_DESTROY:
        LOGI("APP_CMD_DESTROY");
        destroySurface(*a);
        break;
    default:
        break;
    }
}

}  // namespace

extern "C" __attribute__((visibility("default"))) void port_android_main(struct android_app* app, uintptr_t windowBase, size_t windowSize) {
    LOGI("==================================================");
    LOGI("GalaxyMobile: Native flat-screen mobile initialized");
    LOGI("==================================================");

    MobileApp& a = gMobile;
    a.android = app;
    app->userData = &a;
    app->onAppCmd = onAppCmd;
    app->onInputEvent = onInput;

    port_mem_set_reserved_window(windowBase, windowSize);

    a.pad.connected = 1;
    port_input_set(0, &a.pad);

    std::string internalPath = app->activity->internalDataPath ? app->activity->internalDataPath : "";
    bool isPlus = (internalPath.find("plus") != std::string::npos);
    mobile_ui::get().setPlusEdition(isPlus);
    LOGI("GalaxyMobile edition detected: %s (internalDataPath: %s)", isPlus ? "GalaxyMobile+" : "GalaxyMobile (Standard)", internalPath.c_str());

    std::string ext = app->activity->externalDataPath ? app->activity->externalDataPath : (isPlus ? "/sdcard/Android/data/com.galaxy.mobile.plus/files" : "/sdcard/Android/data/com.galaxy.mobile/files");
    a.saveRoot = internalPath + "/nand";
    mkdir(a.saveRoot.c_str(), 0770);
    mobile_ui::get().setSavePath(a.saveRoot + (isPlus ? "/touch_settings_plus.ini" : "/touch_settings.ini"));

    port_log_file((ext + "/petari_mobile_log.txt").c_str());
    gpu::setShaderCachePath((std::string(app->activity->internalDataPath) + "/shaders.bin").c_str());

    // Search for game data files
    a.dataRoot = findGameData(ext);
    if (a.dataRoot.empty()) {
        promptStoragePermissionIfMissing(app->activity);
    }

    bool running = true;
    while (running && !app->destroyRequested) {
        int events;
        android_poll_source* source = nullptr;
        int timeout = (a.windowReady) ? 0 : 50;

        while (ALooper_pollOnce(timeout, nullptr, &events, (void**)&source) >= 0) {
            if (source) {
                source->process(app, source);
            }
            if (app->destroyRequested) {
                running = false;
                break;
            }
            timeout = 0;
        }

        if (!a.windowReady) {
            continue;
        }

        // Boot game once window and data are available
        if (!a.gameBooted) {
            if (!a.dataRoot.empty()) {
                LOGI("Booting game from: %s", a.dataRoot.c_str());
                a.gameBooted = true;
                port_boot(a.dataRoot.c_str(), a.saveRoot.c_str());
            } else {
                static int64_t lastLog = 0;
                int64_t now = port_host_time_ns();
                if (now - lastLog > 3000000000ll) {  // Every 3 seconds
                    lastLog = now;
                    LOGI("Waiting for game files in /sdcard/GalaxyData or %s/game", ext.c_str());
                    // Re-scan in case player pushed files
                    a.dataRoot = findGameData(ext);
                }
            }
        }

        // Handle spin shake decay and feed input state
        if (a.spinFrames > 0) {
            float s = (a.spinFrames & 1) ? 2.5f : -2.5f;
            a.pad.accX = s;
            a.pad.accY = s;
            a.spinFrames--;
        } else {
            a.pad.accX = 0.0f;
            a.pad.accY = 0.0f;
        }
        port_input_set(0, &a.pad);


        // Presentation & Render Loop
        gpu::Renderer& r = gpu::renderer();
        bool newFrame = r.update();

        if (newFrame) {
            gPresentedFrames.store(r.frameNumber());

            // Check if offscreen target dimensions need to change (e.g. toggled fullscreen)
            int desiredTargetH = 1080;
            if (a.screenHeight > 1080) {
                desiredTargetH = std::min(a.screenHeight, 1440);
            }
            int desiredTargetW;
            if (mobile_ui::get().settings().fullscreen) {
                float nativeAspect = (float)a.screenWidth / (float)a.screenHeight;
                desiredTargetW = (int)lroundf(desiredTargetH * nativeAspect);
            } else {
                desiredTargetW = (desiredTargetH * 16) / 9;
            }
            if (a.targetReady && (a.efbTarget.width != desiredTargetW || a.efbTarget.height != desiredTargetH)) {
                r.destroyTarget(a.efbTarget);
                a.efbTarget = r.createTarget(desiredTargetW, desiredTargetH);
                updateViewport(a);
                LOGI("Resized offscreen EfbTarget: %dx%d (fullscreen=%d)", desiredTargetW, desiredTargetH, mobile_ui::get().settings().fullscreen ? 1 : 0);
            }

            // Monoscopic rendering into offscreen target
            r.render(a.efbTarget, nullptr, nullptr, gpu::HudMode::Inline);

            // Clean GL states before clearing backbuffer and blitting
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, a.screenWidth, a.screenHeight);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

            // Clear black pillarbox borders
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            // Blit game image to viewport (pillarbox or fullscreen)
            updateViewport(a);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, a.efbTarget.fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitFramebuffer(0, 0, a.efbTarget.width, a.efbTarget.height,
                              a.vpX, a.vpY, a.vpX + a.vpW, a.vpY + a.vpH,
                              GL_COLOR_BUFFER_BIT, GL_LINEAR);

            // Render on-screen controls HUD & Settings overlay
            mobile_ui::get().render(a.screenWidth, a.screenHeight);

            eglSwapBuffers(a.display, a.surface);
        } else {
            if (mobile_ui::get().needsRedraw()) {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, a.screenWidth, a.screenHeight);
                glDisable(GL_SCISSOR_TEST);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_CULL_FACE);
                glDisable(GL_BLEND);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                updateViewport(a);
                if (a.targetReady && r.hasFrame()) {
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, a.efbTarget.fbo);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                    glBlitFramebuffer(0, 0, a.efbTarget.width, a.efbTarget.height,
                                      a.vpX, a.vpY, a.vpX + a.vpW, a.vpY + a.vpH,
                                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
                }

                mobile_ui::get().render(a.screenWidth, a.screenHeight);
                eglSwapBuffers(a.display, a.surface);
            } else if (!r.hasFrame()) {
                // Idle screen while waiting for the game to boot or submit frames
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, a.screenWidth, a.screenHeight);
                glClearColor(0.04f, 0.05f, 0.10f, 1.0f);  // Galaxy deep blue
                glClear(GL_COLOR_BUFFER_BIT);
                mobile_ui::get().render(a.screenWidth, a.screenHeight);
                eglSwapBuffers(a.display, a.surface);
                usleep(16000);
            } else {
                usleep(1000);
            }
        }
    }

    LOGI("GalaxyMobile exiting cleanly");
    destroySurface(a);
    exit(0);
}

extern "C" {
float port_get_display_aspect(void) {
    if (gMobile.screenHeight > 0) {
        return (float)gMobile.screenWidth / (float)gMobile.screenHeight;
    }
    return 16.0f / 9.0f;
}

int port_is_fullscreen_aspect(void) {
    return mobile_ui::UIOverlay::get().settings().fullscreen ? 1 : 0;
}

int port_is_fly_mode(void) {
    if (!mobile_ui::UIOverlay::get().isPlusEdition()) return 0;
    return mobile_ui::UIOverlay::get().settings().modFly ? 1 : 0;
}

int port_is_skip_cutscenes(void) {
    if (!mobile_ui::UIOverlay::get().isPlusEdition()) return 0;
    return mobile_ui::UIOverlay::get().settings().modSkipCutscenes ? 1 : 0;
}

int port_is_infinite_health(void) {
    if (!mobile_ui::UIOverlay::get().isPlusEdition()) return 0;
    return mobile_ui::UIOverlay::get().settings().modInfiniteHealth ? 1 : 0;
}
}

