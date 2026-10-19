#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "gl_shadergen.h"
#include "gpu.h"
#include "gx_record.h"
#include "port/port.h"

namespace gpu {

struct ShaderGenOut {
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

extern const char* kUniformBlocksVs;
extern const char* kUniformBlocksFs;

extern const char* kColorIn[16];
extern const char* kAlphaIn[8];
extern const char* kDest[4];
extern const int kFrac[8];
extern const char kComp[4];

std::string glKonstColor(uint32_t sel);
std::string glKonstAlpha(uint32_t sel);
std::string glTevRegular(const char* comp, uint32_t bias, uint32_t sub, uint32_t scale, bool alpha);
std::string glSwapString(const ShaderUid& u, uint32_t table);
const char* glAlphaTest(uint32_t func, const char* ref);

}  // namespace gpu
