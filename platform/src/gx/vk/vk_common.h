#pragma once

#include <android/log.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#include "port/port.h"
#include "gx/vk_renderer.h"

#define TAG "PetariVK"
#ifndef LOGI
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#endif
#ifndef LOGE
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#endif
#ifndef LOGW
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#endif

extern "C" int port_is_post_processing_enabled(void);
extern "C" int port_is_fullscreen_aspect(void);

namespace gpu {

const char* vkDebugValue(const char* envName, const char* propertyName);

extern uint64_t sVkDeviceGeneration;

PipelineKey pipelineKeyFor(const Item& it, const ShaderUid& uid);

}  // namespace gpu
