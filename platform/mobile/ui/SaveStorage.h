#pragma once

#include <stddef.h>
#include <string>

namespace mobile_ui {
namespace savefs {

bool removeDirRecursive(const std::string& path);
bool copyFileVerified(const std::string& src, const std::string& dst);
bool copyDirVerified(const std::string& src, const std::string& dst);
bool validateSaveDir(const std::string& dir);
// Temp file + fsync + rename, so a power loss leaves either the old file or
// the complete new one, never a truncated mixture.
bool writeFileAtomic(const std::string& path, const void* data, size_t size);

}  // namespace savefs
}  // namespace mobile_ui
