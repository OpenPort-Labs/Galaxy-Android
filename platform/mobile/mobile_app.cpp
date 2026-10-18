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
#include <sys/system_properties.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include "gx/gl_renderer.h"
#include "gx/vk_renderer.h"
#include "gx/vk_shadergen.h"
#include "mobile_ui.h"
#include "port/input.h"
#include "port/port.h"
#include <android/sensor.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "PetariVR", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "PetariVR", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PetariVR", __VA_ARGS__)

extern "C" void port_mem_set_reserved_window(uintptr_t base, size_t size);
extern "C" void port_boot(const char* dataRoot, const char* saveRoot);
extern "C" void port_audio_pause(void);
extern "C" void port_audio_resume(void);
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

    gpu::VkTarget vkTarget{};
    bool vkReady = false;

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
    bool gamepadDetected = false;
    // Digital buttons and analog-trigger/hat buttons are tracked separately: a
    // pad streams axis updates continuously, and clearing the shared mask there
    // used to drop a button a key event had just pressed (and vice versa).
    uint32_t physicalButtons = 0;
    uint32_t physicalAxisButtons = 0;
    bool touchscreenDown = false;
    float physicalStickX = 0.0f;
    float physicalStickY = 0.0f;
};

MobileApp gMobile;

static ASensorManager* sSensorManager = nullptr;
static const ASensor* sGyroSensor = nullptr;
static const ASensor* sAccelSensor = nullptr;
static ASensorEventQueue* sSensorQueue = nullptr;
static bool sSensorsActive = false;

static void updateSensorsState(bool enable) {
    if (!sSensorQueue) return;
    if (enable && !sSensorsActive) {
        if (sGyroSensor) {
            ASensorEventQueue_enableSensor(sSensorQueue, sGyroSensor);
            ASensorEventQueue_setEventRate(sSensorQueue, sGyroSensor, 16666);
            LOGI("Sensors: Activated gyroscope");
        }
        if (sAccelSensor) {
            ASensorEventQueue_enableSensor(sSensorQueue, sAccelSensor);
            ASensorEventQueue_setEventRate(sSensorQueue, sAccelSensor, 16666);
            LOGI("Sensors: Activated accelerometer");
        }
        sSensorsActive = true;
    } else if (!enable && sSensorsActive) {
        if (sGyroSensor) {
            ASensorEventQueue_disableSensor(sSensorQueue, sGyroSensor);
        }
        if (sAccelSensor) {
            ASensorEventQueue_disableSensor(sSensorQueue, sAccelSensor);
        }
        sSensorsActive = false;
        LOGI("Sensors: Deactivated sensors");
    }
}

static void initSensors(android_app* app) {
    if (!sSensorManager) {
        sSensorManager = ASensorManager_getInstanceForPackage(nullptr);
        if (!sSensorManager) {
            sSensorManager = ASensorManager_getInstance();
        }
    }
    if (sSensorManager && !sSensorQueue) {
        sGyroSensor = ASensorManager_getDefaultSensor(sSensorManager, ASENSOR_TYPE_GYROSCOPE);
        sAccelSensor = ASensorManager_getDefaultSensor(sSensorManager, ASENSOR_TYPE_ACCELEROMETER);
        sSensorQueue = ASensorManager_createEventQueue(sSensorManager, app->looper, 3, nullptr, nullptr);
        sSensorsActive = false;
    }
}

