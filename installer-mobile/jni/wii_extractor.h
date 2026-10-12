#pragma once

#include <string>
#include <functional>
#include <cstdint>

namespace wii {

using LogCallback = std::function<void(const std::string&)>;
using ProgressCallback = std::function<void(int percent, const std::string& message)>;

// Check if a file is a valid Wii ISO or WBFS disc image
bool isWiiDiscImage(const std::string& filePath);

// Extract the Wii data partition from an ISO or WBFS file
// Output structure:
//   <outDir>/sys/main.dol
//   <outDir>/sys/fst.bin
//   <outDir>/files/...
bool extractWiiDisc(
    const std::string& imagePath,
    const std::string& outDir,
    bool includeMovies,
    LogCallback logCb,
    ProgressCallback progressCb
);

} // namespace wii
