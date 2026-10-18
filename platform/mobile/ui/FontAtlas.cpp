#include "FontAtlas.h"

namespace mobile_ui {

namespace {

static int base64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

const ui_font::Face& FontAtlas::getFace() {
    return ui_font::kFaces[0];
}

void FontAtlas::getAtlas(int& width, int& height, std::vector<uint8_t>& coverage) {
    const ui_font::Face& face = getFace();
    width = face.width;
    height = face.height;

    std::vector<uint8_t> packed;
    uint32_t bits = 0;
    int nbits = 0;
    for (const char* p = face.atlas; *p; p++) {
        int v = base64Val(*p);
        if (v < 0) continue;
        bits = (bits << 6) | (uint32_t)v;
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            packed.push_back((uint8_t)(bits >> nbits));
        }
    }

    coverage.assign((size_t)face.width * face.height, 0);
    for (size_t i = 0; i < coverage.size(); i++) {
        uint8_t b = (i / 2 < packed.size()) ? packed[i / 2] : 0;
        coverage[i] = (uint8_t)(((i & 1) ? (b & 15) : (b >> 4)) * 17);
    }
}

const ui_font::Glyph* FontAtlas::findGlyph(uint32_t cp) {
    const ui_font::Face& face = getFace();
    for (int i = 0; i < face.count; i++) {
        if (face.glyphs[i].code == cp) {
            return &face.glyphs[i];
        }
    }
    // Fallback to '?'
    for (int i = 0; i < face.count; i++) {
        if (face.glyphs[i].code == '?') {
            return &face.glyphs[i];
        }
    }
    return face.count > 0 ? &face.glyphs[0] : nullptr;
}


}  // namespace mobile_ui
