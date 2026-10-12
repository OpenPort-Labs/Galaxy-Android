#include <jni.h>
#include <string>
#include <vector>
#include <android/log.h>
#include <unistd.h>
#include <Python.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "CookerBridge", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "CookerBridge", __VA_ARGS__)

static JavaVM* gVm = nullptr;
static jobject gCallbackGlobal = nullptr;
static jmethodID gOnLogMethod = nullptr;
static jmethodID gOnProgressMethod = nullptr;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    gVm = vm;
    return JNI_VERSION_1_6;
}

static void sendLogToJava(const char* text) {
    if (!gVm || !gCallbackGlobal || !gOnLogMethod) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (gVm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (gVm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
        }
    }
    if (env) {
        jstring jText = env->NewStringUTF(text);
        env->CallVoidMethod(gCallbackGlobal, gOnLogMethod, jText);
        env->DeleteLocalRef(jText);
        if (attached) {
            gVm->DetachCurrentThread();
        }
    }
}

static PyObject* py_log_wrapper(PyObject* self, PyObject* args) {
    const char* str = nullptr;
    if (PyArg_ParseTuple(args, "s", &str)) {
        if (str && strlen(str) > 0) {
            sendLogToJava(str);
        }
    }
    Py_RETURN_NONE;
}

static PyMethodDef BridgeMethods[] = {
    {"log_bridge", py_log_wrapper, METH_VARARGS, "Send logs to Android UI"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef bridge_module = {
    PyModuleDef_HEAD_INIT,
    "android_bridge",
    NULL,
    -1,
    BridgeMethods
};

PyMODINIT_FUNC PyInit_android_bridge(void) {
    return PyModule_Create(&bridge_module);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_galaxy_installer_CookerEngine_runCooker(
        JNIEnv *env, jclass clazz,
        jstring jPythonHome, jstring jScriptsDir,
        jstring jInputDir, jstring jOutputDir,
        jboolean jWithMovies, jobject jCallback) {

    const char* pythonHome = env->GetStringUTFChars(jPythonHome, nullptr);
    const char* scriptsDir = env->GetStringUTFChars(jScriptsDir, nullptr);
    const char* inputDir = env->GetStringUTFChars(jInputDir, nullptr);
    const char* outputDir = env->GetStringUTFChars(jOutputDir, nullptr);
    bool withMovies = (bool)jWithMovies;

    gCallbackGlobal = env->NewGlobalRef(jCallback);
    jclass cbClass = env->GetObjectClass(jCallback);
    gOnLogMethod = env->GetMethodID(cbClass, "onLog", "(Ljava/lang/String;)V");
    gOnProgressMethod = env->GetMethodID(cbClass, "onProgress", "(ILjava/lang/String;)V");

    sendLogToJava("[Native] Initializing embedded Python engine...");

    // Register our C module before Py_Initialize
    PyImport_AppendInittab("android_bridge", PyInit_android_bridge);

    wchar_t* wHome = Py_DecodeLocale(pythonHome, nullptr);
    if (wHome) {
        Py_SetPythonHome(wHome);
    }

    Py_Initialize();

    sendLogToJava("[Native] Python engine initialized.");

    // Setup sys.path and log redirection in Python
    std::string setupScript =
        "import sys, os, android_bridge\n"
        "sys.path.insert(0, '" + std::string(scriptsDir) + "')\n"
        "class OutputLogger:\n"
        "    def write(self, s):\n"
        "        for line in s.splitlines():\n"
        "            if line.strip():\n"
        "                android_bridge.log_bridge(line.strip())\n"
        "    def flush(self):\n"
        "        pass\n"
        "sys.stdout = OutputLogger()\n"
        "sys.stderr = OutputLogger()\n";

    PyRun_SimpleString(setupScript.c_str());

    // Execute cook.py
    std::string runScript =
        "import cook\n"
        "args = ['cook.py', '" + std::string(inputDir) + "', '" + std::string(outputDir) + "']\n";
    if (withMovies) {
        runScript += "args.append('--with-movies')\n";
    }
    runScript +=
        "try:\n"
        "    print('[Engine] Beginning conversion...')\n"
        "    cook.main(args)\n"
        "    print('[Engine] Cooking completed successfully!')\n"
        "except Exception as e:\n"
        "    import traceback\n"
        "    print('[ERROR] ' + str(e))\n"
        "    traceback.print_exc()\n"
        "    raise\n";

    sendLogToJava("[Native] Invoking cook.main()...");
    int result = PyRun_SimpleString(runScript.c_str());

    Py_Finalize();
    if (wHome) {
        PyMem_RawFree(wHome);
    }

    env->ReleaseStringUTFChars(jPythonHome, pythonHome);
    env->ReleaseStringUTFChars(jScriptsDir, scriptsDir);
    env->ReleaseStringUTFChars(jInputDir, inputDir);
    env->ReleaseStringUTFChars(jOutputDir, outputDir);
    env->DeleteGlobalRef(gCallbackGlobal);
    gCallbackGlobal = nullptr;

    return result;
}
