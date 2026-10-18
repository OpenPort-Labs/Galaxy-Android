#pragma once

#include "UICommon.h"
#include <vector>

namespace mobile_ui {

struct Color {
    float r, g, b, a;
};

inline Color rgba(float r, float g, float b, float a) {
    return {r, g, b, a};
}

inline Color hexColor(uint32_t hex, float a) {
    float r = ((hex >> 16) & 0xFF) / 255.0f;
    float g = ((hex >> 8) & 0xFF) / 255.0f;
    float b = (hex & 0xFF) / 255.0f;
    return {r, g, b, a};
}

class VertexBatcher {
public:
    explicit VertexBatcher(std::vector<UIVertex>& verts);

    void addQuad(float x0, float y0, float x1, float y1,
                 float u0, float v0, float u1, float v1,
                 const Color& c, float mode, float param,
                 float aspect = 1.0f, float extra = 0.0f);

    void drawCircle(float cx, float cy, float r, const Color& c);
    void drawRing(float cx, float cy, float r, float innerRatio, const Color& c);
    void drawRoundRect(float x0, float y0, float x1, float y1, float radius, const Color& c);
    void drawCogwheel(float cx, float cy, float r, const Color& c, float rot = 0.0f);
    void drawCloseIcon(float cx, float cy, float r, const Color& c);
    void drawCyclone(float cx, float cy, float r, const Color& c, float rot = 0.0f);
    void drawChevron(float cx, float cy, float r, const Color& c, float rotAngle = 0.0f);
    void drawCameraIcon(float cx, float cy, float r, const Color& c);
    void drawLine(float x0, float y0, float x1, float y1, float thickness, const Color& c);
    void drawTriangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color& c);

    float measureText(const char* text, float sizeScale) const;
    void drawText(const char* text, float x, float baseline, float sizeScale, const Color& c, int align = 0);
    void drawM3Switch(float x, float y, float swW, float swH, bool on);

private:
    std::vector<UIVertex>& mVerts;
};

}  // namespace mobile_ui
