#pragma once
#include <stdint.h>

namespace ui_font {

struct Glyph {
    uint16_t code, x, y;
    uint8_t w, h;
    int8_t left, top;
    uint16_t advance;
};

struct Face {
    int size, ascent, descent, width, height;
    const Glyph* glyphs;
    int count;
    const char* atlas;
};

} // namespace ui_font

#include "ui_font_glyphs.h"

namespace ui_font {

static const char kAtlas40[] =
#include "ui_font_atlas40_a.inl"
#include "ui_font_atlas40_b.inl"
;

static const char kAtlas28[] =
#include "ui_font_atlas28.inl"
;

static const Face kFaces[] = {
    {40, 39, 10, 512, 169, kGlyphs40, 97, kAtlas40},
    {28, 28, 7, 512, 98, kGlyphs28, 97, kAtlas28},
};

}  // namespace ui_font
