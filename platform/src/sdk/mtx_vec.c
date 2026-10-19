#include <math.h>
#include <string.h>
#include "revolution/mtx.h"

// ---------------------------------------------------------------------------
// Vectors
// ---------------------------------------------------------------------------
void C_VECAdd(const Vec* a, const Vec* b, Vec* ab) {
    f32 x = a->x + b->x, y = a->y + b->y, z = a->z + b->z;
    ab->x = x; ab->y = y; ab->z = z;
}
void PSVECAdd(const Vec* a, const Vec* b, Vec* ab) { C_VECAdd(a, b, ab); }

void C_VECSubtract(const Vec* a, const Vec* b, Vec* a_b) {
    f32 x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;
    a_b->x = x; a_b->y = y; a_b->z = z;
}
void PSVECSubtract(const Vec* a, const Vec* b, Vec* a_b) { C_VECSubtract(a, b, a_b); }

void C_VECScale(const Vec* src, Vec* dst, f32 scale) {
    f32 x = src->x * scale, y = src->y * scale, z = src->z * scale;
    dst->x = x; dst->y = y; dst->z = z;
}
void PSVECScale(const Vec* src, Vec* dst, f32 scale) { C_VECScale(src, dst, scale); }

void C_VECNormalize(const Vec* src, Vec* unit) {
    f32 mag = src->x * src->x + src->y * src->y + src->z * src->z;
    f32 inv = 1.0f / sqrtf(mag);
    f32 x = src->x * inv, y = src->y * inv, z = src->z * inv;
    unit->x = x; unit->y = y; unit->z = z;
}
void PSVECNormalize(const Vec* src, Vec* unit) { C_VECNormalize(src, unit); }

f32 C_VECSquareMag(const Vec* v) { return v->x * v->x + v->y * v->y + v->z * v->z; }
f32 PSVECSquareMag(const Vec* v) { return C_VECSquareMag(v); }

f32 C_VECMag(const Vec* v) { return sqrtf(C_VECSquareMag(v)); }
f32 PSVECMag(const Vec* v) { return C_VECMag(v); }

f32 C_VECDotProduct(const Vec* a, const Vec* b) { return a->x * b->x + a->y * b->y + a->z * b->z; }
f32 PSVECDotProduct(const Vec* a, const Vec* b) { return C_VECDotProduct(a, b); }

void C_VECCrossProduct(const Vec* a, const Vec* b, Vec* axb) {
    f32 x = a->y * b->z - a->z * b->y;
    f32 y = a->z * b->x - a->x * b->z;
    f32 z = a->x * b->y - a->y * b->x;
    axb->x = x; axb->y = y; axb->z = z;
}
void PSVECCrossProduct(const Vec* a, const Vec* b, Vec* axb) { C_VECCrossProduct(a, b, axb); }

f32 C_VECSquareDistance(const Vec* a, const Vec* b) {
    f32 dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
    return dx * dx + dy * dy + dz * dz;
}
f32 PSVECSquareDistance(const Vec* a, const Vec* b) { return C_VECSquareDistance(a, b); }

f32 C_VECDistance(const Vec* a, const Vec* b) { return sqrtf(C_VECSquareDistance(a, b)); }
f32 PSVECDistance(const Vec* a, const Vec* b) { return C_VECDistance(a, b); }

void C_VECHalfAngle(const Vec* a, const Vec* b, Vec* half) {
    Vec aN, bN, h;
    aN.x = -a->x; aN.y = -a->y; aN.z = -a->z;
    bN.x = -b->x; bN.y = -b->y; bN.z = -b->z;
    C_VECNormalize(&aN, &aN);
    C_VECNormalize(&bN, &bN);
    C_VECAdd(&aN, &bN, &h);
    if (C_VECDotProduct(&h, &h) > 0.0f) {
        C_VECNormalize(&h, half);
    } else {
        *half = h;
    }
}

void C_VECReflect(const Vec* src, const Vec* normal, Vec* dst) {
    Vec uI, uN;
    uI.x = -src->x; uI.y = -src->y; uI.z = -src->z;
    C_VECNormalize(&uI, &uI);
    C_VECNormalize(normal, &uN);
    f32 cosA = C_VECDotProduct(&uI, &uN);
    Vec r;
    r.x = 2.0f * uN.x * cosA - uI.x;
    r.y = 2.0f * uN.y * cosA - uI.y;
    r.z = 2.0f * uN.z * cosA - uI.z;
    C_VECNormalize(&r, dst);
}