static void pollSensors(MobileApp& a) {
    if (!sSensorQueue) return;
    if (!sSensorsActive) {
        updateSensorsState(true);
    }

    ASensorEvent event;
    while (ASensorEventQueue_getEvents(sSensorQueue, &event, 1) > 0) {
        if (!sSensorsActive) continue;
        if (event.type == ASENSOR_TYPE_GYROSCOPE) {
            if (mobile_ui::UIOverlay::get().settings().gyroAiming) {
                // Android Landscape coordinate conversion:
                float rotH = event.vector.x - event.vector.z * 0.35f;
                float rotV = event.vector.y;

                const auto& settings = mobile_ui::UIOverlay::get().settings();
                if (settings.gyroInvertX) rotH = -rotH;
                if (settings.gyroInvertY) rotV = -rotV;

                float sens = 0.035f;
                a.pad.pointerX += rotH * sens;
                a.pad.pointerY -= rotV * sens;
                a.pad.pointerX = std::max(-1.0f, std::min(1.0f, a.pad.pointerX));
                a.pad.pointerY = std::max(-1.0f, std::min(1.0f, a.pad.pointerY));
                a.pad.pointerValid = 1;
                mobile_ui::UIOverlay::get().setPointerPos(a.pad.pointerX, a.pad.pointerY);
                port_input_set(0, &a.pad);
            }
        } else if (event.type == ASENSOR_TYPE_ACCELEROMETER) {
            // Android Landscape coordinate conversion for motion steering (Ray Surfing / Mantis, Star Ball)
            // - Tilting phone like a steering wheel (Roll): pulls along the phone's Y axis (long axis)
            // - Tilting phone forward/backward (Pitch): pulls along the phone's X axis (short axis)
            float rawRoll, rawPitch;
            if (mobile_ui::get().settings().screenOrientationChoice == 2) {
                // Reverse landscape (USB port to the left)
                rawRoll = (event.acceleration.y / 9.81f) * 1.4f;
                rawPitch = ((-event.acceleration.x - 3.5f) / 9.81f) * 1.4f;
            } else {
                // Standard landscape (USB port to the right)
                rawRoll = (-event.acceleration.y / 9.81f) * 1.4f;
                rawPitch = ((event.acceleration.x - 3.5f) / 9.81f) * 1.4f;
            }
            rawRoll = std::max(-1.0f, std::min(1.0f, rawRoll));
            rawPitch = std::max(-1.0f, std::min(1.0f, rawPitch));

            // Apply physical tilt if virtual touch stick is not being actively dragged
            if (!mobile_ui::get().isStickActive()) {
                if (fabsf(rawRoll) > 0.05f || fabsf(rawPitch) > 0.05f) {
                    a.pad.tilted = 1;
                    a.pad.downX = rawRoll;
                    a.pad.downZ = rawPitch;
                    float rem = 1.0f - (a.pad.downX * a.pad.downX + a.pad.downZ * a.pad.downZ);
                    a.pad.downY = -(rem > 0.0f ? sqrtf(rem) : 0.0f);
                } else {
                    a.pad.tilted = 0;
                }
            } else {
                a.pad.tilted = 0;
            }

            if (mobile_ui::UIOverlay::get().settings().gyroAiming && !sGyroSensor) {
                float tiltX = -event.acceleration.y * 0.15f;
                float tiltY = event.acceleration.x * 0.15f;
                const auto& settings = mobile_ui::UIOverlay::get().settings();
                if (settings.gyroInvertX) tiltX = -tiltX;
                if (settings.gyroInvertY) tiltY = -tiltY;
                a.pad.pointerX = std::max(-1.0f, std::min(1.0f, tiltX));
                a.pad.pointerY = std::max(-1.0f, std::min(1.0f, tiltY));
                a.pad.pointerValid = 1;
                mobile_ui::UIOverlay::get().setPointerPos(a.pad.pointerX, a.pad.pointerY);
            }
            port_input_set(0, &a.pad);
        }
    }
}

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
        extDir + "/GalaxyData",
        extDir,
        "/sdcard/Android/data/com.openport.galaxyandroid/files/game",
        "/sdcard/Android/data/com.openport.galaxyandroid/files/GalaxyData",
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

    // Discover external MicroSD card mount points (/storage/XXXX-XXXX)
    DIR* storageDir = opendir("/storage");
    if (storageDir) {
        struct dirent* entry;
        while ((entry = readdir(storageDir)) != nullptr) {
            if (entry->d_name[0] != '.' && strcmp(entry->d_name, "emulated") != 0 && strcmp(entry->d_name, "self") != 0) {
                std::string sd = std::string("/storage/") + entry->d_name;
                candidates.push_back(sd + "/GalaxyData");
                candidates.push_back(sd + "/GalaxyData/game");
                candidates.push_back(sd + "/Android/data/com.openport.galaxyandroid/files/GalaxyData");
                candidates.push_back(sd + "/Android/data/com.openport.galaxyandroid/files/game");
                candidates.push_back(sd + "/game");
                candidates.push_back(sd + "/cooked");
            }
        }
        closedir(storageDir);
    }

    DIR* mediaDir = opendir("/mnt/media_rw");
    if (mediaDir) {
        struct dirent* entry;
        while ((entry = readdir(mediaDir)) != nullptr) {
            if (entry->d_name[0] != '.') {
                std::string sd = std::string("/mnt/media_rw/") + entry->d_name;
                candidates.push_back(sd + "/GalaxyData");
                candidates.push_back(sd + "/GalaxyData/game");
                candidates.push_back(sd + "/Android/data/com.openport.galaxyandroid/files/GalaxyData");
                candidates.push_back(sd + "/Android/data/com.openport.galaxyandroid/files/game");
                candidates.push_back(sd + "/game");
            }
        }
        closedir(mediaDir);
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

    if (env->ExceptionCheck()) env->ExceptionClear();

    jclass versionClass = env->FindClass("android/os/Build$VERSION");
    if (!versionClass) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }
    jfieldID sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
    if (!sdkIntField) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(versionClass);
        return;
    }
    jint sdkInt = env->GetStaticIntField(versionClass, sdkIntField);
    env->DeleteLocalRef(versionClass);

    if (sdkInt >= 30) {
        jclass envClass = env->FindClass("android/os/Environment");
        if (!envClass) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            return;
        }
        jmethodID isManagerMethod = env->GetStaticMethodID(envClass, "isExternalStorageManager", "()Z");
        if (!isManagerMethod) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(envClass);
            return;
        }
        jboolean isManager = env->CallStaticBooleanMethod(envClass, isManagerMethod);
        env->DeleteLocalRef(envClass);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return;
        }

        if (!isManager) {
            LOGI("MANAGE_EXTERNAL_STORAGE permission missing, launching settings prompt...");
            jclass intentClass = env->FindClass("android/content/Intent");
            if (!intentClass) {
                if (env->ExceptionCheck()) env->ExceptionClear();
                return;
            }
            jmethodID intentCtor = env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V");

            jclass uriClass = env->FindClass("android/net/Uri");
            if (!uriClass || !intentCtor) {
                if (env->ExceptionCheck()) env->ExceptionClear();
                if (intentClass) env->DeleteLocalRef(intentClass);
                if (uriClass) env->DeleteLocalRef(uriClass);
                return;
            }
            jmethodID parseMethod = env->GetStaticMethodID(uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");

            jclass actClass = env->GetObjectClass(activity->clazz);
            if (!actClass || !parseMethod) {
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(intentClass);
                env->DeleteLocalRef(uriClass);
                if (actClass) env->DeleteLocalRef(actClass);
                return;
            }
            jmethodID getPackageNameMethod = env->GetMethodID(actClass, "getPackageName", "()Ljava/lang/String;");
            jmethodID startActivityMethod = env->GetMethodID(actClass, "startActivity", "(Landroid/content/Intent;)V");

            if (getPackageNameMethod && startActivityMethod) {
                jstring pkgName = (jstring)env->CallObjectMethod(activity->clazz, getPackageNameMethod);
                if (pkgName) {
                    const char* pkgChars = env->GetStringUTFChars(pkgName, nullptr);
                    std::string uriStr = std::string("package:") + (pkgChars ? pkgChars : "");
                    if (pkgChars) env->ReleaseStringUTFChars(pkgName, pkgChars);

                    jstring jUriStr = env->NewStringUTF(uriStr.c_str());
                    jobject jUri = env->CallStaticObjectMethod(uriClass, parseMethod, jUriStr);

                    jstring actionStr = env->NewStringUTF("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION");
                    jobject intent = env->NewObject(intentClass, intentCtor, actionStr, jUri);

                    if (intent) {
                        env->CallVoidMethod(activity->clazz, startActivityMethod, intent);
                        env->DeleteLocalRef(intent);
                    }
                    if (actionStr) env->DeleteLocalRef(actionStr);
                    if (jUri) env->DeleteLocalRef(jUri);
                    if (jUriStr) env->DeleteLocalRef(jUriStr);
                    env->DeleteLocalRef(pkgName);
                }
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(actClass);
            env->DeleteLocalRef(uriClass);
            env->DeleteLocalRef(intentClass);
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

static void enforceLandscapeOrientation(MobileApp& a) {
    if (a.android && a.android->activity) {
        int orientationChoice = mobile_ui::UIOverlay::get().settings().screenOrientationChoice;
        // 0 = SCREEN_ORIENTATION_SENSOR_LANDSCAPE (6, auto-flips 180 deg)
        // 1 = SCREEN_ORIENTATION_LANDSCAPE (0, natural 90 deg)
        // 2 = SCREEN_ORIENTATION_REVERSE_LANDSCAPE (8, reverse 270 deg)
        int requested = 6;
        if (orientationChoice == 1) requested = 0;
        else if (orientationChoice == 2) requested = 8;

        ANativeActivity* act = a.android->activity;
        JNIEnv* env = nullptr;
        act->vm->AttachCurrentThread(&env, nullptr);
        if (env) {
            jclass actClass = env->GetObjectClass(act->clazz);
            if (actClass) {
                jmethodID setOrient = env->GetMethodID(actClass, "setRequestedOrientation", "(I)V");
                if (setOrient) {
                    env->CallVoidMethod(act->clazz, setOrient, requested);
                }
                env->DeleteLocalRef(actClass);
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
            act->vm->DetachCurrentThread();
        }
    }
}

void updateViewport(MobileApp& a) {
    if (mobile_ui::get().settings().fullscreen) {
        a.vpX = 0;
        a.vpY = 0;
        a.vpW = a.screenWidth;
        a.vpH = a.screenHeight;
    } else {
        float targetAspect = 16.0f / 9.0f;
        float screenAspect = (a.screenWidth >= a.screenHeight)
            ? ((float)a.screenWidth / (float)a.screenHeight)
            : ((float)a.screenHeight / (float)a.screenWidth);
        if (a.screenWidth >= a.screenHeight) {
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
    enforceLandscapeOrientation(a);
    eglSwapInterval(a.display, 1);

    LOGI("Window surface ready: %dx%d (GL: %s)", a.screenWidth, a.screenHeight, (const char*)glGetString(GL_RENDERER));
    // OpenGL ES is what is running here: either the user asked for it or the
    // Vulkan device/driver turned out to be unusable.
    mobile_ui::get().setActiveBackend(0);

    updateViewport(a);
    LOGI("Viewport: x=%d, y=%d, w=%d, h=%d (fullscreen=%d)", a.vpX, a.vpY, a.vpW, a.vpH, mobile_ui::get().settings().fullscreen ? 1 : 0);

    // Initialize renderer and render target
    gpu::Renderer& r = gpu::renderer();
    if (!r.init()) {
        LOGE("gpu::renderer().init() failed");
        return false;
    }

    // Allocate offscreen target (aspect-matched when in fullscreen mode)
    int scaleChoice = mobile_ui::get().settings().renderScale;
    int dispH = std::min(a.screenWidth, a.screenHeight);
    int dispW = std::max(a.screenWidth, a.screenHeight);
    int targetH = 1080;
    if (scaleChoice == 0) targetH = 480;       // Native 480p (blazing fast for low-end devices)
    else if (scaleChoice == 1) targetH = 720;  // 720p HD (great for Helio G81/G99)
    else if (scaleChoice == 2) targetH = 1080; // 1080p FHD
    else if (scaleChoice == 3) targetH = 1440; // 1440p 2K Ultra
    else if (dispH > 1080) {
        targetH = std::min(dispH, 1440);
    }
    int targetW;
    if (mobile_ui::get().settings().fullscreen) {
        float nativeAspect = (float)dispW / (float)dispH;
        targetW = (int)lroundf(targetH * nativeAspect);
    } else {
        targetW = (targetH * 16) / 9;
    }
    if (!a.targetReady || a.efbTarget.width != targetW || a.efbTarget.height != targetH) {
        gpu::EfbTarget newTarget = r.createTarget(targetW, targetH);
        if (newTarget.fbo == 0 && targetH > 720) {
            LOGW("Failed to allocate %dx%d target, falling back to 720p", targetW, targetH);
            targetH = 720;
            targetW = mobile_ui::get().settings().fullscreen ? (int)lroundf(720.0f * ((float)dispW / (float)dispH)) : (720 * 16) / 9;
            newTarget = r.createTarget(targetW, targetH);
        }
        if (newTarget.fbo != 0) {
            if (a.targetReady) {
                r.destroyTarget(a.efbTarget);
            }
            a.efbTarget = newTarget;
            a.targetReady = true;
            LOGI("Created offscreen EfbTarget: %dx%d (fullscreen=%d)", targetW, targetH, mobile_ui::get().settings().fullscreen ? 1 : 0);
        } else {
            LOGE("Critical: Failed to create offscreen EfbTarget (%dx%d)", targetW, targetH);
        }
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
        bool isJoystickSource = (source & AINPUT_SOURCE_CLASS_JOYSTICK) != 0 ||
                                ((source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD) ||
                                ((source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK);

        // Physical gamepad analog stick, D-pad, and trigger mapping
        if (isJoystickSource) {
            if (!a->gamepadDetected) {
                a->gamepadDetected = true;
                mobile_ui::get().showToast("Gamepad Connected");
            }

            // Left Stick (Analog Movement)
            float stickX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
            float stickY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            float stickMag = sqrtf(stickX * stickX + stickY * stickY);
            const float kDeadzone = 0.18f;
            if (stickMag <= kDeadzone) {
                stickX = 0.0f;
                stickY = 0.0f;
            } else {
                float norm = (stickMag - kDeadzone) / (1.0f - kDeadzone);
                stickX = (stickX / stickMag) * std::min(1.0f, norm);
                stickY = (stickY / stickMag) * std::min(1.0f, norm);
            }

            a->pad.connected = 1;
            a->physicalStickX = stickX;
            a->physicalStickY = -stickY; // Invert Y for Wii/GameCube convention
            if (mobile_ui::get().isStickActive()) {
                a->pad.stickX = mobile_ui::get().getTouchStickX();
                a->pad.stickY = mobile_ui::get().getTouchStickY();
            } else if (a->gamepadDetected && (fabsf(a->physicalStickX) > 0.001f || fabsf(a->physicalStickY) > 0.001f)) {
                a->pad.stickX = a->physicalStickX;
                a->pad.stickY = a->physicalStickY;
            } else {
                a->pad.stickX = 0.0f;
                a->pad.stickY = 0.0f;
            }

            // D-Pad Hat axes (Standard on Xbox, PlayStation, 8BitDo, and HID pads)
            float hatX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
            float hatY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
            if (hatX < -0.5f) { a->physicalAxisButtons |= 0x0001; a->physicalAxisButtons &= ~0x0002; }
            else if (hatX > 0.5f) { a->physicalAxisButtons |= 0x0002; a->physicalAxisButtons &= ~0x0001; }
            else { a->physicalAxisButtons &= ~0x0003; }

            if (hatY < -0.5f) { a->physicalAxisButtons |= 0x0008; a->physicalAxisButtons &= ~0x0004; }
            else if (hatY > 0.5f) { a->physicalAxisButtons |= 0x0004; a->physicalAxisButtons &= ~0x0008; }
            else { a->physicalAxisButtons &= ~0x000C; }

            // Analog Triggers (L2 / R2)
            float rawL = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0));
            float rawBrake = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_BRAKE, 0));
            float lTrig = std::max(rawL, rawBrake);

            float rawR = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0));
            float rawGas = std::max(0.0f, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_GAS, 0));
            float rTrig = std::max(rawR, rawGas);

            if (lTrig > 0.4f) a->physicalAxisButtons |= 0x2000; // Z button (Crouch / Ground Pound)
            else a->physicalAxisButtons &= ~0x2000;

            if (rTrig > 0.4f) a->spinFrames = 6; // Spin shake

            // Right Stick (Full 360-degree orbital camera & Star Pointer)
            float rightX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
            float rightY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
            if (fabsf(rightX) <= 0.05f && fabsf(rightY) <= 0.05f) {
                rightX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RX, 0);
                rightY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RY, 0);
            }
            float rStickX = (fabsf(rightX) > 0.12f) ? rightX : 0.0f;
            float rStickY = (fabsf(rightY) > 0.12f) ? -rightY : 0.0f;
            if (mobile_ui::get().settings().rightStickMode == 0) {
                // Mode 0: 360-degree Orbital Camera
                a->pad.rightStickX = rStickX;
                a->pad.rightStickY = rStickY;
            } else {
                // Mode 1: Star Pointer Stick
                a->pad.rightStickX = 0.0f;
                a->pad.rightStickY = 0.0f;
                if ((fabsf(rStickX) > 0.10f || fabsf(rStickY) > 0.10f) && !a->touchscreenDown) {
                    float speed = 0.040f;
                    a->pad.pointerX = std::max(-1.0f, std::min(1.0f, a->pad.pointerX + rStickX * speed));
                    a->pad.pointerY = std::max(-1.0f, std::min(1.0f, a->pad.pointerY + rStickY * speed));
                    a->pad.pointerValid = 1;
                    mobile_ui::get().setPointerPos(a->pad.pointerX, a->pad.pointerY);
                }
            }

            a->pad.buttons = a->physicalButtons | a->physicalAxisButtons | mobile_ui::get().getTouchButtons();
            port_input_set(0, &a->pad);
            return 1;
        }

        int32_t touchAction = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (touchAction == AMOTION_EVENT_ACTION_DOWN) a->touchscreenDown = true;
        if (touchAction == AMOTION_EVENT_ACTION_UP || touchAction == AMOTION_EVENT_ACTION_CANCEL) a->touchscreenDown = false;

        updateViewport(*a);
        int ptrX = a->vpX, ptrY = a->vpY, ptrW = a->vpW, ptrH = a->vpH;
        if (mobile_ui::get().settings().fullscreen) {
            ptrW = (int)lroundf((float)a->screenHeight * (16.0f / 9.0f));
            ptrX = (a->screenWidth - ptrW) / 2;
        }
        if (mobile_ui::get().handleInput(event, a->pad, a->spinFrames,
                                         a->screenWidth, a->screenHeight,
                                         ptrX, ptrY, ptrW, ptrH)) {
            if (mobile_ui::get().isStickActive()) {
                a->pad.stickX = mobile_ui::get().getTouchStickX();
                a->pad.stickY = mobile_ui::get().getTouchStickY();
            } else if (a->gamepadDetected && (fabsf(a->physicalStickX) > 0.001f || fabsf(a->physicalStickY) > 0.001f)) {
                a->pad.stickX = a->physicalStickX;
                a->pad.stickY = a->physicalStickY;
            } else {
                a->pad.stickX = 0.0f;
                a->pad.stickY = 0.0f;
            }
            a->pad.buttons = a->physicalButtons | a->physicalAxisButtons | mobile_ui::get().getTouchButtons();
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

        if (!a->gamepadDetected && (keyCode >= AKEYCODE_BUTTON_A && keyCode <= AKEYCODE_BUTTON_16)) {
            a->gamepadDetected = true;
            mobile_ui::get().showToast("Gamepad Connected");
        }

        int32_t keyAction = AKeyEvent_getAction(event);
        bool down = (keyAction == AKEY_EVENT_ACTION_DOWN);

        switch (keyCode) {
        case AKEYCODE_BUTTON_A:
        case AKEYCODE_BUTTON_1:
        case AKEYCODE_DPAD_CENTER:
        case AKEYCODE_ENTER:
        case AKEYCODE_NUMPAD_ENTER:
            a->pad.connected = 1;
            if (down) a->physicalButtons |= 0x0800;  // Switch A: Jump / Confirm / Interact
            else a->physicalButtons &= ~0x0800;
            break;
        case AKEYCODE_BUTTON_B:
        case AKEYCODE_BUTTON_2:
        case AKEYCODE_BUTTON_C:
            a->pad.connected = 1;
            if (down) {
                // Switch B: Jump (primary jump on Switch) and Star Bit Cancel/Shoot
                a->physicalButtons |= 0x0800; // Jump
                a->physicalButtons |= 0x0400; // Cancel/Shoot
            } else {
                a->physicalButtons &= ~0x0800;
                a->physicalButtons &= ~0x0400;
            }
            break;
        case AKEYCODE_BUTTON_X:
        case AKEYCODE_BUTTON_3:
            a->pad.connected = 1;
            if (down) a->spinFrames = 6;  // Switch X: Spin Attack!
            break;
        case AKEYCODE_BUTTON_Y:
        case AKEYCODE_BUTTON_4:
            a->pad.connected = 1;
            if (down) a->spinFrames = 6;  // Switch Y: Spin Attack! (Primary on Switch 3D All-Stars)
            break;
        case AKEYCODE_BUTTON_L1:
        case AKEYCODE_BUTTON_5:
            a->pad.connected = 1;
            if (down) a->physicalButtons |= 0x1000;  // Switch L: Camera snap behind Mario (C button)
            else a->physicalButtons &= ~0x1000;
            break;
        case AKEYCODE_BUTTON_R1:
        case AKEYCODE_BUTTON_6:
            a->pad.connected = 1;
            if (down) {
                // Switch R: Reset Star Pointer to screen center & camera snap
                a->pad.pointerX = 0.0f;
                a->pad.pointerY = 0.0f;
                a->pad.pointerValid = 1;
                mobile_ui::get().setPointerPos(0.0f, 0.0f);
                a->physicalButtons |= 0x1000;
            } else {
                a->physicalButtons &= ~0x1000;
            }
            break;
        case AKEYCODE_BUTTON_L2:
        case AKEYCODE_BUTTON_7:
        case AKEYCODE_BUTTON_Z:
            a->pad.connected = 1;
            if (down) a->physicalButtons |= 0x2000;  // Switch ZL: Crouch / Ground Pound / Backflip
            else a->physicalButtons &= ~0x2000;
            break;
        case AKEYCODE_BUTTON_R2:
        case AKEYCODE_BUTTON_8:
            a->pad.connected = 1;
            if (down) {
                // Switch ZR: Star Bit Shoot / Crouch
                a->physicalButtons |= 0x0400;
                a->physicalButtons |= 0x2000;
            } else {
                a->physicalButtons &= ~0x0400;
                a->physicalButtons &= ~0x2000;
            }
            break;
        case AKEYCODE_BUTTON_THUMBL:
        case AKEYCODE_BUTTON_11:
            a->pad.connected = 1;
            if (down) a->physicalButtons |= 0x2000;  // L3: Crouch
            else a->physicalButtons &= ~0x2000;
            break;
        case AKEYCODE_BUTTON_THUMBR:
        case AKEYCODE_BUTTON_12:
            a->pad.connected = 1;
            if (down) {
                // Switch R3: Recenter Star Pointer to center of screen
                a->pad.pointerX = 0.0f;
                a->pad.pointerY = 0.0f;
                a->pad.pointerValid = 1;
                mobile_ui::get().setPointerPos(0.0f, 0.0f);
            }
            break;
        case AKEYCODE_BUTTON_START:
        case AKEYCODE_BUTTON_10:
        case AKEYCODE_BUTTON_SELECT:
        case AKEYCODE_BUTTON_9:
        case AKEYCODE_BUTTON_MODE:
        case AKEYCODE_MENU:
            a->pad.connected = 1;
            if (down) a->physicalButtons |= 0x0010;  // Plus / Pause
            else a->physicalButtons &= ~0x0010;
            break;
        case AKEYCODE_DPAD_UP:
            a->pad.connected = 1;
            if (down) a->physicalButtons |= 0x0008;
            else a->physicalButtons &= ~0x0008;
            break;
        case AKEYCODE_DPAD_DOWN:
            a->pad.connected = 1;
            if (down) a->physicalButtons |= 0x0004;
            else a->physicalButtons &= ~0x0004;
            break;
        case AKEYCODE_DPAD_LEFT:
            a->pad.connected = 1;
            if (down) a->physicalButtons |= 0x0001;
            else a->physicalButtons &= ~0x0001;
            break;
        case AKEYCODE_DPAD_RIGHT:
            a->pad.connected = 1;
            if (down) a->physicalButtons |= 0x0002;
            else a->physicalButtons &= ~0x0002;
            break;
        case AKEYCODE_BACK:
        case AKEYCODE_ESCAPE:
            if (down) {
                if (mobile_ui::get().isSettingsOpen()) {
                    mobile_ui::get().closeSettings();
                } else {
                    a->physicalButtons |= 0x0010; // Pause (+)
                }
            } else {
                a->physicalButtons &= ~0x0010;
            }
            return 1;
        default:
            return 0;
        }

        if (mobile_ui::get().isStickActive()) {
            a->pad.stickX = mobile_ui::get().getTouchStickX();
            a->pad.stickY = mobile_ui::get().getTouchStickY();
        } else if (a->gamepadDetected && (fabsf(a->physicalStickX) > 0.001f || fabsf(a->physicalStickY) > 0.001f)) {
            a->pad.stickX = a->physicalStickX;
            a->pad.stickY = a->physicalStickY;
        } else {
            a->pad.stickX = 0.0f;
            a->pad.stickY = 0.0f;
        }
        a->pad.buttons = a->physicalButtons | a->physicalAxisButtons | mobile_ui::get().getTouchButtons();
        port_input_set(0, &a->pad);
        return 1;
    }

    return 0;
}

