#pragma once
#include <string.h>
#include "revolution/types.h"
#include <vector>

struct AudioResampler {
    s16 hist[3][2] = {};
    double pos = 1.0;

    void process(const s16* in, u32 frames, double step, std::vector<s16>& out) {
        auto at = [&](int k, int c) -> float { return k < 3 ? hist[k][c] : in[(k - 3) * 2 + c]; };
        int total = (int)frames + 3;
        for (;;) {
            int i = (int)pos;
            if (i + 2 >= total) break;
            float t = (float)(pos - i);
            for (int c = 0; c < 2; c++) {
                float p0 = at(i - 1, c), p1 = at(i, c), p2 = at(i + 1, c), p3 = at(i + 2, c);
                float v = p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
                out.push_back((s16)(v < -32768.0f ? -32768.0f : v > 32767.0f ? 32767.0f : v));
            }
            pos += step;
        }
        s16 last[3][2];
        for (int k = 0; k < 3; k++) {
            last[k][0] = (s16)at(total - 3 + k, 0);
            last[k][1] = (s16)at(total - 3 + k, 1);
        }
        memcpy(hist, last, sizeof(hist));
        pos -= frames;
    }
};

void port_audio_mix_speaker(s16* lr, u32 frames, u32 rate);
