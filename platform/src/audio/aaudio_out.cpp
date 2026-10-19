// Host audio output: the game's AI DMA stream (16-bit stereo, right sample
// first, normally 32 kHz) played through AAudio.  Blocks go into a ring that
// AAudio's data callback drains at the device's pace; the AI clock
// (ai_dsp.cpp) keeps the ring topped up.  PETARI_WAV=<path> also records the
// stream to a WAV file.
#include <aaudio/AAudio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "port/port.h"
#include "revolution/types.h"

#include "audio_resample.h"

namespace {

std::mutex sLock;  // the stream, the resampler and the WAV file
AAudioStream* sStream = nullptr;
bool sOpenFailed = false;
std::atomic<bool> sDisconnected{false};
int32_t sDeviceRate = 0;
std::vector<s16> sLR, sOut;

AudioResampler sResampler;

// Interleaved L/R frames at the device rate: written by port_audio_submit,
// read by the data callback.  Positions count frames and wrap freely.
const u32 kRingFrames = 16384;  // 0.5 s at 32 kHz
s16 sRing[kRingFrames * 2];
std::atomic<u32> sRingWrite{0}, sRingRead{0};
std::atomic<int> sUnderruns{0};
bool sPlaying = false;  // callback thread only: the ring had audio last time

FILE* sWav = nullptr;
u32 sWavFrames = 0;

// Output gain: silent while a cutscene is fast-forwarded (its sounds would
// come out in a jumble), with 30 ms ramps.
float sGain = 1.0f;

void applySkipGain(s16* lr, u32 frames, u32 rate) {
    float target = port_skip_fast_forwarding() ? 0.0f : 1.0f;
    if (sGain == target && target == 1.0f) {
        return;
    }
    float step = 1.0f / (rate * 0.03f);
    for (u32 i = 0; i < frames; i++) {
        sGain = target > sGain ? fminf(target, sGain + step) : fmaxf(target, sGain - step);
        lr[i * 2] = (s16)(lr[i * 2] * sGain);
        lr[i * 2 + 1] = (s16)(lr[i * 2 + 1] * sGain);
    }
}


void writeWavHeader(FILE* f, u32 frames, u32 rate) {
    u32 dataBytes = frames * 4;
    u8 h[44];
    memcpy(h, "RIFF", 4);
    u32 v = 36 + dataBytes;
    memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16;
    memcpy(h + 16, &v, 4);
    u16 s = 1;
    memcpy(h + 20, &s, 2);  // PCM
    s = 2;
    memcpy(h + 22, &s, 2);  // stereo
    memcpy(h + 24, &rate, 4);
    v = rate * 4;
    memcpy(h + 28, &v, 4);
    s = 4;
    memcpy(h + 32, &s, 2);
    s = 16;
    memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &dataBytes, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
    fseek(f, 0, SEEK_END);
}

// AAudio's real-time thread: copies queued frames out, silence when there
// are none.
aaudio_data_callback_result_t dataCallback(AAudioStream* stream, void*, void* audioData, int32_t numFrames) {
    s16* out = (s16*)audioData;
    u32 r = sRingRead.load(std::memory_order_relaxed);
    u32 avail = sRingWrite.load(std::memory_order_acquire) - r;
    u32 n = avail < (u32)numFrames ? avail : (u32)numFrames;
    for (u32 i = 0; i < n; i++) {
        u32 idx = (r + i) & (kRingFrames - 1);
        out[i * 2] = sRing[idx * 2];
        out[i * 2 + 1] = sRing[idx * 2 + 1];
    }
    if (n < (u32)numFrames) {
        memset(out + n * 2, 0, (size_t)(numFrames - n) * 4);
        if (sPlaying && !port_is_paused()) {
            sUnderruns++;
            if (stream) {
                int32_t cur = AAudioStream_getBufferSizeInFrames(stream);
                int32_t cap = AAudioStream_getBufferCapacityInFrames(stream);
                int32_t burst = AAudioStream_getFramesPerBurst(stream);
                if (burst > 0 && cur + burst <= cap) {
                    AAudioStream_setBufferSizeInFrames(stream, cur + burst);
                }
            }
        }
        sPlaying = false;
    } else {
        sPlaying = true;
    }
    sRingRead.store(r + n, std::memory_order_release);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

// The output device went away or changed: port_audio_submit reopens the
// stream (the callback may not do it itself).
void errorCallback(AAudioStream*, void*, aaudio_result_t error) {
    if (error == AAUDIO_ERROR_DISCONNECTED) {
        sDisconnected = true;
    }
}

bool openStream(u32 rate) {
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return false;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    // The device's own rate (48 kHz on the Quest): asking for the game's
    // 32 kHz moves AAudio off its fast path, to 20 ms bursts.
    (void)rate;
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
    AAudioStreamBuilder_setDataCallback(b, dataCallback, nullptr);
    AAudioStreamBuilder_setErrorCallback(b, errorCallback, nullptr);
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &sStream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) {
        port_log("audio: AAudio LOW_LATENCY open failed (%s), retrying with default mode...", AAudio_convertResultToText(r));
        if (AAudio_createStreamBuilder(&b) == AAUDIO_OK) {
            AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
            AAudioStreamBuilder_setChannelCount(b, 2);
            AAudioStreamBuilder_setSampleRate(b, 48000);
            AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_NONE);
            AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
            AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_MEDIA);
            AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
            AAudioStreamBuilder_setDataCallback(b, dataCallback, nullptr);
            AAudioStreamBuilder_setErrorCallback(b, errorCallback, nullptr);
            r = AAudioStreamBuilder_openStream(b, &sStream);
            AAudioStreamBuilder_delete(b);
        }
    }
    if (r != AAUDIO_OK) {
        port_log("audio: AAudio open failed: %s", AAudio_convertResultToText(r));
        sStream = nullptr;
        return false;
    }
    sDeviceRate = AAudioStream_getSampleRate(sStream);
    if (sDeviceRate <= 0) sDeviceRate = 48000;
    // Dynamic low-latency buffer sizing: start at 3 bursts (~6-12 ms latency),
    // and let dataCallback dynamically expand if hardware underruns occur.
    int32_t burst = AAudioStream_getFramesPerBurst(sStream);
    if (burst <= 0) burst = 192;
    int32_t cap = AAudioStream_getBufferCapacityInFrames(sStream);
    int32_t desired = (cap > 0) ? std::min(burst * 3, cap) : burst * 3;
    AAudioStream_setBufferSizeInFrames(sStream, desired);
    sRingRead.store(sRingWrite.load());
    sResampler = AudioResampler();
    r = AAudioStream_requestStart(sStream);
    if (r != AAUDIO_OK) {
        port_log("audio: AAudio requestStart failed: %s", AAudio_convertResultToText(r));
        AAudioStream_close(sStream);
        sStream = nullptr;
        return false;
    }
    port_log("audio: AAudio started: %d Hz (source %u Hz), burst %d frames, buffer %d frames", sDeviceRate, rate, burst, AAudioStream_getBufferSizeInFrames(sStream));
    return true;
}

}  // namespace

