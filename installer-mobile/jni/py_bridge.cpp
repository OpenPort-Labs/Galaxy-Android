#include <jni.h>
#include <mutex>
#include <string>
#include <vector>
#include <android/log.h>
#include <unistd.h>
#include <Python.h>
#include "bridge_common.h"
#include "yaz0.h"

using namespace cooker_bridge;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    gVm = vm;
    return JNI_VERSION_1_6;
}

static void reportPythonException() {
    PyObject *ptype = nullptr, *pvalue = nullptr, *ptraceback = nullptr;
    PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    if (pvalue) {
        PyObject* pStr = PyObject_Str(pvalue);
        if (pStr) {
            const char* errText = PyUnicode_AsUTF8(pStr);
            if (errText) {
                sendLogToJava(("[Python Error] " + std::string(errText)).c_str());
            }
            Py_DECREF(pStr);
        }
    }
    Py_XDECREF(ptype);
    Py_XDECREF(pvalue);
    Py_XDECREF(ptraceback);
    PyErr_Clear();
}

static bool setPythonGlobals(const char* scriptsDir, const char* inputDir, const char* outputDir, bool withMovies) {
    PyObject* mainMod = PyImport_AddModule("__main__");
    if (!mainMod) return false;
    PyObject* globals = PyModule_GetDict(mainMod);
    if (!globals) return false;

    auto setStr = [&](const char* name, const char* value) -> bool {
        PyObject* v = PyUnicode_FromString(value ? value : "");
        if (!v) return false;
        int rc = PyDict_SetItemString(globals, name, v);
        Py_DECREF(v);
        return rc == 0;
    };

    return setStr("_port_scripts", scriptsDir) && setStr("_port_input", inputDir) &&
           setStr("_port_output", outputDir) &&
           PyDict_SetItemString(globals, "_port_with_movies", withMovies ? Py_True : Py_False) == 0;
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

static PyObject* py_progress_wrapper(PyObject* self, PyObject* args) {
    int percent = 0;
    const char* str = nullptr;
    if (PyArg_ParseTuple(args, "is", &percent, &str)) {
        if (str) {
            sendProgressToJava(percent, str);
        }
    }
    Py_RETURN_NONE;
}

static PyObject* py_yaz0_decompress(PyObject* self, PyObject* args) {
    Py_buffer view;
    if (!PyArg_ParseTuple(args, "y*", &view)) {
        return NULL;
    }
    const uint8_t* data = (const uint8_t*)view.buf;
    size_t len = (size_t)view.len;
    if (len < 16 || memcmp(data, "Yaz0", 4) != 0) {
        PyObject* ret = PyBytes_FromStringAndSize((const char*)data, len);
        PyBuffer_Release(&view);
        return ret;
    }
    uint32_t decompSize = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) | ((uint32_t)data[6] << 8) | (uint32_t)data[7];
    PyObject* outObj = PyBytes_FromStringAndSize(NULL, decompSize);
    if (!outObj) {
        PyBuffer_Release(&view);
        return PyErr_NoMemory();
    }
    char* dst = PyBytes_AS_STRING(outObj);
    long long n = yaz0_decode(data, len, (uint8_t*)dst, decompSize);
    PyBuffer_Release(&view);
    if (n != (long long)decompSize) {
        Py_DECREF(outObj);
        PyErr_SetString(PyExc_ValueError, "bad Yaz0 stream");
        return NULL;
    }
    return outObj;
}