// Brings the Vulkan backend up on *window* and creates the offscreen target at
// the configured render scale.  Shared by startup and by the recovery that runs
// when a device is lost mid-session, so a recreated device goes through exactly
// the same setup as the first one.
static bool bringUpVulkan(MobileApp& a, ANativeWindow* window) {
    if (!window) return false;
    if (!gpu::VkRenderer::get().init(window)) return false;

    a.vkReady = true;
    mobile_ui::get().setActiveBackend(1);
    mobile_ui::get().setGpuRendererName(gpu::VkRenderer::get().deviceName());
    a.screenWidth = ANativeWindow_getWidth(window);
    a.screenHeight = ANativeWindow_getHeight(window);
    enforceLandscapeOrientation(a);
    a.windowReady = true;

    int scaleChoice = mobile_ui::get().settings().renderScale;
    int dispH = std::min(a.screenWidth, a.screenHeight);
    int dispW = std::max(a.screenWidth, a.screenHeight);
    int targetH = 1080;
    if (scaleChoice == 0) targetH = 480;
    else if (scaleChoice == 1) targetH = 720;
    else if (scaleChoice == 2) targetH = 1080;
    else if (scaleChoice == 3) targetH = 1440;
    else if (dispH > 1080) targetH = std::min(dispH, 1440);

    int targetW;
    if (mobile_ui::get().settings().fullscreen) {
        float nativeAspect = (float)dispW / (float)dispH;
        targetW = (int)lroundf(targetH * nativeAspect);
    } else {
        targetW = (targetH * 16) / 9;
    }

    if (a.vkTarget.framebuffer) {
        gpu::VkRenderer::get().destroyTarget(a.vkTarget);
    }
    a.vkTarget = gpu::VkRenderer::get().createTarget(targetW, targetH);
    a.targetReady = (a.vkTarget.framebuffer != VK_NULL_HANDLE);
    updateViewport(a);
    LOGI("Vulkan 1.0 native renderer initialized (%dx%d), offscreen target: %dx%d",
         a.screenWidth, a.screenHeight, targetW, targetH);
    return true;
}

