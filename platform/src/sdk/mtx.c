// Portable implementation of the Revolution SDK MTX/VEC/QUAT library.
// The original PS* variants are hand-written paired-single assembly; these are
// straightforward C equivalents with the same conventions:
//   - Mtx is 3x4 row-major, points are column vectors (v' = M * v), translation in column 3.
//   - Every function tolerates its output aliasing an input.
#include <math.h>
#include <string.h>
#include "revolution/mtx.h"

void C_VECNormalize(const Vec* src, Vec* unit);
void C_VECCrossProduct(const Vec* a, const Vec* b, Vec* axb);
void C_VECAdd(const Vec* a, const Vec* b, Vec* ab);
f32 C_VECDotProduct(const Vec* a, const Vec* b);

// ---------------------------------------------------------------------------
// 3x4 matrices
// ---------------------------------------------------------------------------
void C_MTXIdentity(Mtx m) {
    m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = 0.0f;
    m[1][0] = 0.0f; m[1][1] = 1.0f; m[1][2] = 0.0f; m[1][3] = 0.0f;
    m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = 1.0f; m[2][3] = 0.0f;
}
void PSMTXIdentity(Mtx m) { C_MTXIdentity(m); }

void C_MTXCopy(const Mtx src, Mtx dst) {
    if (src != (const float(*)[4])dst) {
        memmove(dst, src, sizeof(Mtx));
    }
}
void PSMTXCopy(const Mtx src, Mtx dst) { C_MTXCopy(src, dst); }

void C_MTXConcat(const Mtx a, const Mtx b, Mtx ab) {
    Mtx t;
    for (int r = 0; r < 3; r++) {
        t[r][0] = a[r][0] * b[0][0] + a[r][1] * b[1][0] + a[r][2] * b[2][0];
        t[r][1] = a[r][0] * b[0][1] + a[r][1] * b[1][1] + a[r][2] * b[2][1];
        t[r][2] = a[r][0] * b[0][2] + a[r][1] * b[1][2] + a[r][2] * b[2][2];
        t[r][3] = a[r][0] * b[0][3] + a[r][1] * b[1][3] + a[r][2] * b[2][3] + a[r][3];
    }
    memcpy(ab, t, sizeof(Mtx));
}
void PSMTXConcat(const Mtx a, const Mtx b, Mtx ab) { C_MTXConcat(a, b, ab); }

void C_MTXConcatArray(const Mtx a, const Mtx* srcBase, Mtx* dstBase, u32 count) {
    for (u32 i = 0; i < count; i++) {
        C_MTXConcat(a, srcBase[i], dstBase[i]);
    }
}
void PSMTXConcatArray(const Mtx a, const Mtx* srcBase, Mtx* dstBase, u32 count) { C_MTXConcatArray(a, srcBase, dstBase, count); }

void C_MTXTranspose(const Mtx src, Mtx xPose) {
    Mtx t;
    t[0][0] = src[0][0]; t[0][1] = src[1][0]; t[0][2] = src[2][0]; t[0][3] = 0.0f;
    t[1][0] = src[0][1]; t[1][1] = src[1][1]; t[1][2] = src[2][1]; t[1][3] = 0.0f;
    t[2][0] = src[0][2]; t[2][1] = src[1][2]; t[2][2] = src[2][2]; t[2][3] = 0.0f;
    memcpy(xPose, t, sizeof(Mtx));
}
void PSMTXTranspose(const Mtx src, Mtx xPose) { C_MTXTranspose(src, xPose); }

