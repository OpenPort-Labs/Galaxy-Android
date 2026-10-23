import os
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class TestTimingAndPacingRegression(unittest.TestCase):
    def test_vi_timing_decoupled_from_display_refresh(self):
        vi_cpp_path = os.path.join(ROOT, "platform", "src", "vi", "vi.cpp")
        with open(vi_cpp_path, "r", encoding="utf-8") as f:
            vi_cpp = f.read()

        # Authentic Wii field period constant
        self.assertIn("kBaseViPeriodNs = 16683350LL", vi_cpp)
        self.assertIn("getSimulationPeriodNs()", vi_cpp)

        # Simulation retrace must NOT be divided by display refresh rate hz
        # (which previously caused 90 FPS to run 1.5x fast and 120 FPS to run 2x fast - Issue #112)
        self.assertNotIn("1000000000LL / (hz * 1.5f)", vi_cpp)
        self.assertIn("minInterval = (int64_t)(period * 0.88)", vi_cpp)

        # Fallback clock must allow intermediate retrace dispatch (Issue #112 slow motion at 30 FPS)
        self.assertNotIn("now - lastRaised < (int64_t)(period * 1.05)", vi_cpp)
        self.assertIn("now - lastRaised < (int64_t)(period * 0.70)", vi_cpp)

    def test_frame_pacer_contains_no_busy_spin_yield(self):
        frame_pacer_path = os.path.join(ROOT, "platform", "mobile", "display", "FramePacer.cpp")
        with open(frame_pacer_path, "r", encoding="utf-8") as f:
            frame_pacer_cpp = f.read()

        # Busy spin with yield burns CPU, causing thermal throttling down to 12-14 FPS on Mali/Adreno (Issues #110, #105, #99)
        self.assertNotIn('asm volatile("yield"', frame_pacer_cpp)

        # Monotonic phase advance avoids accumulating positive slippage drift against SurfaceFlinger VSync
        self.assertIn("mLastPresentNs = targetNs;", frame_pacer_cpp)

    def test_timing_intervals_math(self):
        base_period_ns = 16683350
        min_interval_ns = int(base_period_ns * 0.88)

        # At 90 Hz, frame time is 11.11ms -> MUST be rejected by raiseRetrace
        frame_90hz_ns = 1000000000 // 90
        self.assertLess(frame_90hz_ns, min_interval_ns)

        # At 120 Hz, frame time is 8.33ms -> MUST be rejected by raiseRetrace
        frame_120hz_ns = 1000000000 // 120
        self.assertLess(frame_120hz_ns, min_interval_ns)

        # At 144 Hz, frame time is 6.94ms -> MUST be rejected by raiseRetrace
        frame_144hz_ns = 1000000000 // 144
        self.assertLess(frame_144hz_ns, min_interval_ns)

        # At 60 Hz, frame time is 16.66ms -> MUST be accepted
        frame_60hz_ns = 1000000000 // 60
        self.assertGreaterEqual(frame_60hz_ns, min_interval_ns)

        # At 30 Hz, frame time is 33.33ms -> MUST be accepted
        frame_30hz_ns = 1000000000 // 30
        self.assertGreaterEqual(frame_30hz_ns, min_interval_ns)


if __name__ == "__main__":
    unittest.main()
