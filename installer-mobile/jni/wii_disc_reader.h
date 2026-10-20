#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <memory>

namespace wii {

class DiscImageReader {
public:
    virtual ~DiscImageReader() = default;
    virtual bool read(uint64_t virtualOffset, size_t len, uint8_t* out) = 0;
    virtual uint64_t size() const = 0;
};

std::unique_ptr<DiscImageReader> openDiscImage(const std::string& path);
bool isWiiDiscImage(const std::string& filePath);

} // namespace wii
