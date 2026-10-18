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

#include "gx/vk_renderer.h"

#define TAG "PetariVK"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

extern "C" int port_is_post_processing_enabled(void);
extern "C" int port_is_fullscreen_aspect(void);

namespace gpu {

const char* vkDebugValue(const char* envName, const char* propertyName);

extern uint64_t sVkDeviceGeneration;

PipelineKey pipelineKeyFor(const Item& it, const ShaderUid& uid);

}  // namespace gpu
