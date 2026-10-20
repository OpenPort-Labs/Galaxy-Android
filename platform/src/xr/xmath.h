// Minimal column-major 4x4 matrix / quaternion helpers for the VR layer.
#pragma once

#include <math.h>
#include <string.h>

namespace xm {

struct Vec3 {
    float x, y, z;
};

struct Quat {
    float x, y, z, w;
};

struct Mat4 {
    float m[16];  // column-major (OpenGL)

    static Mat4 identity() {
        Mat4 r;
        memset(r.m, 0, sizeof(r.m));
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; c++) {
        for (int rr = 0; rr < 4; rr++) {
            r.m[c * 4 + rr] = a.m[0 * 4 + rr] * b.m[c * 4 + 0] + a.m[1 * 4 + rr] * b.m[c * 4 + 1] + a.m[2 * 4 + rr] * b.m[c * 4 + 2] +
                              a.m[3 * 4 + rr] * b.m[c * 4 + 3];
        }
    }
    return r;
}

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 a) { return sqrtf(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    float l = length(a);
    return l > 1e-8f ? a * (1.0f / l) : Vec3{0, 0, 0};
}

inline Vec3 rotate(Quat q, Vec3 v) {
    Vec3 u{q.x, q.y, q.z};
    Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

inline Quat conj(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }

inline Mat4 fromRotation(Quat q) {
    Mat4 r = Mat4::identity();
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    r.at(0, 0) = 1 - 2 * (yy + zz);
    r.at(0, 1) = 2 * (xy - wz);
    r.at(0, 2) = 2 * (xz + wy);
    r.at(1, 0) = 2 * (xy + wz);
    r.at(1, 1) = 1 - 2 * (xx + zz);
    r.at(1, 2) = 2 * (yz - wx);
    r.at(2, 0) = 2 * (xz - wy);
    r.at(2, 1) = 2 * (yz + wx);
    r.at(2, 2) = 1 - 2 * (xx + yy);
    return r;
}

inline Mat4 translation(Vec3 t) {
    Mat4 r = Mat4::identity();
    r.at(0, 3) = t.x;
    r.at(1, 3) = t.y;
    r.at(2, 3) = t.z;
    return r;
}

inline Mat4 scale(float s) {
    Mat4 r = Mat4::identity();
    r.at(0, 0) = r.at(1, 1) = r.at(2, 2) = s;
    return r;
}

// World-from-pose matrix.
inline Mat4 poseMatrix(Quat q, Vec3 p) { return translation(p) * fromRotation(q); }

// Pose-from-world (view) matrix.
inline Mat4 inversePose(Quat q, Vec3 p) {
    Quat c = conj(q);
    Vec3 t = rotate(c, p * -1.0f);
    return translation(t) * fromRotation(c);
}

// OpenXR asymmetric FOV -> OpenGL clip space (z in [-1, 1]).
inline Vec3 transformPoint(const Mat4& a, Vec3 p) {
    return {a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12], a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13],
            a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14]};
}

// General inverse (cofactors); identity if singular.
inline Mat4 inverse(const Mat4& a) {
    const float* m = a.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabsf(det) < 1e-30f) return Mat4::identity();
    Mat4 r;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] / det;
    return r;
}

// Rotation of an orthonormal 3x3 block -> quaternion.
inline Quat quatFromMatrix(const Mat4& a) {
    float m00 = a.at(0, 0), m11 = a.at(1, 1), m22 = a.at(2, 2);
    float tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (a.at(2, 1) - a.at(1, 2)) / s;
        q.y = (a.at(0, 2) - a.at(2, 0)) / s;
        q.z = (a.at(1, 0) - a.at(0, 1)) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (a.at(2, 1) - a.at(1, 2)) / s;
        q.x = 0.25f * s;
        q.y = (a.at(0, 1) + a.at(1, 0)) / s;
        q.z = (a.at(0, 2) + a.at(2, 0)) / s;
    } else if (m11 > m22) {
        float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (a.at(0, 2) - a.at(2, 0)) / s;
        q.x = (a.at(0, 1) + a.at(1, 0)) / s;
        q.y = 0.25f * s;
        q.z = (a.at(1, 2) + a.at(2, 1)) / s;
    } else {
        float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (a.at(1, 0) - a.at(0, 1)) / s;
        q.x = (a.at(0, 2) + a.at(2, 0)) / s;
        q.y = (a.at(1, 2) + a.at(2, 1)) / s;
        q.z = 0.25f * s;
    }
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return l > 1e-8f ? Quat{q.x / l, q.y / l, q.z / l, q.w / l} : Quat{0, 0, 0, 1};
}

// A 3x4 row-major matrix (the game's) as a Mat4.
inline Mat4 fromRows3x4(const float* r) {
    Mat4 m = Mat4::identity();
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++) {
            m.at(row, col) = r[row * 4 + col];
        }
    }
    return m;
}

inline Mat4 projectionFov(float angleLeft, float angleRight, float angleUp, float angleDown, float nearZ, float farZ) {
    float tl = tanf(angleLeft), tr = tanf(angleRight), tu = tanf(angleUp), td = tanf(angleDown);
    float w = tr - tl, h = tu - td;
    Mat4 r;
    memset(r.m, 0, sizeof(r.m));
    r.at(0, 0) = 2.0f / w;
    r.at(0, 2) = (tr + tl) / w;
    r.at(1, 1) = 2.0f / h;
    r.at(1, 2) = (tu + td) / h;
    r.at(2, 2) = -(farZ + nearZ) / (farZ - nearZ);
    r.at(2, 3) = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    r.at(3, 2) = -1.0f;
    return r;
}

}  // namespace xm
