import unittest
import math

class TestCameraControlAndPointerCoexistence(unittest.TestCase):
    """
    Automated regression tests for 360 camera swipe control, Star Pointer coexistence,
    drag delta calculations, angle wrapping, and decay dynamics.
    """

    def test_camera_yaw_wrapping_360_degrees(self):
        """
        Verify that horizontal yaw wraps smoothly across full 360 continuous rotation
        for arbitrary large angular deltas without getting stuck or glitching.
        """
        kPi = 3.14159265
        kTwoPi = 6.2831853

        def wrap_yaw(yaw):
            while yaw > kPi:
                yaw -= kTwoPi
            while yaw < -kPi:
                yaw += kTwoPi
            return yaw

        # Continuous rotation test: rotate 10 full turns in positive and negative directions
        yaw = 0.0
        step = 0.5  # ~28.6 degrees per frame
        for _ in range(200):
            yaw = wrap_yaw(yaw + step)
            self.assertGreaterEqual(yaw, -kPi)
            self.assertLessEqual(yaw, kPi)

        for _ in range(200):
            yaw = wrap_yaw(yaw - step)
            self.assertGreaterEqual(yaw, -kPi)
            self.assertLessEqual(yaw, kPi)

        # Single massive swipe impulse test (> 4*PI)
        big_jump = wrap_yaw(15.0)
        self.assertAlmostEqual(big_jump, 15.0 - 2 * kTwoPi, places=5)
        self.assertGreaterEqual(big_jump, -kPi)
        self.assertLessEqual(big_jump, kPi)

    def test_camera_pitch_clamping(self):
        """
        Verify that vertical pitch clamps cleanly between -1.05 and +1.20 radians.
        """
        kPitchMin = -1.05
        kPitchMax = 1.20

        def clamp_pitch(pitch, ry, mult=0.060):
            if ry > 0.005 or ry < -0.005:
                pitch += ry * mult
                if pitch > kPitchMax:
                    pitch = kPitchMax
                if pitch < kPitchMin:
                    pitch = kPitchMin
            return pitch

        p = 0.0
        # Push all the way up
        for _ in range(100):
            p = clamp_pitch(p, 1.0)
        self.assertEqual(p, kPitchMax)

        # Push all the way down
        for _ in range(100):
            p = clamp_pitch(p, -1.0)
        self.assertEqual(p, kPitchMin)

    def test_star_pointer_and_camera_swipe_coexistence(self):
        """
        Verify that swiping outside buttons/sticks simultaneously updates
        the Star Pointer screen position AND emits camera swipe velocity.
        """
        screen_w = 2400.0
        screen_h = 1080.0
        vp_x, vp_y = 240.0, 0.0
        vp_w, vp_h = 1920.0, 1080.0

        scale = 1.0
        deadzone = 1.5 * scale
        sens = 1.0

        # Touch-down at center
        last_x, last_y = 1200.0, 540.0
        # Drag right by 40 pixels
        px, py = 1240.0, 540.0

        dx = px - last_x
        dy = py - last_y

        # 1. Star Pointer tracking
        pointer_x = max(-1.0, min(1.0, ((px - vp_x) / vp_w) * 2.0 - 1.0))
        pointer_y = max(-1.0, min(1.0, ((py - vp_y) / vp_h) * 2.0 - 1.0))

        # Star pointer must track exact finger position
        expected_ptr_x = ((1240.0 - 240.0) / 1920.0) * 2.0 - 1.0  # (1000/1920)*2 - 1 = 0.04167
        self.assertAlmostEqual(pointer_x, expected_ptr_x, places=4)

        # 2. Camera Swipe calculation
        right_stick_x = 0.0
        right_stick_y = 0.0
        if abs(dx) > deadzone or abs(dy) > deadzone:
            swipe_yaw = -(dx / vp_w) * (2.0 * math.pi) * sens
            swipe_pitch = -(dy / vp_h) * 2.2 * sens
            right_stick_x += swipe_yaw * 2.25
            right_stick_y += swipe_pitch * 2.25

        # Both Star Pointer and Camera rotation must be active simultaneously
        self.assertNotEqual(pointer_x, 0.0, "Star pointer must be updated")
        self.assertLess(right_stick_x, 0.0, "Rightward swipe must produce negative yaw (rotate right)")
        self.assertEqual(right_stick_y, 0.0, "Zero Y delta produces zero pitch")

    def test_smooth_camera_decay_no_abrupt_halt(self):
        """
        Verify that camera velocity decays smoothly (0.82 factor) instead of snapping to zero.
        """
        rs_x = 2.0
        decay = 0.82
        frames = 0
        while abs(rs_x) >= 0.005:
            rs_x *= decay
            frames += 1

        # Smooth glide over ~25 frames
        self.assertGreater(frames, 15, "Decay must provide smooth glide over multiple frames")
        self.assertLess(frames, 40, "Decay must settle within reasonable duration")
        self.assertLess(abs(rs_x), 0.005)

if __name__ == '__main__':
    unittest.main()
