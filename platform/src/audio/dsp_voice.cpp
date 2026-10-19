#include "dsp_internal.h"

static void lowPass(Voice* v, s16* buf) {
    s16* hist = (s16*)(words(v) + W_LOWPASS_HIST);
    s32 yn1 = v->_8 ? 0 : hist[0], xn1 = v->_8 ? 0 : hist[1];
    s32 coeff = (u16)v->iir_filter_params[4];
    for (int i = 0; i < N; i++) {
        s32 xn0 = buf[i];
        s64 t = ((s64)(xn0 - xn1) * coeff >> 7) + yn1;
        s16 yn0 = clamp16((s32)(t < -0x8000 ? -0x8000 : t > 0x7FFF ? 0x7FFF : t));
        buf[i] = yn0;
        yn1 = yn0;
        xn1 = xn0;
    }
    hist[0] = (s16)yn1;
    hist[1] = (s16)xn1;
}

static void fir(Voice* v, s16* buf, int taps) {
    if (taps > 24) taps = 24;
    const s16* coeff = v->fir_filter_params;
    s16 x[20 + N];
    memcpy(x, v->_80, 40);
    memcpy(x + 20, buf, N * 2);
    for (int i = 0; i < N; i++) {
        s64 acc = 0;
        for (int k = 0; k < taps; k++) {
            int j = 20 + i - k;
            if (j >= 0) acc += (s64)coeff[k] * x[j];
        }
        buf[i] = clamp16((s32)(acc >> 15));
    }
    memcpy(v->_80, x + N, 40);
}

static void biquad(Voice* v, s16* buf) {
    s16* hist = v->_A8;
    const s16* c = v->iir_filter_params;
    s32 xn1 = hist[0], xn2 = hist[1], yn1 = hist[2], yn2 = hist[3];
    for (int i = 0; i < N; i++) {
        s32 xn0 = buf[i];
        s64 t = (s64)c[0] * xn1 + (s64)c[1] * xn2 + (s64)c[2] * yn1 + (s64)c[3] * yn2;
        s16 yn0 = clamp16((s32)(t >> 15));
        buf[i] = yn0;
        xn2 = xn1;
        xn1 = xn0;
        yn2 = yn1;
        yn1 = yn0;
    }
    hist[0] = (s16)xn1;
    hist[1] = (s16)xn2;
    hist[2] = (s16)yn1;
    hist[3] = (s16)yn2;
}

void addVoice(Voice* v) {
    if (!v->mIsActive || v->mIsFinished) return;

    Buffer in;
    loadInput(v, in);

    if (v->iir_filter_params[4] != 0) lowPass(v, in);
    int taps = v->mFilterMode & 0x1F;
    if (taps > 1) fir(v, in, taps);
    const s16* c = v->iir_filter_params;
    if ((v->mFilterMode & 0x20) && (c[0] != 0x7FFF || c[1] || c[2] || c[3])) biquad(v, in);

    if (v->_58) {
        if (v->mForcedStop) {
            v->_56 = v->_54 / 2;
            if (v->_56 == 0) v->mIsFinished = 1;
        }
        u32 x = (v->_50 >> 8) & 0x7F, y = v->_50 & 0x7F;
        s16 right = M.sine[x], back = M.sine[y], left = M.sine[x ^ 0x7F], front = M.sine[y ^ 0x7F];
        const int sh = 15;
        s16 quad[4] = {(s16)((left * front) >> sh), (s16)((left * back) >> sh), (s16)((right * front) >> sh), (s16)((right * back) >> sh)};
        s16 delta = (s16)(v->_56 - v->_54);
        s16 deltas[4], rev[4], revDeltas[4];
        for (int i = 0; i < 4; i++) deltas[i] = (s16)(((u16)quad[i] * delta) >> sh);
        for (int i = 0; i < 4; i++) quad[i] = (s16)((quad[i] * v->_54) >> sh);
        for (int i = 0; i < 4; i++) {
            rev[i] = (s16)((quad[i] * v->_52) >> sh);
            revDeltas[i] = (s16)((deltas[i] * v->_52) >> sh);
        }
        s16* dsts[8] = {M.fl, M.bl, M.fr, M.br, M.flRev, M.blRev, M.frRev, M.brRev};
        for (int i = 0; i < 8; i++) {
            s16 vol = i < 4 ? quad[i] : rev[i - 4];
            s16 dv = i < 4 ? deltas[i] : revDeltas[i - 4];
            addWithRamp(dsts[i], in, (s32)vol << 16, ((s32)dv << 16) / N);
        }
        v->_54 = v->_56;
    } else {
        u16* ch = words(v) + W_CHANNELS;
        if (v->mForcedStop) {
            bool mute = true;
            for (int i = 0; i < 6; i++) {
                ch[i * 4 + 1] = (u16)((s16)ch[i * 4 + 2] / 2);
                mute &= ch[i * 4 + 1] == 0;
            }
            if (mute) v->mIsFinished = 1;
        }
        for (int i = 0; i < 6; i++) {
            u16* c4 = ch + i * 4;
            if (!c4[0]) continue;
            s16 target = (s16)c4[1], current = (s16)c4[2];
            s32 step = ((s32)(s16)(target - current) << 16) / N;
            if (!current && !step) continue;
            s16* dst = M.bufferFor(c4[0]);
            if (!dst) continue;
            s32 vol = addWithRamp(dst, in, (s32)current << 16, step);
            c4[2] = (u16)(s16)(vol >> 16);
        }
    }
    if (!v->mPauseFlag) v->_8 = 0;
}
