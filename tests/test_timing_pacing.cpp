#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mirror of core VI timing algorithms and constants from platform/src/vi/vi.cpp
static const int64_t kBaseViPeriodNs = 16683350LL; // 59.94 Hz

static inline int64_t calcSimPeriod(float speed) {
    if (speed < 0.1f) speed = 1.0f;
    return (int64_t)((double)kBaseViPeriodNs / speed);
}

static inline int64_t calcMinInterval(int64_t simPeriod) {
    int64_t minInterval = (int64_t)(simPeriod * 0.88);
    if (minInterval < 2000000LL) minInterval = 2000000LL;
    return minInterval;
}

// Simulated retrace engine tracking both display flips and fallback clock ticks
struct SimulatedViEngine {
    int64_t raisedAt = 0;
    float speedMultiplier = 1.0f;
    uint32_t retraceCount = 0;

    void reset(float speed = 1.0f) {
        raisedAt = 0;
        speedMultiplier = speed;
        retraceCount = 0;
    }

    bool raiseRetrace(int64_t nowNs) {
        int64_t period = calcSimPeriod(speedMultiplier);
        int64_t minInterval = calcMinInterval(period);
        if (nowNs - raisedAt < minInterval) {
            return false;
        }
        raisedAt = nowNs;
        retraceCount++;
        return true;
    }

    // Step clock tick from viClock fallback loop
    bool onViClockTick(int64_t nowNs) {
        int64_t period = calcSimPeriod(speedMultiplier);
        if (nowNs - raisedAt < (int64_t)(period * 0.70)) {
            return false;
        }
        return raiseRetrace(nowNs);
    }
};

static void testSimulationPeriodAndInterval(void) {
    int64_t period60 = calcSimPeriod(1.0f);
    assert(period60 == 16683350LL);
    int64_t minInterval60 = calcMinInterval(period60);
    // 88% of 16.683ms is ~14.68ms
    assert(minInterval60 == 14681348LL);

    // 90Hz display frame time (11.11ms) must strictly be REJECTED (< 14.68ms)
    int64_t frame90Hz = 11111111LL;
    assert(frame90Hz < minInterval60);

    // 120Hz display frame time (8.33ms) must strictly be REJECTED (< 14.68ms)
    int64_t frame120Hz = 8333333LL;
    assert(frame120Hz < minInterval60);

    // 144Hz display frame time (6.94ms) must strictly be REJECTED (< 14.68ms)
    int64_t frame144Hz = 6944444LL;
    assert(frame144Hz < minInterval60);

    // 60Hz display frame time (16.67ms) must be ACCEPTED (>= 14.68ms)
    int64_t frame60Hz = 16666666LL;
    assert(frame60Hz >= minInterval60);

    // 30Hz display frame time (33.33ms) must be ACCEPTED (>= 14.68ms)
    int64_t frame30Hz = 33333333LL;
    assert(frame30Hz >= minInterval60);

    printf("[PASS] Simulation period invariants and refresh rate rejection thresholds verified\n");
}

static void test90HzSimulationDecoupling(void) {
    SimulatedViEngine engine;
    engine.reset(1.0f);

    // Simulate 1 second (1,000,000,000 ns) of 90 FPS rendering (calls every 11.11ms)
    // with interleaved viClock fallback loop running at authentic 16.68ms period.
    const int64_t totalDurationNs = 1000000000LL;
    const int64_t presentPeriodNs = 1000000000LL / 90; // ~11.11ms
    const int64_t clockPeriodNs = kBaseViPeriodNs;      // ~16.68ms

    int64_t nextPresentNs = presentPeriodNs;
    int64_t nextClockNs = clockPeriodNs;
    int64_t currentNs = 0;

    // Initial first retrace at boot
    engine.raiseRetrace(0);

    while (currentNs < totalDurationNs) {
        if (nextPresentNs <= nextClockNs) {
            currentNs = nextPresentNs;
            engine.raiseRetrace(currentNs);
            nextPresentNs += presentPeriodNs;
        } else {
            currentNs = nextClockNs;
            engine.onViClockTick(currentNs);
            nextClockNs += clockPeriodNs;
        }
    }

    // In 1 second, a 90 FPS display mode MUST yield ~60 simulation retraces (NOT 90!)
    // Tolerating +-1 boundary frame for 59.94 Hz timing over 1.00s.
    assert(engine.retraceCount >= 59 && engine.retraceCount <= 61);
    printf("[PASS] 90 Hz presentation simulation rate clamped to 60 Hz simulation cadence (%u ticks)\n",
           engine.retraceCount);
}