void onAppCmd(android_app* app, int32_t cmd) {
    MobileApp* a = (MobileApp*)app->userData;
    if (!a) return;

    switch (cmd) {
    case APP_CMD_INIT_WINDOW: {
        bool useVulkan = (mobile_ui::get().settings().graphicsBackend == 1);
        if (useVulkan) {
            LOGI("Selected Vulkan 1.0 backend, initializing VkRenderer with native touch controls...");
            if (!bringUpVulkan(*a, app->window)) {
                LOGE("Vulkan 1.0 initialization failed; falling back to OpenGL ES 3.2");
                a->vkReady = false;
                initEgl(*a);
                createSurface(*a);
            }
        } else {
            initEgl(*a);
            createSurface(*a);
        }
        break;
    }
    case APP_CMD_TERM_WINDOW:
        LOGI("APP_CMD_TERM_WINDOW");
        if (a->vkReady) {
            if (a->vkTarget.framebuffer) {
                gpu::VkRenderer::get().destroyTarget(a->vkTarget);
            }
            gpu::VkRenderer::get().shutdown();
            a->vkReady = false;
            a->windowReady = false;
        } else {
            destroySurface(*a);
        }
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        LOGI("APP_CMD_WINDOW_RESIZED / CONFIG_CHANGED");
        if (a->vkReady) {
            gpu::VkRenderer::get().onWindowResized(app->window);
            a->screenWidth = ANativeWindow_getWidth(app->window);
            a->screenHeight = ANativeWindow_getHeight(app->window);
            updateViewport(*a);
        } else if (a->surface != EGL_NO_SURFACE) {
            destroySurface(*a);
            createSurface(*a);
        }
        break;
    case APP_CMD_RESUME:
        LOGI("APP_CMD_RESUME");
        port_audio_resume();
        port_set_paused(0);
        break;
    case APP_CMD_PAUSE:
        LOGI("APP_CMD_PAUSE");
        // Key-up events are lost while the activity is paused, so drop every held
        // button instead of resuming with a stuck pad.
        a->physicalButtons = 0;
        a->physicalAxisButtons = 0;
        a->spinFrames = 0;
        a->touchscreenDown = false;
        a->pad.buttons = mobile_ui::get().getTouchButtons();
        port_input_set(0, &a->pad);
        port_audio_pause();
        port_set_paused(1);
        break;
    case APP_CMD_DESTROY:
        LOGI("APP_CMD_DESTROY");
        if (a->vkReady) {
            if (a->vkTarget.framebuffer) {
                gpu::VkRenderer::get().destroyTarget(a->vkTarget);
            }
            gpu::VkRenderer::get().shutdown();
            a->vkReady = false;
            a->windowReady = false;
        } else {
            destroySurface(*a);
        }
        break;
    default:
        break;
    }
}

}  // namespace