u32 C_MTXInverse(const Mtx src, Mtx inv) {
    f32 det = src[0][0] * src[1][1] * src[2][2] + src[0][1] * src[1][2] * src[2][0] + src[0][2] * src[1][0] * src[2][1] -
              src[2][0] * src[1][1] * src[0][2] - src[1][0] * src[0][1] * src[2][2] - src[0][0] * src[2][1] * src[1][2];
    if (det == 0.0f) {
        return 0;
    }
    f32 d = 1.0f / det;
    Mtx t;
    t[0][0] = (src[1][1] * src[2][2] - src[2][1] * src[1][2]) * d;
    t[0][1] = -(src[0][1] * src[2][2] - src[2][1] * src[0][2]) * d;
    t[0][2] = (src[0][1] * src[1][2] - src[1][1] * src[0][2]) * d;
    t[1][0] = -(src[1][0] * src[2][2] - src[2][0] * src[1][2]) * d;
    t[1][1] = (src[0][0] * src[2][2] - src[2][0] * src[0][2]) * d;
    t[1][2] = -(src[0][0] * src[1][2] - src[1][0] * src[0][2]) * d;
    t[2][0] = (src[1][0] * src[2][1] - src[2][0] * src[1][1]) * d;
    t[2][1] = -(src[0][0] * src[2][1] - src[2][0] * src[0][1]) * d;
    t[2][2] = (src[0][0] * src[1][1] - src[1][0] * src[0][1]) * d;
    t[0][3] = -t[0][0] * src[0][3] - t[0][1] * src[1][3] - t[0][2] * src[2][3];
    t[1][3] = -t[1][0] * src[0][3] - t[1][1] * src[1][3] - t[1][2] * src[2][3];
    t[2][3] = -t[2][0] * src[0][3] - t[2][1] * src[1][3] - t[2][2] * src[2][3];
    memcpy(inv, t, sizeof(Mtx));
    return 1;
}
u32 PSMTXInverse(const Mtx src, Mtx inv) { return C_MTXInverse(src, inv); }

u32 C_MTXInvXpose(const Mtx src, Mtx invX) {
    f32 det = src[0][0] * src[1][1] * src[2][2] + src[0][1] * src[1][2] * src[2][0] + src[0][2] * src[1][0] * src[2][1] -
              src[2][0] * src[1][1] * src[0][2] - src[1][0] * src[0][1] * src[2][2] - src[0][0] * src[2][1] * src[1][2];
    if (det == 0.0f) {
        return 0;
    }
    f32 d = 1.0f / det;
    Mtx t;
    t[0][0] = (src[1][1] * src[2][2] - src[2][1] * src[1][2]) * d;
    t[0][1] = -(src[1][0] * src[2][2] - src[2][0] * src[1][2]) * d;
    t[0][2] = (src[1][0] * src[2][1] - src[2][0] * src[1][1]) * d;
    t[1][0] = -(src[0][1] * src[2][2] - src[2][1] * src[0][2]) * d;
    t[1][1] = (src[0][0] * src[2][2] - src[2][0] * src[0][2]) * d;
    t[1][2] = -(src[0][0] * src[2][1] - src[2][0] * src[0][1]) * d;
    t[2][0] = (src[0][1] * src[1][2] - src[1][1] * src[0][2]) * d;
    t[2][1] = -(src[0][0] * src[1][2] - src[1][0] * src[0][2]) * d;
    t[2][2] = (src[0][0] * src[1][1] - src[1][0] * src[0][1]) * d;
    t[0][3] = t[1][3] = t[2][3] = 0.0f;
    memcpy(invX, t, sizeof(Mtx));
    return 1;
}
u32 PSMTXInvXpose(const Mtx src, Mtx invX) { return C_MTXInvXpose(src, invX); }

void C_MTXMultVec(const Mtx m, const Vec* src, Vec* dst) {
    Vec v = *src;
    dst->x = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3];
    dst->y = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3];
    dst->z = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3];
}
void PSMTXMultVec(const Mtx m, const Vec* src, Vec* dst) { C_MTXMultVec(m, src, dst); }

void C_MTXMultVecSR(const Mtx m, const Vec* src, Vec* dst) {
    Vec v = *src;
    dst->x = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z;
    dst->y = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z;
    dst->z = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z;
}
void PSMTXMultVecSR(const Mtx m, const Vec* src, Vec* dst) { C_MTXMultVecSR(m, src, dst); }

void C_MTXMultVecArray(const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count) {
    for (u32 i = 0; i < count; i++) {
        C_MTXMultVec(m, &srcBase[i], &dstBase[i]);
    }
}
void PSMTXMultVecArray(const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count) { C_MTXMultVecArray(m, srcBase, dstBase, count); }

void C_MTXMultVecArraySR(const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count) {
    for (u32 i = 0; i < count; i++) {
        C_MTXMultVecSR(m, &srcBase[i], &dstBase[i]);
    }
}
void PSMTXMultVecArraySR(const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count) { C_MTXMultVecArraySR(m, srcBase, dstBase, count); }

