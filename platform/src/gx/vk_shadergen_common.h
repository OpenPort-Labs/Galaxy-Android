#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "gx_shader_uid.h"
#include "gpu.h"
#include "gx_record.h"
#include "port/port.h"
#include "gx_state_blocks.h"
#include "vk_shadergen.h"

namespace gpu {

struct VulkanShaderGenOut {
    std::string s;
    void w(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        s += buf;
    }
};

extern const char* kVulkanUniformBlocksVs;
extern const char* kVulkanUniformBlocksFs;

extern const char* kVkColorIn[16];
extern const char* kVkAlphaIn[8];
extern const char* kVkDest[4];
extern const int kVkFrac[8];
extern const char kVkComp[4];

std::string vkKonstColor(uint32_t sel);
std::string vkKonstAlpha(uint32_t sel);
std::string vkTevRegular(const char* comp, uint32_t bias, uint32_t sub, uint32_t scale, bool alpha);
std::string vkSwapString(const ShaderUid& u, uint32_t table);
const char* vkAlphaTest(uint32_t func, const char* ref);

}  // namespace gpu