extern "C" void port_android_vibrate(int ms) {
    if (ms <= 0) return;
    if (!gMobile.android || !gMobile.android->activity || !gMobile.android->activity->vm || !gMobile.android->activity->clazz) return;
    JavaVM* vm = gMobile.android->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    jint getEnvRes = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvRes == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) {
            return;
        }
        attached = true;
    } else if (getEnvRes != JNI_OK || !env) {
        return;
    }

    if (env->ExceptionCheck()) env->ExceptionClear();

    jclass clazz = env->GetObjectClass(gMobile.android->activity->clazz);
    if (clazz) {
        jmethodID mid = env->GetStaticMethodID(clazz, "triggerHaptic", "(I)V");
        if (mid) {
            env->CallStaticVoidMethod(clazz, mid, (jint)ms);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(clazz);
    }

    if (attached) vm->DetachCurrentThread();
}

extern "C" void port_android_exit_to_launcher() {
    if (!gMobile.android || !gMobile.android->activity || !gMobile.android->activity->vm || !gMobile.android->activity->clazz) return;
    JavaVM* vm = gMobile.android->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    jint getEnvRes = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvRes == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) {
            return;
        }
        attached = true;
    } else if (getEnvRes != JNI_OK || !env) {
        return;
    }

    if (env->ExceptionCheck()) env->ExceptionClear();

    jclass clazz = env->GetObjectClass(gMobile.android->activity->clazz);
    if (clazz) {
        jmethodID mid = env->GetStaticMethodID(clazz, "returnToLauncher", "()V");
        if (mid) {
            env->CallStaticVoidMethod(clazz, mid);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(clazz);
    }

    if (attached) vm->DetachCurrentThread();
}

