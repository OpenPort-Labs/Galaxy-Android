#include "audio_resample.h"

#include <mutex>
#include "port/port.h"

static const u32 kSpeakerRate = 6000;
static std::mutex sSpkLock;
static s16 sSpk[4096];
static u32 sSpkRead = 0, sSpkWrite = 0;
static double sSpkPhase = 0.0;

void port_audio_mix_speaker(s16* lr, u32 frames, u32 rate) {
    std::lock_guard<std::mutex> lock(sSpkLock);
    double step = (double)kSpeakerRate / (double)rate;
    for (u32 i = 0; i < frames; i++) {
        if (sSpkWrite - sSpkRead < 2) break;
        u32 a = sSpkRead & 4095, b = (sSpkRead + 1) & 4095;
        float s = sSpk[a] + (sSpk[b] - sSpk[a]) * (float)sSpkPhase;
        s *= 0.8f;
        for (int c = 0; c < 2; c++) {
            int v = lr[i * 2 + c] + (int)s;
            lr[i * 2 + c] = (s16)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
        }
        sSpkPhase += step;
        while (sSpkPhase >= 1.0) {
            sSpkPhase -= 1.0;
            sSpkRead++;
        }
    }
}

extern "C" void port_speaker_push(const s16* samples, int count) {
    std::lock_guard<std::mutex> lock(sSpkLock);
    for (int i = 0; i < count; i++) {
        if (sSpkWrite - sSpkRead >= 4096) sSpkRead++;
        sSpk[sSpkWrite & 4095] = samples[i];
        sSpkWrite++;
    }
}
