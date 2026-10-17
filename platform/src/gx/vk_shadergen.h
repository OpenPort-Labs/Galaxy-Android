#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "gl_shadergen.h"

namespace gpu {

// Generates Vulkan 1.0 GLSL (version 450) with standard Vulkan descriptor bindings (SSBOs, UBO, samplers)
std::string genVulkanVertexShader(const ShaderUid& uid);
std::string genVulkanFragmentShader(const ShaderUid& uid);

// Compiles Vulkan GLSL source to SPIR-V bytecode targeting Vulkan 1.0 using shaderc
bool compileShaderToSPIRV(const std::string& src, bool isVertex, std::vector<uint32_t>& outSpirv, std::string& outError);

// High-level: generates and compiles both vertex and fragment shaders for a ShaderUid
bool compileVulkanShader(const ShaderUid& uid, std::vector<uint32_t>& outVsSpirv, std::vector<uint32_t>& outFsSpirv);

// The already compiled SPIR-V for *uid*, if the cache has it, without running
// shaderc.  A render thread takes this path on every program it has not built
// yet; only a miss is worth handing to a worker thread.
bool lookupVulkanShader(const ShaderUid& uid, std::vector<uint32_t>& outVsSpirv, std::vector<uint32_t>& outFsSpirv);

// Where the compiled SPIR-V is reused from (see vk_shadergen.cpp).  Empty path
// means every program is compiled from source.
void setVulkanShaderCachePath(const char* path);

}  // namespace gpu