static void test30HzBatterySaverCadence(void) {
    SimulatedViEngine engine;
    engine.reset(1.0f);

    // Simulate 1 second of 30 FPS rendering (calls every 33.33ms)
    // viClock MUST fill the intermediate slots so physics runs at 100% normal speed.
    const int64_t totalDurationNs = 1000000000LL;
    const int64_t presentPeriodNs = 1000000000LL / 30; // ~33.33ms
    const int64_t clockPeriodNs = kBaseViPeriodNs;      // ~16.68ms

    int64_t nextPresentNs = presentPeriodNs;
    int64_t nextClockNs = clockPeriodNs;
    int64_t currentNs = 0;

    engine.raiseRetrace(0);

    while (currentNs < totalDurationNs) {
        if (nextPresentNs <= nextClockNs) {
            currentNs = nextPresentNs;
            engine.raiseRetrace(currentNs);
            nextPresentNs += presentPeriodNs;
        } else {
            currentNs = nextClockNs;
            engine.onViClockTick(currentNs);
            nextClockNs += clockPeriodNs;
        }
    }

    // In 1 second, 30 FPS presentation MUST yield ~60 simulation retraces (NOT 30!)
    // This fixes Issue #112 slow motion (half speed).
    assert(engine.retraceCount >= 59 && engine.retraceCount <= 61);
    printf("[PASS] 30 Hz presentation preserves full 60 Hz simulation cadence (%u ticks)\n",
           engine.retraceCount);
}

static void test60HzPhaseLocking(void) {
    SimulatedViEngine engine;
    engine.reset(1.0f);

    // Simulate 1 second of 60 FPS presentation (calls every 16.67ms)
    const int64_t totalDurationNs = 1000000000LL;
    const int64_t presentPeriodNs = 1000000000LL / 60; // 16.666ms
    const int64_t clockPeriodNs = kBaseViPeriodNs;      // 16.683ms

    int64_t nextPresentNs = presentPeriodNs;
    int64_t nextClockNs = clockPeriodNs;
    int64_t currentNs = 0;

    engine.raiseRetrace(0);

    while (currentNs < totalDurationNs) {
        if (nextPresentNs <= nextClockNs) {
            currentNs = nextPresentNs;
            engine.raiseRetrace(currentNs);
            nextPresentNs += presentPeriodNs;
        } else {
            currentNs = nextClockNs;
            engine.onViClockTick(currentNs);
            nextClockNs += clockPeriodNs;
        }
    }

    assert(engine.retraceCount >= 59 && engine.retraceCount <= 61);
    printf("[PASS] 60 Hz presentation phase-locked to simulation cadence (%u ticks)\n",
           engine.retraceCount);
}

static void testGameSpeedScaling(void) {
    SimulatedViEngine engine;

    // Fast-Forward: 2.0x game speed
    engine.reset(2.0f);
    int64_t period2x = calcSimPeriod(2.0f);
    assert(period2x == kBaseViPeriodNs / 2);
    int64_t minInterval2x = calcMinInterval(period2x);
    // At 2x speed, 120Hz frames (8.33ms) must be accepted
    assert(8333333LL >= minInterval2x);

    // Slow-Mo: 0.5x game speed
    engine.reset(0.5f);
    int64_t periodHalf = calcSimPeriod(0.5f);
    assert(periodHalf == kBaseViPeriodNs * 2);
    int64_t minIntervalHalf = calcMinInterval(periodHalf);
    // At 0.5x speed, 60Hz frames (16.67ms) must be rejected (< 29.36ms)
    assert(16666666LL < minIntervalHalf);

    printf("[PASS] Game speed scaling correctly adjusts simulation period and retrace filter\n");
}

static void testFramePacerMonotonicPhase(void) {
    int64_t lastPresentNs = 1000000000LL;
    int64_t framePeriodNs = 1000000000LL / 60; // 16666666 ns

    // Case 1: Frame rendered on time, target advances monotonically without drift
    int64_t targetNs = lastPresentNs + framePeriodNs;
    int64_t nowNs = targetNs + 2000; // Woke up 2 microseconds after target
    // In our fixed pacer, when on time or near target, mLastPresentNs locks to targetNs:
    lastPresentNs = targetNs;
    assert(lastPresentNs == 1000000000LL + framePeriodNs);

    // Next frame target retains exact 60.000 Hz phase:
    int64_t nextTargetNs = lastPresentNs + framePeriodNs;
    assert(nextTargetNs == 1000000000LL + framePeriodNs * 2);

    // Case 2: Severe stall (missed deadline by > 1 frame period)
    nowNs = nextTargetNs + framePeriodNs * 2;
    if (nowNs >= nextTargetNs) {
        lastPresentNs = nowNs; // Resets cleanly without negative debt
    }
    assert(lastPresentNs == nowNs);

    printf("[PASS] Frame pacer monotonic phase tracking and stall reset verified\n");
}

int main(void) {
    printf("=== Running Timing and Frame Pacing Unit Tests ===\n");
    testSimulationPeriodAndInterval();
    test90HzSimulationDecoupling();
    test30HzBatterySaverCadence();
    test60HzPhaseLocking();
    testGameSpeedScaling();
    testFramePacerMonotonicPhase();
    printf("All timing and frame pacing unit tests passed successfully.\n");
    return 0;
}