extern "C" void port_audio_submit(const s16* rightLeft, u32 frames, u32 rate) {
    std::lock_guard<std::mutex> lock(sLock);
    sLR.resize(frames * 2);
    for (u32 i = 0; i < frames; i++) {
        sLR[i * 2] = rightLeft[i * 2 + 1];
        sLR[i * 2 + 1] = rightLeft[i * 2];
    }
    port_audio_mix_speaker(sLR.data(), frames, rate);
    applySkipGain(sLR.data(), frames, rate);

    static bool wavChecked = false;
    if (!wavChecked) {
        wavChecked = true;
        if (const char* path = getenv("PETARI_WAV")) {
            if (*path && (sWav = fopen(path, "wb")) != nullptr) {
                writeWavHeader(sWav, 0, rate);
            }
        }
    }
    if (sWav) {
        fwrite(sLR.data(), 4, frames, sWav);
        sWavFrames += frames;
        if ((sWavFrames & 0x3FFF) < frames) {
            writeWavHeader(sWav, sWavFrames, rate);
            fflush(sWav);
        }
    }

    if (sStream && sDisconnected.exchange(false)) {
        port_log("audio: output device disconnected, reopening");
        AAudioStream_close(sStream);
        sStream = nullptr;
    }
    static int sRetryCounter = 0;
    if (!sStream) {
        if (sOpenFailed) {
            if (++sRetryCounter < 100) return; // Retry audio stream initialization after ~2 seconds
            sRetryCounter = 0;
            sOpenFailed = false;
        }
        sOpenFailed = !openStream(rate);
    }
    if (!sStream) return;

    const s16* out = sLR.data();
    u32 outFrames = frames;
    if ((u32)sDeviceRate != rate) {
        sOut.clear();
        sResampler.process(sLR.data(), frames, (double)rate / (double)sDeviceRate, sOut);
        out = sOut.data();
        outFrames = (u32)(sOut.size() / 2);
    }

    u32 w = sRingWrite.load(std::memory_order_relaxed);
    u32 space = kRingFrames - (w - sRingRead.load(std::memory_order_acquire));
    if (outFrames > space) {
        outFrames = space;  // the device stopped reading; the AI clock waits for it
    }
    for (u32 i = 0; i < outFrames; i++) {
        u32 idx = (w + i) & (kRingFrames - 1);
        sRing[idx * 2] = out[i * 2];
        sRing[idx * 2 + 1] = out[i * 2 + 1];
    }
    sRingWrite.store(w + outFrames, std::memory_order_release);
}


// Times the device found the ring empty while the game was running.
extern "C" int port_audio_underruns(void) { return sUnderruns.load(); }

// Audio queued for the device, or -1 without one (the AI then runs on a
// timer).
extern "C" int64_t port_audio_queued_ns(void) {
    std::lock_guard<std::mutex> lock(sLock);
    if (!sStream || sDeviceRate <= 0) return sOpenFailed ? -1 : 0;
    u32 queued = sRingWrite.load(std::memory_order_relaxed) - sRingRead.load(std::memory_order_acquire);
    return (int64_t)queued * 1000000000ll / sDeviceRate;
}

extern "C" void port_audio_pause(void) {
    std::lock_guard<std::mutex> lock(sLock);
    if (sStream) {
        AAudioStream_requestPause(sStream);
    }
}

extern "C" void port_audio_resume(void) {
    std::lock_guard<std::mutex> lock(sLock);
    if (sStream) {
        AAudioStream_requestStart(sStream);
    }
}