static void updateWindowRefreshRate(float targetFps) {
    static float sLastAppliedFps = -1.0f;
    if (fabsf(sLastAppliedFps - targetFps) < 0.5f) return;
    sLastAppliedFps = targetFps;

    if (!gMobile.android || !gMobile.android->activity || !gMobile.android->activity->vm || !gMobile.android->activity->clazz) return;
    JavaVM* vm = gMobile.android->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    jint getEnvRes = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvRes == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
        attached = true;
    } else if (getEnvRes != JNI_OK || !env) {
        return;
    }

    if (env->ExceptionCheck()) env->ExceptionClear();
    jclass clazz = env->GetObjectClass(gMobile.android->activity->clazz);
    if (clazz) {
        jmethodID mid = env->GetStaticMethodID(clazz, "setPreferredRefreshRate", "(F)V");
        if (mid) {
            env->CallStaticVoidMethod(clazz, mid, (jfloat)targetFps);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(clazz);
    }
    if (attached) vm->DetachCurrentThread();
}

static int64_t sLastPresentNs = 0;

// Frame rate limiter and pacer.
// Enforces minimum frame spacing without burning CPU cycles in busy-wait loops.
static void syncMobileFrameRate(int targetFps, float speed) {
    if (targetFps < 30) targetFps = 30;
    if (targetFps > 144) targetFps = 144;
    static int sCurrentRefreshRate = 0;
    if (sCurrentRefreshRate != targetFps) {
        sCurrentRefreshRate = targetFps;
        updateWindowRefreshRate((float)targetFps);
        port_set_display_refresh_rate(targetFps);
    }

    if (speed > 1.05f) {
        targetFps = (int)((float)targetFps * speed);
    }
    int64_t framePeriodNs = 1000000000ll / targetFps;

    int64_t nowNs = port_host_time_ns();
    if (sLastPresentNs != 0) {
        int64_t elapsedNs = nowNs - sLastPresentNs;
        if (elapsedNs < framePeriodNs) {
            int64_t waitNs = framePeriodNs - elapsedNs;
            // Leave a 1.2 ms margin so Android kernel timer slack does not cause a missed VSync pulse
            if (waitNs > 1500000ll) {
                struct timespec req;
                int64_t sleepNs = waitNs - 1200000ll;
                req.tv_sec = sleepNs / 1000000000ll;
                req.tv_nsec = sleepNs % 1000000000ll;
                int64_t sleepStartNs = port_host_time_ns();
                nanosleep(&req, nullptr);
                port_perf_pace(port_host_time_ns() - sleepStartNs);
            }
        }
    }

    sLastPresentNs = port_host_time_ns();
}

static void crashSignalHandler(int sig, siginfo_t* info, void* ucontext) {
    (void)ucontext;
    const char* sigName = "UNKNOWN";
    if (sig == SIGSEGV) sigName = "SIGSEGV (Segmentation Fault)";
    else if (sig == SIGBUS) sigName = "SIGBUS (Bus Error / Alignment)";
    else if (sig == SIGFPE) sigName = "SIGFPE (Arithmetic Exception)";
    else if (sig == SIGILL) sigName = "SIGILL (Illegal Instruction)";
    else if (sig == SIGABRT) sigName = "SIGABRT (Abort)";

    LOGE("================ FATAL NATIVE CRASH ================");
    LOGE("Signal: %d (%s) at address %p", sig, sigName, info ? info->si_addr : nullptr);

    FILE* f = fopen("/sdcard/GalaxyData/galaxy_crash.log", "a");
    if (f) {
        time_t t = time(nullptr);
        char* ct = ctime(&t);
        if (ct) {
            char* nl = strchr(ct, '\n');
            if (nl) *nl = '\0';
        }
        fprintf(f, "[%s] FATAL SIGNAL %d (%s) at address %p\n", ct ? ct : "unknown time", sig, sigName, info ? info->si_addr : nullptr);
        fflush(f);
        fclose(f);
    }
    sync();

    signal(sig, SIG_DFL);
    raise(sig);
}

static void installCrashHandlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crashSignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
}

