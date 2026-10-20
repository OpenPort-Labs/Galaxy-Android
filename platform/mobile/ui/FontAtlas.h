#pragma once

#include <stdint.h>
#include <vector>
#include "ui_font.h"

namespace mobile_ui {

class FontAtlas {
public:
    // Retrieves font atlas dimensions and 8-bit alpha coverage data
    static void getAtlas(int& width, int& height, std::vector<uint8_t>& coverage);

    // Returns reference to the primary font face
    static const ui_font::Face& getFace();

    // Finds a glyph by unicode codepoint, or fallback question mark
    static const ui_font::Glyph* findGlyph(uint32_t cp);
};

}  // namespace mobile_ui