// ---------------------------------------------------------------------------
// Quaternions
// ---------------------------------------------------------------------------
void C_QUATAdd(const Quaternion* p, const Quaternion* q, Quaternion* r) {
    r->x = p->x + q->x; r->y = p->y + q->y; r->z = p->z + q->z; r->w = p->w + q->w;
}
void PSQUATAdd(const Quaternion* p, const Quaternion* q, Quaternion* r) { C_QUATAdd(p, q, r); }

void C_QUATMultiply(const Quaternion* p, const Quaternion* q, Quaternion* pq) {
    Quaternion t;
    t.w = p->w * q->w - p->x * q->x - p->y * q->y - p->z * q->z;
    t.x = p->w * q->x + p->x * q->w + p->y * q->z - p->z * q->y;
    t.y = p->w * q->y + p->y * q->w + p->z * q->x - p->x * q->z;
    t.z = p->w * q->z + p->z * q->w + p->x * q->y - p->y * q->x;
    *pq = t;
}
void PSQUATMultiply(const Quaternion* p, const Quaternion* q, Quaternion* pq) { C_QUATMultiply(p, q, pq); }

f32 C_QUATDotProduct(const Quaternion* p, const Quaternion* q) { return p->x * q->x + p->y * q->y + p->z * q->z + p->w * q->w; }
f32 PSQUATDotProduct(const Quaternion* p, const Quaternion* q) { return C_QUATDotProduct(p, q); }

void C_QUATScale(const Quaternion* q, Quaternion* r, f32 scale) {
    r->x = q->x * scale; r->y = q->y * scale; r->z = q->z * scale; r->w = q->w * scale;
}
void PSQUATScale(const Quaternion* q, Quaternion* r, f32 scale) { C_QUATScale(q, r, scale); }

void C_QUATNormalize(const Quaternion* src, Quaternion* unit) {
    f32 mag = src->x * src->x + src->y * src->y + src->z * src->z + src->w * src->w;
    if (mag >= 0.00001f) {
        mag = 1.0f / sqrtf(mag);
        unit->x = src->x * mag; unit->y = src->y * mag; unit->z = src->z * mag; unit->w = src->w * mag;
    } else {
        unit->x = unit->y = unit->z = unit->w = 0.0f;
    }
}
void PSQUATNormalize(const Quaternion* src, Quaternion* unit) { C_QUATNormalize(src, unit); }

void C_QUATMtx(Quaternion* r, const Mtx m) {
    f32 tr = m[0][0] + m[1][1] + m[2][2];
    if (tr > 0.0f) {
        f32 s = sqrtf(tr + 1.0f);
        r->w = s * 0.5f;
        s = 0.5f / s;
        r->x = (m[2][1] - m[1][2]) * s;
        r->y = (m[0][2] - m[2][0]) * s;
        r->z = (m[1][0] - m[0][1]) * s;
    } else {
        static const int nxt[3] = {1, 2, 0};
        int i = 0;
        if (m[1][1] > m[0][0]) i = 1;
        if (m[2][2] > m[i][i]) i = 2;
        int j = nxt[i];
        int k = nxt[j];
        f32 s = sqrtf((m[i][i] - (m[j][j] + m[k][k])) + 1.0f);
        f32 q[4];
        q[i] = s * 0.5f;
        if (s != 0.0f) s = 0.5f / s;
        q[3] = (m[k][j] - m[j][k]) * s;
        q[j] = (m[i][j] + m[j][i]) * s;
        q[k] = (m[i][k] + m[k][i]) * s;
        r->x = q[0]; r->y = q[1]; r->z = q[2]; r->w = q[3];
    }
}

void C_QUATSlerp(const Quaternion* p, const Quaternion* q, Quaternion* r, f32 t) {
    f32 cosTheta = p->x * q->x + p->y * q->y + p->z * q->z + p->w * q->w;
    f32 tq = 1.0f;
    if (cosTheta < 0.0f) {
        cosTheta = -cosTheta;
        tq = -tq;
    }
    f32 tp;
    if (cosTheta <= 1.0f - 0.00001f) {
        f32 theta = acosf(cosTheta);
        f32 sinTheta = sinf(theta);
        tp = sinf((1.0f - t) * theta) / sinTheta;
        tq *= sinf(t * theta) / sinTheta;
    } else {
        tp = 1.0f - t;
        tq *= t;
    }
    r->x = tp * p->x + tq * q->x;
    r->y = tp * p->y + tq * q->y;
    r->z = tp * p->z + tq * q->z;
    r->w = tp * p->w + tq * q->w;
}
