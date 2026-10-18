#include "VertexBatcher.h"
#include "FontAtlas.h"

#include <math.h>
#include <algorithm>

namespace mobile_ui {

VertexBatcher::VertexBatcher(std::vector<UIVertex>& verts)
    : mVerts(verts) {}

void VertexBatcher::addQuad(float x0, float y0, float x1, float y1,
                           float u0, float v0, float u1, float v1,
                           const Color& c, float mode, float param,
                           float aspect, float extra) {
    mVerts.push_back({x0, y0, u0, v0, c.r, c.g, c.b, c.a, mode, param, aspect, extra});
    mVerts.push_back({x1, y0, u1, v0, c.r, c.g, c.b, c.a, mode, param, aspect, extra});
    mVerts.push_back({x1, y1, u1, v1, c.r, c.g, c.b, c.a, mode, param, aspect, extra});

    mVerts.push_back({x0, y0, u0, v0, c.r, c.g, c.b, c.a, mode, param, aspect, extra});
    mVerts.push_back({x1, y1, u1, v1, c.r, c.g, c.b, c.a, mode, param, aspect, extra});
    mVerts.push_back({x0, y1, u0, v1, c.r, c.g, c.b, c.a, mode, param, aspect, extra});
}

void VertexBatcher::drawCircle(float cx, float cy, float r, const Color& c) {
    addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 1.0f, 0.0f);
}

void VertexBatcher::drawRing(float cx, float cy, float r, float innerRatio, const Color& c) {
    addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 1.0f, innerRatio);
}

void VertexBatcher::drawRoundRect(float x0, float y0, float x1, float y1, float radius, const Color& c) {
    float rw = x1 - x0;
    float rh = y1 - y0;
    if (rw <= 0.0f || rh <= 0.0f) return;
    if (rw >= rh) {
        float aspect = rw / rh;
        float rNorm = std::min(1.0f, radius / (rh * 0.5f));
        addQuad(x0, y0, x1, y1, -aspect, -1.0f, aspect, 1.0f, c, 0.0f, rNorm, aspect, 0.0f);
    } else {
        float aspect = rh / rw;
        float rNorm = std::min(1.0f, radius / (rw * 0.5f));
        addQuad(x0, y0, x1, y1, -1.0f, -aspect, 1.0f, aspect, c, 0.0f, rNorm, aspect, 1.0f);
    }
}

void VertexBatcher::drawCogwheel(float cx, float cy, float r, const Color& c, float rot) {
    addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 3.0f, rot);
}

void VertexBatcher::drawCloseIcon(float cx, float cy, float r, const Color& c) {
    addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 4.0f, 0.0f);
}

void VertexBatcher::drawCyclone(float cx, float cy, float r, const Color& c, float rot) {
    addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 5.0f, rot);
}

void VertexBatcher::drawChevron(float cx, float cy, float r, const Color& c, float rotAngle) {
    addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 6.0f, rotAngle);
}

void VertexBatcher::drawCameraIcon(float cx, float cy, float r, const Color& c) {
    addQuad(cx - r, cy - r, cx + r, cy + r, -1.0f, -1.0f, 1.0f, 1.0f, c, 7.0f, 0.0f);
}

void VertexBatcher::drawLine(float x0, float y0, float x1, float y1, float thickness, const Color& c) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float hThick = thickness * 0.5f;
    float nx = -dy / len * hThick;
    float ny = dx / len * hThick;

    mVerts.push_back({x0 + nx, y0 + ny, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
    mVerts.push_back({x1 + nx, y1 + ny, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
    mVerts.push_back({x1 - nx, y1 - ny, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});

    mVerts.push_back({x0 + nx, y0 + ny, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
    mVerts.push_back({x1 - nx, y1 - ny, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
    mVerts.push_back({x0 - nx, y0 - ny, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
}

void VertexBatcher::drawTriangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color& c) {
    mVerts.push_back({x0, y0, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
    mVerts.push_back({x1, y1, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
    mVerts.push_back({x2, y2, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f, 0.0f, 1.0f, 0.0f});
}

float VertexBatcher::measureText(const char* text, float sizeScale) const {
    const ui_font::Face& face = FontAtlas::getFace();
    float strW = 0.0f;
    for (const char* p = text; *p; p++) {
        char ch = *p;
        if (ch >= 32 && ch < 127) {
            strW += (face.glyphs[ch - 32].advance * 0.25f) * sizeScale;
        }
    }
    return strW;
}

void VertexBatcher::drawText(const char* text, float x, float baseline, float sizeScale, const Color& c, int align) {
    const ui_font::Face& face = FontAtlas::getFace();
    float strW = measureText(text, sizeScale);
    float curX = x;
    if (align == 1) curX -= strW * 0.5f;      // Center
    else if (align == 2) curX -= strW;        // Right

    for (const char* p = text; *p; p++) {
        char ch = *p;
        if (ch < 32 || ch >= 127) continue;
        const ui_font::Glyph& g = face.glyphs[ch - 32];
        float gx = curX + g.left * sizeScale;
        float gy = baseline - g.top * sizeScale;
        float gw = g.w * sizeScale;
        float gh = g.h * sizeScale;
        float u0 = (float)g.x / (float)face.width;
        float v0 = (float)g.y / (float)face.height;
        float u1 = (float)(g.x + g.w) / (float)face.width;
        float v1 = (float)(g.y + g.h) / (float)face.height;

        addQuad(gx, gy, gx + gw, gy + gh, u0, v0, u1, v1, c, 2.0f, 0.0f);
        curX += (g.advance * 0.25f) * sizeScale;
    }
}

void VertexBatcher::drawM3Switch(float x, float y, float swW, float swH, bool on) {
    float r = swH * 0.5f;
    if (on) {
        drawRoundRect(x, y, x + swW, y + swH, r, rgba(0.043f, 0.341f, 0.816f, 1.0f));
        float thumbR = r * 0.70f;
        drawCircle(x + swW - r, y + r, thumbR, rgba(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        drawRoundRect(x, y, x + swW, y + swH, r, rgba(0.28f, 0.31f, 0.36f, 0.65f));
        float thumbR = r * 0.55f;
        drawCircle(x + r, y + r, thumbR, rgba(0.70f, 0.74f, 0.80f, 1.0f));
    }
}

}  // namespace mobile_ui