void C_MTXTrans(Mtx m, f32 xT, f32 yT, f32 zT) {
    m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = xT;
    m[1][0] = 0.0f; m[1][1] = 1.0f; m[1][2] = 0.0f; m[1][3] = yT;
    m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = 1.0f; m[2][3] = zT;
}
void PSMTXTrans(Mtx m, f32 xT, f32 yT, f32 zT) { C_MTXTrans(m, xT, yT, zT); }

void C_MTXTransApply(const Mtx src, Mtx dst, f32 xT, f32 yT, f32 zT) {
    if (src != (const float(*)[4])dst) {
        memcpy(dst, src, sizeof(Mtx));
    }
    dst[0][3] += xT;
    dst[1][3] += yT;
    dst[2][3] += zT;
}
void PSMTXTransApply(const Mtx src, Mtx dst, f32 xT, f32 yT, f32 zT) { C_MTXTransApply(src, dst, xT, yT, zT); }

void C_MTXScale(Mtx m, f32 xS, f32 yS, f32 zS) {
    m[0][0] = xS;   m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = 0.0f;
    m[1][0] = 0.0f; m[1][1] = yS;   m[1][2] = 0.0f; m[1][3] = 0.0f;
    m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = zS;   m[2][3] = 0.0f;
}
void PSMTXScale(Mtx m, f32 xS, f32 yS, f32 zS) { C_MTXScale(m, xS, yS, zS); }

void C_MTXScaleApply(const Mtx src, Mtx dst, f32 xS, f32 yS, f32 zS) {
    for (int c = 0; c < 4; c++) {
        dst[0][c] = src[0][c] * xS;
        dst[1][c] = src[1][c] * yS;
        dst[2][c] = src[2][c] * zS;
    }
}
void PSMTXScaleApply(const Mtx src, Mtx dst, f32 xS, f32 yS, f32 zS) { C_MTXScaleApply(src, dst, xS, yS, zS); }

void C_MTXRotTrig(Mtx m, char axis, f32 sinA, f32 cosA) {
    switch (axis) {
    case 'x':
    case 'X':
        m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 0.0f;  m[0][3] = 0.0f;
        m[1][0] = 0.0f; m[1][1] = cosA; m[1][2] = -sinA; m[1][3] = 0.0f;
        m[2][0] = 0.0f; m[2][1] = sinA; m[2][2] = cosA;  m[2][3] = 0.0f;
        break;
    case 'y':
    case 'Y':
        m[0][0] = cosA;  m[0][1] = 0.0f; m[0][2] = sinA; m[0][3] = 0.0f;
        m[1][0] = 0.0f;  m[1][1] = 1.0f; m[1][2] = 0.0f; m[1][3] = 0.0f;
        m[2][0] = -sinA; m[2][1] = 0.0f; m[2][2] = cosA; m[2][3] = 0.0f;
        break;
    case 'z':
    case 'Z':
        m[0][0] = cosA; m[0][1] = -sinA; m[0][2] = 0.0f; m[0][3] = 0.0f;
        m[1][0] = sinA; m[1][1] = cosA;  m[1][2] = 0.0f; m[1][3] = 0.0f;
        m[2][0] = 0.0f; m[2][1] = 0.0f;  m[2][2] = 1.0f; m[2][3] = 0.0f;
        break;
    default:
        break;
    }
}
void PSMTXRotTrig(Mtx m, char axis, f32 sinA, f32 cosA) { C_MTXRotTrig(m, axis, sinA, cosA); }

void C_MTXRotRad(Mtx m, char axis, f32 rad) { C_MTXRotTrig(m, axis, sinf(rad), cosf(rad)); }
void PSMTXRotRad(Mtx m, char axis, f32 rad) { C_MTXRotRad(m, axis, rad); }