extern "C" __attribute__((visibility("default"))) void port_android_main(struct android_app* app, uintptr_t windowBase, size_t windowSize) {
    installCrashHandlers();
    LOGI("==================================================");
    LOGI("GalaxyMobile: Native flat-screen mobile initialized");
    LOGI("==================================================");

    MobileApp& a = gMobile;
    a.android = app;
    app->userData = &a;
    app->onAppCmd = onAppCmd;
    app->onInputEvent = onInput;
    initSensors(app);

    port_mem_set_reserved_window(windowBase, windowSize);

    a.pad.connected = 1;
    port_input_set(0, &a.pad);
    port_skip_set_hold_seconds(0.0f);

    std::string internalPath = app->activity->internalDataPath ? app->activity->internalDataPath : "";
    if (internalPath.empty()) {
        internalPath = "/data/data/com.openport.galaxyandroid/files";
    }
    bool isPlus = (internalPath.find("plus") != std::string::npos);
    mobile_ui::get().setPlusEdition(isPlus);
    LOGI("GalaxyMobile edition detected: %s (internalDataPath: %s)", isPlus ? "GalaxyMobile+" : "GalaxyMobile (Standard)", internalPath.c_str());

    std::string ext = app->activity->externalDataPath ? app->activity->externalDataPath : (isPlus ? "/sdcard/Android/data/com.galaxy.mobile.plus/files" : "/sdcard/Android/data/com.galaxy.mobile/files");
    a.saveRoot = internalPath + "/nand";
    mkdir(a.saveRoot.c_str(), 0770);
    mobile_ui::get().setSavePath(a.saveRoot + (isPlus ? "/touch_settings_plus.ini" : "/touch_settings.ini"));
    int initialFps = mobile_ui::get().settings().targetFps;
    if (mobile_ui::get().settings().batterySaver) initialFps = 30;
    updateWindowRefreshRate((float)initialFps);

    std::string primaryLog = "/sdcard/GalaxyData/galaxy_log.txt";
    FILE* tst = fopen(primaryLog.c_str(), "a");
    if (tst) {
        fclose(tst);
        port_log_file(primaryLog.c_str());
    } else {
        port_log_file((ext + "/galaxy_log.txt").c_str());
    }
    gpu::setShaderCachePath((internalPath + "/shaders.bin").c_str());
    gpu::setVkPipelineCachePath((internalPath + "/vk_pipeline_cache.bin").c_str());
    // Compiled SPIR-V, so shaderc only runs for a program once per install.
    gpu::setVulkanShaderCachePath((internalPath + "/vk_shaders.bin").c_str());

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
        int ident;
        while ((ident = ALooper_pollOnce(timeout, nullptr, &events, (void**)&source)) >= 0) {
            if (ident == 3) {
                pollSensors(a);
            }
            if (source) {
                source->process(app, source);
            }
            if (app->destroyRequested) {
                running = false;
                break;
            }
            timeout = 0;
        }

        if (!a.windowReady || port_is_paused()) {
            usleep(25000);
            continue;
        }

        pollSensors(a);

        // Boot game once window and data are available
        if (!a.gameBooted) {
            if (!a.dataRoot.empty()) {
                // Language selection: user manual preference first, then fallback to device locale
                int userLang = mobile_ui::UIOverlay::get().settings().languageChoice;
                if (userLang == 1) port_language_set("en");
                else if (userLang == 2) port_language_set("es");
                else if (userLang == 3) port_language_set("fr");
                else if (userLang == 4) port_language_set("de");
                else if (userLang == 5) port_language_set("it");
                else {
                    char sysLocale[PROP_VALUE_MAX] = {0};
                    if (__system_property_get("persist.sys.locale", sysLocale) > 0 ||
                        __system_property_get("ro.product.locale", sysLocale) > 0) {
                        char langCode[3] = { (char)tolower(sysLocale[0]), (char)tolower(sysLocale[1]), 0 };
                        port_language_set(langCode);
                        LOGI("Auto-detected device language: %s -> set language code: %s", sysLocale, langCode);
                    }
                }

                LOGI("Booting game from: %s", a.dataRoot.c_str());
                a.gameBooted = true;
                port_boot(a.dataRoot.c_str(), a.saveRoot.c_str());
            } else {
                static int64_t lastLog = 0;
                static int missingCount = 0;
                int64_t now = port_host_time_ns();
                if (now - lastLog > 2000000000ll) {  // Every 2 seconds
                    lastLog = now;
                    missingCount++;
                    LOGI("Waiting for game files in /sdcard/GalaxyData or %s/game (attempt %d)", ext.c_str(), missingCount);
                    // Re-scan in case player pushed files
                    a.dataRoot = findGameData(ext);
                    if (a.dataRoot.empty() && missingCount >= 2) {
                        LOGE("Game data missing or incomplete, redirecting to launcher activity");
                        port_android_exit_to_launcher();
                    }
                }
            }
        }

        // Update virtual pad pulse timers, auto-release, and hardware stats
        mobile_ui::get().update(a.pad, a.spinFrames);

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
        if (mobile_ui::get().isStickActive()) {
            a.pad.stickX = mobile_ui::get().getTouchStickX();
            a.pad.stickY = mobile_ui::get().getTouchStickY();
        } else if (a.gamepadDetected && (fabsf(a.physicalStickX) > 0.001f || fabsf(a.physicalStickY) > 0.001f)) {
            a.pad.stickX = a.physicalStickX;
            a.pad.stickY = a.physicalStickY;
        } else {
            a.pad.stickX = 0.0f;
            a.pad.stickY = 0.0f;
        }
        a.pad.buttons = a.physicalButtons | a.physicalAxisButtons | mobile_ui::get().getTouchButtons();
        port_input_set(0, &a.pad);

        // Direct hardware haptics integration: poll game engine rumble
        if (mobile_ui::get().settings().haptics) {
            static int sLastRumbleState = 0;
            int r = port_input_rumble(0);
            if (r && !sLastRumbleState) {
                port_android_vibrate(45);
            }
            sLastRumbleState = r;
        }


        // A Vulkan device can be lost mid-session (driver reset, GPU fault).
        // Vulkan expects the application to create a new device and carry on, so
        // that is what is tried first: dropping the rest of the session onto
        // OpenGL ES changes both the picture and the frame rate, and the player
        // notices.  Only a Vulkan that cannot be recreated falls back - or one
        // that is lost again straight away, since recreating it in a loop tears
        // the swapchain down over and over, which the player sees as the screen
        // flickering, and never reaches a playable state.
        if (a.vkReady && gpu::VkRenderer::get().hasFailed()) {
            static int64_t sLastRecreateNs = 0;
            static int sQuickRecreates = 0;
            int64_t nowNs = port_host_time_ns();
            sQuickRecreates = (sLastRecreateNs && nowNs - sLastRecreateNs < 15000000000ll) ? (sQuickRecreates + 1) : 0;
            sLastRecreateNs = nowNs;

            LOGW("Vulkan renderer failed at runtime; recreating the Vulkan device (attempt %d)", sQuickRecreates + 1);
            if (a.vkTarget.framebuffer) {
                gpu::VkRenderer::get().destroyTarget(a.vkTarget);
            }
            a.vkTarget = gpu::VkTarget{};
            a.targetReady = false;
            gpu::VkRenderer::get().shutdown();
            a.vkReady = false;
            // The shader and pipeline caches on disk mean the recreation below does
            // not pay for the programs again.
            if (sQuickRecreates < 2 && bringUpVulkan(a, a.android ? a.android->window : nullptr)) {
                LOGI("Vulkan device recreated; the session continues on Vulkan");
                continue;
            }
            LOGE("Vulkan was lost again immediately (or could not be recreated); falling back to OpenGL ES 3.2");
            sQuickRecreates = 0;
            if (initEgl(a) && createSurface(a)) {
                mobile_ui::get().setActiveBackend(0);
                LOGI("OpenGL ES 3.2 fallback renderer active");
            } else {
                LOGE("OpenGL ES fallback failed too; no rendering backend is available");
            }
            continue;
        }

        // Presentation & Render Loop
        if (a.vkReady) {
            gpu::VkRenderer& vkr = gpu::VkRenderer::get();
            int64_t updateStartNs = port_host_time_ns();
            bool newFrame = vkr.update();
            port_perf_update(port_host_time_ns() - updateStartNs);

            if (newFrame) {
                gPresentedFrames.store(vkr.frameNumber());

                int scaleChoice = mobile_ui::get().settings().renderScale;
                int desiredTargetH = 1080;
                if (scaleChoice == 0) desiredTargetH = 480;       // Native 480p
                else if (scaleChoice == 1) desiredTargetH = 720;  // 720p HD
                else if (scaleChoice == 2) desiredTargetH = 1080; // 1080p FHD
                else if (scaleChoice == 3) desiredTargetH = 1440; // 1440p 2K
                else if (a.screenHeight > 1080) {
                    desiredTargetH = std::min(a.screenHeight, 1440);
                }
                int desiredTargetW;
                if (mobile_ui::get().settings().fullscreen) {
                    float nativeAspect = (float)a.screenWidth / (float)a.screenHeight;
                    desiredTargetW = (int)lroundf(desiredTargetH * nativeAspect);
                } else {
                    desiredTargetW = (desiredTargetH * 16) / 9;
                }
                if (a.targetReady && (a.vkTarget.width != desiredTargetW || a.vkTarget.height != desiredTargetH)) {
                    vkr.destroyTarget(a.vkTarget);
                    a.vkTarget = vkr.createTarget(desiredTargetW, desiredTargetH);
                    a.targetReady = (a.vkTarget.framebuffer != VK_NULL_HANDLE);
                    updateViewport(a);
                    LOGI("Resized Vulkan target: %dx%d (fullscreen=%d)", desiredTargetW, desiredTargetH, mobile_ui::get().settings().fullscreen ? 1 : 0);
                }

                int64_t renderStartNs = port_host_time_ns();
                // Render into offscreen target
                vkr.render(a.vkTarget, nullptr, nullptr, gpu::HudMode::Inline);
                int64_t renderEndNs = port_host_time_ns();
                port_perf_render(renderEndNs - renderStartNs, 0);
                mobile_ui::get().reportGpuRenderTime(renderEndNs - renderStartNs);

                // Present to swapchain with pillarboxing
                updateViewport(a);
                int64_t presentStartNs = port_host_time_ns();
                vkr.present(a.vkTarget, a.vpX, a.vpY, a.vpW, a.vpH);
                port_perf_present(port_host_time_ns() - presentStartNs);
                mobile_ui::get().reportFramePresented();

                // Frame pacing: enforce minimum time spacing between presents so FPS NEVER exceeds targetFps
                int targetFps = mobile_ui::get().settings().targetFps;
                if (mobile_ui::get().settings().batterySaver) targetFps = 30;
                syncMobileFrameRate(targetFps, port_get_game_speed());
            } else {
                int targetFps = mobile_ui::get().settings().targetFps;
                if (mobile_ui::get().settings().batterySaver) targetFps = 30;
                float speed = port_get_game_speed();
                int effectiveFps = (speed > 1.05f) ? (int)((float)targetFps * speed) : targetFps;
                int64_t framePeriodNs = 1000000000ll / (effectiveFps > 0 ? effectiveFps : 60);
                int64_t nowNs = port_host_time_ns();

                bool highRefreshSlot = (targetFps > 60 && a.targetReady && vkr.hasFrame() && (nowNs - sLastPresentNs >= framePeriodNs));
                bool uiRedrawNeeded = (mobile_ui::get().needsRedraw() && a.targetReady && vkr.hasFrame() &&
                                       (!a.gameBooted || port_is_paused() || mobile_ui::get().isSettingsOpen()));

                if (highRefreshSlot || uiRedrawNeeded) {
                    syncMobileFrameRate(targetFps, 1.0f);
                    updateViewport(a);
                    vkr.present(a.vkTarget, a.vpX, a.vpY, a.vpW, a.vpH);
                    mobile_ui::get().reportFramePresented();
                } else {
                    usleep(1000);
                }
            }
        } else {
            // OpenGL ES fallback loop
            gpu::Renderer& r = gpu::renderer();
            bool newFrame = r.update();

            if (newFrame) {
                gPresentedFrames.store(r.frameNumber());

                // Check if offscreen target dimensions need to change (e.g. toggled fullscreen or resolution scale)
                int scaleChoice = mobile_ui::get().settings().renderScale;
                if (mobile_ui::get().settings().batterySaver) {
                    scaleChoice = std::min(scaleChoice, 1);
                }
                int desiredTargetH = 1080;
                if (scaleChoice == 0) desiredTargetH = 480;       // Native 480p (max performance)
                else if (scaleChoice == 1) desiredTargetH = 720;  // 720p HD (smooth performance on budget chips)
                else if (scaleChoice == 2) desiredTargetH = 1080; // 1080p Full HD
                else if (scaleChoice == 3) desiredTargetH = 1440; // 1440p 2K Ultra
                else if (a.screenHeight > 1080) {
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
                    gpu::EfbTarget newTarget = r.createTarget(desiredTargetW, desiredTargetH);
                    if (newTarget.fbo != 0) {
                        r.destroyTarget(a.efbTarget);
                        a.efbTarget = newTarget;
                        updateViewport(a);
                        LOGI("Resized offscreen EfbTarget: %dx%d (fullscreen=%d)", desiredTargetW, desiredTargetH, mobile_ui::get().settings().fullscreen ? 1 : 0);
                    } else {
                        LOGE("Failed to resize offscreen EfbTarget to %dx%d; preserving current %dx%d target",
                             desiredTargetW, desiredTargetH, a.efbTarget.width, a.efbTarget.height);
                    }
                }

                int64_t renderStartNs = port_host_time_ns();
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
                int64_t renderEndNs = port_host_time_ns();
                port_perf_render(renderEndNs - renderStartNs, 0);
                mobile_ui::get().reportGpuRenderTime(renderEndNs - renderStartNs);
                mobile_ui::get().reportFramePresented();

                // Frame pacing: enforce minimum time spacing between presents so FPS NEVER exceeds targetFps
                int targetFps = mobile_ui::get().settings().targetFps;
                if (mobile_ui::get().settings().batterySaver) targetFps = 30;
                syncMobileFrameRate(targetFps, port_get_game_speed());
            } else {
                int targetFps = mobile_ui::get().settings().targetFps;
                if (mobile_ui::get().settings().batterySaver) targetFps = 30;
                float speed = port_get_game_speed();
                int effectiveFps = (speed > 1.05f) ? (int)((float)targetFps * speed) : targetFps;
                int64_t framePeriodNs = 1000000000ll / (effectiveFps > 0 ? effectiveFps : 60);
                int64_t nowNs = port_host_time_ns();

                bool highRefreshSlot = (targetFps > 60 && a.targetReady && r.hasFrame() && (nowNs - sLastPresentNs >= framePeriodNs));
                bool uiRedrawNeeded = (mobile_ui::get().needsRedraw() &&
                                       (!a.gameBooted || port_is_paused() || mobile_ui::get().isSettingsOpen()));

                if (highRefreshSlot || uiRedrawNeeded) {
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

                    syncMobileFrameRate(targetFps, 1.0f);

                    mobile_ui::get().render(a.screenWidth, a.screenHeight);
                    eglSwapBuffers(a.display, a.surface);
                    mobile_ui::get().reportFramePresented();
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
}

    LOGI("GalaxyMobile exiting cleanly");
    destroySurface(a);
    _exit(0);
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
    return mobile_ui::UIOverlay::get().settings().modFly ? 1 : 0;
}

int port_is_skip_cutscenes(void) {
    return mobile_ui::UIOverlay::get().settings().modSkipCutscenes ? 1 : 0;
}

int port_is_infinite_health(void) {
    return mobile_ui::UIOverlay::get().settings().modInfiniteHealth ? 1 : 0;
}

int port_is_infinite_lives(void) {
    return mobile_ui::UIOverlay::get().settings().modInfiniteLives ? 1 : 0;
}

int port_is_infinite_starbits(void) {
    return mobile_ui::UIOverlay::get().settings().modInfiniteStarBits ? 1 : 0;
}

int port_is_shield_hd_textures(void) {
    return mobile_ui::UIOverlay::get().settings().modShieldTextures ? 1 : 0;
}

int port_is_shield_cutscenes(void) {
    return mobile_ui::UIOverlay::get().settings().modShieldCutscenes ? 1 : 0;
}

int port_is_shield_controls(void) {
    return mobile_ui::UIOverlay::get().settings().modShieldControls ? 1 : 0;
}

int port_is_classic_gamepad_ui(void) {
    return mobile_ui::UIOverlay::get().settings().modClassicGamepadUI ? 1 : 0;
}

int port_is_riivolution_mods(void) {
    return mobile_ui::UIOverlay::get().settings().modRiivolution ? 1 : 0;
}
}

