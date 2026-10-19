#include <math.h>
#include <string.h>
#include "revolution/mtx.h"

// ---------------------------------------------------------------------------
// 4x4 matrices
// ---------------------------------------------------------------------------
void C_MTXFrustum(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
    f32 tmp = 1.0f / (r - l);
    m[0][0] = (2 * n) * tmp;
    m[0][1] = 0.0f;
    m[0][2] = (r + l) * tmp;
    m[0][3] = 0.0f;
    tmp = 1.0f / (t - b);
    m[1][0] = 0.0f;
    m[1][1] = (2 * n) * tmp;
    m[1][2] = (t + b) * tmp;
    m[1][3] = 0.0f;
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    tmp = 1.0f / (f - n);
    m[2][2] = -(n)*tmp;
    m[2][3] = -(f * n) * tmp;
    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = -1.0f;
    m[3][3] = 0.0f;
}

void C_MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f) {
    f32 angle = MTXDegToRad(fovY * 0.5f);
    f32 cot = 1.0f / tanf(angle);
    m[0][0] = cot / aspect;
    m[0][1] = 0.0f;
    m[0][2] = 0.0f;
    m[0][3] = 0.0f;
    m[1][0] = 0.0f;
    m[1][1] = cot;
    m[1][2] = 0.0f;
    m[1][3] = 0.0f;
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    f32 tmp = 1.0f / (f - n);
    m[2][2] = -(n)*tmp;
    m[2][3] = -(f * n) * tmp;
    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = -1.0f;
    m[3][3] = 0.0f;
}

void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
    f32 tmp = 1.0f / (r - l);
    m[0][0] = 2.0f * tmp;
    m[0][1] = 0.0f;
    m[0][2] = 0.0f;
    m[0][3] = -(r + l) * tmp;
    tmp = 1.0f / (t - b);
    m[1][0] = 0.0f;
    m[1][1] = 2.0f * tmp;
    m[1][2] = 0.0f;
    m[1][3] = -(t + b) * tmp;
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    tmp = 1.0f / (f - n);
    m[2][2] = -(1.0f) * tmp;
    m[2][3] = -(f)*tmp;
    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;
}

void C_MTX44Identity(Mtx44 m) {
    memset(m, 0, sizeof(Mtx44));
    m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
}
void PSMTX44Identity(Mtx44 m) { C_MTX44Identity(m); }

void C_MTX44Copy(const Mtx44 src, Mtx44 dst) {
    if (src != (const float(*)[4])dst) {
        memmove(dst, src, sizeof(Mtx44));
    }
}
void PSMTX44Copy(const Mtx44 src, Mtx44 dst) { C_MTX44Copy(src, dst); }

void C_MTX44Concat(const Mtx44 a, const Mtx44 b, Mtx44 ab) {
    Mtx44 t;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            t[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c] + a[r][3] * b[3][c];
        }
    }
    memcpy(ab, t, sizeof(Mtx44));
}
void PSMTX44Concat(const Mtx44 a, const Mtx44 b, Mtx44 ab) { C_MTX44Concat(a, b, ab); }

void C_MTX44MultVec(const Mtx44 m, const Vec* src, Vec* dst) {
    Vec v = *src;
    f32 w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
    w = 1.0f / w;
    dst->x = (m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3]) * w;
    dst->y = (m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3]) * w;
    dst->z = (m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]) * w;
}
void PSMTX44MultVec(const Mtx44 m, const Vec* src, Vec* dst) { C_MTX44MultVec(m, src, dst); }

void C_MTXLightFrustum(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
    f32 tmp = 1.0f / (r - l);
    m[0][0] = ((2 * n) * tmp) * scaleS;
    m[0][1] = 0.0f;
    m[0][2] = (((r + l) * tmp) * scaleS) - transS;
    m[0][3] = 0.0f;
    tmp = 1.0f / (t - b);
    m[1][0] = 0.0f;
    m[1][1] = ((2 * n) * tmp) * scaleT;
    m[1][2] = (((t + b) * tmp) * scaleT) - transT;
    m[1][3] = 0.0f;
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = -1.0f;
    m[2][3] = 0.0f;
}

void C_MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
    f32 angle = MTXDegToRad(fovY * 0.5f);
    f32 cot = 1.0f / tanf(angle);
    m[0][0] = (cot / aspect) * scaleS;
    m[0][1] = 0.0f;
    m[0][2] = -transS;
    m[0][3] = 0.0f;
    m[1][0] = 0.0f;
    m[1][1] = cot * scaleT;
    m[1][2] = -transT;
    m[1][3] = 0.0f;
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = -1.0f;
    m[2][3] = 0.0f;
}

void C_MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
    f32 tmp = 1.0f / (r - l);
    m[0][0] = (2.0f * tmp * scaleS);
    m[0][1] = 0.0f;
    m[0][2] = 0.0f;
    m[0][3] = ((-(r + l) * tmp) * scaleS) + transS;
    tmp = 1.0f / (t - b);
    m[1][0] = 0.0f;
    m[1][1] = (2.0f * tmp) * scaleT;
    m[1][2] = 0.0f;
    m[1][3] = ((-(t + b) * tmp) * scaleT) + transT;
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = 0.0f;
    m[2][3] = 1.0f;
}