static PyMethodDef BridgeMethods[] = {
    {"log_bridge", py_log_wrapper, METH_VARARGS, "Send logs to Android UI"},
    {"progress_bridge", py_progress_wrapper, METH_VARARGS, "Send progress to Android UI"},
    {"yaz0_decompress", py_yaz0_decompress, METH_VARARGS, "Decompress Yaz0 natively in C"},
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

    std::unique_lock<std::mutex> opLock(sOperationMutex, std::try_to_lock);
    if (!opLock.owns_lock()) {
        LOGE("runCooker: another installer operation is already running");
        return -2;
    }

    const char* pythonHome = env->GetStringUTFChars(jPythonHome, nullptr);
    const char* scriptsDir = env->GetStringUTFChars(jScriptsDir, nullptr);
    const char* inputDir = env->GetStringUTFChars(jInputDir, nullptr);
    const char* outputDir = env->GetStringUTFChars(jOutputDir, nullptr);
    if (!pythonHome || !scriptsDir || !inputDir || !outputDir) {
        LOGE("runCooker: a null path argument was passed in");
        return -1;
    }
    bool withMovies = (bool)jWithMovies;

    gCallbackGlobal = env->NewGlobalRef(jCallback);
    if (!bindCallbackMethods(env, jCallback)) {
        env->DeleteGlobalRef(gCallbackGlobal);
        gCallbackGlobal = nullptr;
        env->ReleaseStringUTFChars(jPythonHome, pythonHome);
        env->ReleaseStringUTFChars(jScriptsDir, scriptsDir);
        env->ReleaseStringUTFChars(jInputDir, inputDir);
        env->ReleaseStringUTFChars(jOutputDir, outputDir);
        return -1;
    }

    sendLogToJava("[Native] Initializing embedded Python engine...");

    PyImport_AppendInittab("android_bridge", PyInit_android_bridge);

    wchar_t* wHome = Py_DecodeLocale(pythonHome, nullptr);
    if (wHome) {
        Py_SetPythonHome(wHome);
    }

    Py_Initialize();

    sendLogToJava("[Native] Python engine initialized.");

    if (!setPythonGlobals(scriptsDir, inputDir, outputDir, withMovies)) {
        sendLogToJava("[Error] Could not pass the working paths to the embedded interpreter.");
        reportPythonException();
    }

    std::string setupScript =
        "import sys, os, android_bridge\n"
        "sys.path.insert(0, _port_scripts)\n"
        "class OutputLogger:\n"
        "    def write(self, s):\n"
        "        for line in s.splitlines():\n"
        "            if line.strip():\n"
        "                android_bridge.log_bridge(line.strip())\n"
        "    def flush(self):\n"
        "        pass\n"
        "sys.stdout = OutputLogger()\n"
        "sys.stderr = OutputLogger()\n";

    bool setupOk = PyRun_SimpleString(setupScript.c_str()) == 0;
    if (!setupOk) {
        sendLogToJava("[Error] Python setup failed; the conversion cannot run.");
        reportPythonException();
    }

    std::string runScript =
        "import cook, android_bridge, traceback, os\n"
        "def on_progress(done, total, *args):\n"
        "    pct = int(done * 100 / total) if total > 0 else 0\n"
        "    src = args[0] if args else ''\n"
        "    name = os.path.basename(src) if src else ''\n"
        "    msg = f'Cooking: {done}/{total} ({pct}%) - {name}' if name else f'Cooking assets: {done}/{total} ({pct}%)'\n"
        "    android_bridge.progress_bridge(pct, msg)\n"
        "try:\n"
        "    print('[Engine] Beginning conversion...')\n"
        "    ret = cook.convert(\n"
        "        data_dir=_port_input,\n"
        "        out_dir=_port_output,\n"
        "        with_movies=_port_with_movies,\n"
        "        progress=on_progress\n"
        "    )\n"
        "    if ret != 0:\n"
        "        raise RuntimeError(f'cook.convert returned non-zero code {ret}')\n"
        "    print('[Engine] Cooking completed successfully!')\n"
        "except BaseException as e:\n"
        "    import traceback\n"
        "    print('[ERROR] ' + str(e))\n"
        "    traceback.print_exc()\n"
        "    raise\n";

    sendLogToJava("[Native] Invoking cook.convert()...");
    int result = setupOk ? PyRun_SimpleString(runScript.c_str()) : -1;
    if (result != 0) {
        reportPythonException();
    }

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

extern "C" JNIEXPORT jint JNICALL
Java_com_openport_galaxyandroid_CookerEngine_runCooker(
        JNIEnv *env, jclass clazz,
        jstring jPythonHome, jstring jScriptsDir,
        jstring jInputDir, jstring jOutputDir,
        jboolean jWithMovies, jobject jCallback) {
    return Java_com_galaxy_installer_CookerEngine_runCooker(env, clazz, jPythonHome, jScriptsDir, jInputDir, jOutputDir, jWithMovies, jCallback);
}
