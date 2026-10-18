#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace mobile_ui {

class DolphinSaveConverter {
public:
    static bool isDolphinSave(const uint8_t* data, size_t size);
    static bool isNativeSave(const uint8_t* data, size_t size);
    static bool convertDolphinToNative(const uint8_t* inData, size_t inSize, std::vector<uint8_t>& outData);
    static bool convertNativeToDolphin(const uint8_t* inData, size_t inSize, std::vector<uint8_t>& outData);
    static uint32_t calcChecksum(const uint8_t* data, size_t size, bool bigEndian);

private:
    static bool processSave(const uint8_t* inData, size_t inSize, std::vector<uint8_t>& outData, bool fromDolphin);
};

}  // namespace mobile_ui