void C_MTXRotAxisRad(Mtx m, const Vec* axis, f32 rad) {
    f32 s = sinf(rad), c = cosf(rad), t = 1.0f - c;
    f32 mag = sqrtf(axis->x * axis->x + axis->y * axis->y + axis->z * axis->z);
    f32 x = axis->x, y = axis->y, z = axis->z;
    if (mag != 0.0f) {
        x /= mag;
        y /= mag;
        z /= mag;
    }
    f32 xSq = x * x, ySq = y * y, zSq = z * z;
    m[0][0] = (t * xSq) + c;
    m[0][1] = (t * x * y) - (s * z);
    m[0][2] = (t * x * z) + (s * y);
    m[0][3] = 0.0f;
    m[1][0] = (t * x * y) + (s * z);
    m[1][1] = (t * ySq) + c;
    m[1][2] = (t * y * z) - (s * x);
    m[1][3] = 0.0f;
    m[2][0] = (t * x * z) - (s * y);
    m[2][1] = (t * y * z) + (s * x);
    m[2][2] = (t * zSq) + c;
    m[2][3] = 0.0f;
}
void PSMTXRotAxisRad(Mtx m, const Vec* axis, f32 rad) { C_MTXRotAxisRad(m, axis, rad); }

void C_MTXQuat(Mtx m, const Quaternion* q) {
    f32 s = 2.0f / (q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w);
    f32 xs = q->x * s, ys = q->y * s, zs = q->z * s;
    f32 wx = q->w * xs, wy = q->w * ys, wz = q->w * zs;
    f32 xx = q->x * xs, xy = q->x * ys, xz = q->x * zs;
    f32 yy = q->y * ys, yz = q->y * zs, zz = q->z * zs;
    m[0][0] = 1.0f - (yy + zz);
    m[0][1] = xy - wz;
    m[0][2] = xz + wy;
    m[0][3] = 0.0f;
    m[1][0] = xy + wz;
    m[1][1] = 1.0f - (xx + zz);
    m[1][2] = yz - wx;
    m[1][3] = 0.0f;
    m[2][0] = xz - wy;
    m[2][1] = yz + wx;
    m[2][2] = 1.0f - (xx + yy);
    m[2][3] = 0.0f;
}
void PSMTXQuat(Mtx m, const Quaternion* q) { C_MTXQuat(m, q); }

void C_MTXReflect(Mtx m, const Vec* p, const Vec* n) {
    f32 vxy = -2.0f * n->x * n->y, vxz = -2.0f * n->x * n->z, vyz = -2.0f * n->y * n->z;
    f32 pdotn = 2.0f * (p->x * n->x + p->y * n->y + p->z * n->z);
    m[0][0] = 1.0f - 2.0f * n->x * n->x; m[0][1] = vxy; m[0][2] = vxz; m[0][3] = pdotn * n->x;
    m[1][0] = vxy; m[1][1] = 1.0f - 2.0f * n->y * n->y; m[1][2] = vyz; m[1][3] = pdotn * n->y;
    m[2][0] = vxz; m[2][1] = vyz; m[2][2] = 1.0f - 2.0f * n->z * n->z; m[2][3] = pdotn * n->z;
}
void PSMTXReflect(Mtx m, const Vec* p, const Vec* n) { C_MTXReflect(m, p, n); }

void C_MTXLookAt(Mtx m, const Point3d* camPos, const Vec* camUp, const Point3d* target) {
    Vec vLook, vRight, vUp;
    vLook.x = camPos->x - target->x;
    vLook.y = camPos->y - target->y;
    vLook.z = camPos->z - target->z;
    C_VECNormalize(&vLook, &vLook);
    C_VECCrossProduct(camUp, &vLook, &vRight);
    C_VECNormalize(&vRight, &vRight);
    C_VECCrossProduct(&vLook, &vRight, &vUp);
    m[0][0] = vRight.x; m[0][1] = vRight.y; m[0][2] = vRight.z;
    m[0][3] = -(camPos->x * vRight.x + camPos->y * vRight.y + camPos->z * vRight.z);
    m[1][0] = vUp.x; m[1][1] = vUp.y; m[1][2] = vUp.z;
    m[1][3] = -(camPos->x * vUp.x + camPos->y * vUp.y + camPos->z * vUp.z);
    m[2][0] = vLook.x; m[2][1] = vLook.y; m[2][2] = vLook.z;
    m[2][3] = -(camPos->x * vLook.x + camPos->y * vLook.y + camPos->z * vLook.z);
}


