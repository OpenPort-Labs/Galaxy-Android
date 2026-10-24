#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "vi/vi_timing.h"
#include "FramePacerTiming.h"

// These tests drive the same inline decisions vi.cpp and FramePacer.cpp use,
// so a change to the shipped thresholds or windows fails here.

struct SimulatedViEngine {
    int64_t raisedAt = 0;
    float speedMultiplier = 1.0f;
    uint32_t retraceCount = 0;

    void reset(float speed = 1.0f) {
        raisedAt = 0;
        speedMultiplier = speed;
        retraceCount = 0;
    }

    int64_t simPeriod() const {
        return port_vi_timing::simulationPeriodNs(speedMultiplier);
    }

    bool raiseRetrace(int64_t nowNs) {
        if (!port_vi_timing::retraceAllowed(nowNs, raisedAt, simPeriod())) {
            return false;
        }
        raisedAt = nowNs;
        retraceCount++;
        return true;
    }

    bool onViClockTick(int64_t nowNs) {
        if (!port_vi_timing::fallbackTickDue(nowNs, raisedAt, simPeriod())) {
            return false;
        }
        return raiseRetrace(nowNs);
    }
};

// Interleaves presentation flips at presentFps with the free-running fallback
// clock, exactly as the render thread and viClock do at runtime.
static uint32_t runOneSecond(int presentFps, float speed = 1.0f) {
    SimulatedViEngine engine;
    engine.reset(speed);

    const int64_t totalDurationNs = 1000000000LL;
    const int64_t presentPeriodNs = 1000000000LL / presentFps;
    int64_t nextPresentNs = presentPeriodNs;
    int64_t nextClockNs = engine.simPeriod();
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
            nextClockNs = port_vi_timing::advanceDeadline(nextClockNs, currentNs, engine.simPeriod());
        }
    }
    return engine.retraceCount;
}

static void testSimulationPeriodAndInterval(void) {
    int64_t period60 = port_vi_timing::simulationPeriodNs(1.0f);
    assert(period60 == port_vi_timing::kBaseViPeriodNs);
    assert(period60 == 16683350LL);

    int64_t minInterval60 = port_vi_timing::minRetraceIntervalNs(period60);
    // 88% of 16.683ms is ~14.68ms
    assert(minInterval60 == 14681348LL);

    // 90/120/144 Hz display frames arrive faster than the Wii field rate and
    // must be rejected, or the game runs in fast forward (Issue #112).
    assert(1000000000LL / 90 < minInterval60);
    assert(1000000000LL / 120 < minInterval60);
    assert(1000000000LL / 144 < minInterval60);

    // 60/30 Hz frames satisfy the cadence window.
    assert(1000000000LL / 60 >= minInterval60);
    assert(1000000000LL / 30 >= minInterval60);

    // The 2 ms floor still bounds pathological speed multipliers.
    assert(port_vi_timing::minRetraceIntervalNs(1000000LL) == 2000000LL);
    printf("[PASS] Simulation period invariants and refresh rate rejection thresholds verified\n");
}

static void test90HzSimulationDecoupling(void) {
    uint32_t ticks = runOneSecond(90);
    assert(ticks >= 59 && ticks <= 61);
    printf("[PASS] 90 Hz presentation simulation rate clamped to 60 Hz simulation cadence (%u ticks)\n",
           ticks);
}

static void test30HzBatterySaverCadence(void) {
    uint32_t ticks = runOneSecond(30);
    assert(ticks >= 59 && ticks <= 61);
    printf("[PASS] 30 Hz presentation preserves full 60 Hz simulation cadence (%u ticks)\n", ticks);
}

static void test60HzPhaseLocking(void) {
    uint32_t ticks = runOneSecond(60);
    assert(ticks >= 59 && ticks <= 61);
    printf("[PASS] 60 Hz presentation phase-locked to simulation cadence (%u ticks)\n", ticks);
}

static void testGameSpeedScaling(void) {
    int64_t period2x = port_vi_timing::simulationPeriodNs(2.0f);
    assert(period2x == port_vi_timing::kBaseViPeriodNs / 2);
    // At 2x speed, 120 Hz frames must be accepted.
    assert(8333333LL >= port_vi_timing::minRetraceIntervalNs(period2x));

    int64_t periodHalf = port_vi_timing::simulationPeriodNs(0.5f);
    assert(periodHalf == port_vi_timing::kBaseViPeriodNs * 2);
    // At 0.5x speed, 60 Hz frames arrive too early and must be rejected.
    assert(16666666LL < port_vi_timing::minRetraceIntervalNs(periodHalf));

    // Sub-0.1 multipliers are treated as normal speed.
    assert(port_vi_timing::simulationPeriodNs(0.0f) == port_vi_timing::kBaseViPeriodNs);

    // A 2x-speed second must produce ~120 retraces, not 60.
    uint32_t ticks = runOneSecond(60, 2.0f);
    assert(ticks >= 119 && ticks <= 121);
    printf("[PASS] Game speed scaling correctly adjusts simulation period and retrace filter\n");
}

static void testFramePacerMonotonicPhase(void) {
    using namespace mobile_display::pacing;

    assert(clampTargetFps(15) == 30);
    assert(clampTargetFps(240) == 144);
    assert(framePeriodNs(60) == 16666666);

    int64_t period = framePeriodNs(clampTargetFps(60));
    int64_t last = 1000000000LL;
    int64_t target = nextDeadline(last, period);

    // Reaching the pacer before the deadline: it sleeps to the target and
    // anchors to the exact deadline, so no drift accumulates.
    int64_t onTime = target - 2000;
    assert(!phaseLost(onTime, last, period));
    int64_t settled = settlePhase(onTime, last, period);
    assert(settled == target);
    assert(nextDeadline(settled, period) == last + period * 2);

    // The deadline was already missed at decision time: re-anchor to now
    // instead of inheriting the old phase (negative debt).
    int64_t missed = target + 2000;
    assert(!phaseLost(missed, last, period));
    assert(settlePhase(missed, last, period) == missed);

    // A one-frame overrun still has usable phase and re-anchors to now.
    int64_t late = target + period;
    assert(!phaseLost(late, last, period));
    assert(settlePhase(late, last, period) == late);

    // Missing by more than two frames loses phase entirely.
    int64_t stalled = target + period * 2 + 1;
    assert(phaseLost(stalled, last, period));
    assert(settlePhase(stalled, last, period) == stalled);

    // Backwards or uninitialized anchors reset as well.
    assert(phaseLost(last - 1, last, period));
    assert(phaseLost(target, 0, period));

    // Catch-up slots exist only above 60 Hz.
    assert(!presentSlotDue(last + 1000, last, 60));
    assert(!presentSlotDue(last + period * 2, last, 60));
    assert(presentSlotDue(last + period, last, 120));
    assert(!presentSlotDue(last + 1000, last, 120));

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
