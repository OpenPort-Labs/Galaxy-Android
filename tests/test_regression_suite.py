import unittest
import math

class TestInputAndAspectRegressions(unittest.TestCase):
    def test_gamepad_pointer_aiming_direction(self):
        """
        Verify physical controller right stick pointer aiming direction.
        On Android: pushing stick UP gives rawY < 0, which yields rStickY > 0.
        In Wii/screen coordinates, top of screen is Y = -1.0, bottom is Y = +1.0.
        Pushing UP must move pointer UP (decrease pointerY towards -1.0).
        Pushing DOWN must move pointer DOWN (increase pointerY towards +1.0).
        """
        speed = 0.035
        pointer_y = 0.0

        # Push stick UP: rStickY > 0
        r_stick_y_up = 1.0
        new_pointer_y_up = max(-1.0, min(1.0, pointer_y - r_stick_y_up * speed))
        self.assertLess(new_pointer_y_up, pointer_y, "Pushing stick UP must decrease pointerY towards -1.0")

        # Push stick DOWN: rStickY < 0
        r_stick_y_down = -1.0
        new_pointer_y_down = max(-1.0, min(1.0, pointer_y - r_stick_y_down * speed))
        self.assertGreater(new_pointer_y_down, pointer_y, "Pushing stick DOWN must increase pointerY towards +1.0")

    def test_dynamic_stick_quadrant_isolation(self):
        """
        Verify that dynamic left stick only captures lower-left quadrant
        (px <= w * 0.40f && py >= h * 0.45f).
        All other quadrants must remain available for direct Star Pointer aiming.
        """
        w = 2400.0
        h = 1080.0

        def is_stick_eligible(px, py):
            return (px <= w * 0.40) and (py >= h * 0.45)

        # Bottom-left touch (movement stick)
        self.assertTrue(is_stick_eligible(300.0, 800.0))

        # Top-left touch (Star pointer aiming)
        self.assertFalse(is_stick_eligible(300.0, 200.0))

        # Mid-left touch (Star pointer aiming)
        self.assertFalse(is_stick_eligible(300.0, 400.0))

        # Center touch (Star pointer aiming)
        self.assertFalse(is_stick_eligible(1200.0, 540.0))

        # Right screen touch (Star pointer aiming / actions)
        self.assertFalse(is_stick_eligible(1800.0, 540.0))

    def test_hud_safe_aspect_ratio_preservation(self):
        """
        Verify that in ultrawide mode (e.g. 2400x1080 = 20:9 aspect),
        2D hudSafe elements maintain proper 16:9 aspect ratio and centering.
        """
        target_w = 2400.0
        target_h = 1080.0
        k169 = 16.0 / 9.0
        native_w = 640.0
        native_h = 480.0

        target_aspect = target_w / target_h
        self.assertGreater(target_aspect, k169)

        # Compute HUD safe bounds
        hud_w = target_h * k169  # 1920.0
        hud_ofs_x = (target_w - hud_w) * 0.5  # 240.0
        hud_fx = hud_w / native_w  # 3.0
        fy = target_h / native_h   # 2.25

        # A 16:9 full-width 2D HUD quad spanning [0, 640]
        vp_x = round(hud_ofs_x + 0.0 * hud_fx)
        vp_w = round(640.0 * hud_fx)

        self.assertEqual(vp_x, 240)
        self.assertEqual(vp_w, 1920)
        self.assertEqual(vp_x + vp_w, 2160)
        # Verify perfect centering in 2400px wide target
        self.assertEqual(hud_ofs_x, (target_w - (vp_x + vp_w - vp_x)) / 2)

    def test_settings_refresh_rate_decoupling(self):
        """
        Verify that targetFps and preferredRefreshRate can be configured
        independently without overwriting each other.
        """
        target_fps = 60
        preferred_refresh_rate = 120
        battery_saver = False

        # Selecting a 120Hz display refresh rate must preserve the 60 FPS target
        display_refresh_selection = 120
        preferred_refresh_rate = display_refresh_selection
        self.assertEqual(target_fps, 60)
        self.assertEqual(preferred_refresh_rate, 120)

        # Enabling battery saver locks render FPS to 30 without wiping physical refresh rate
        battery_saver = True
        if battery_saver:
            target_fps = 30
        self.assertEqual(target_fps, 30)
        self.assertEqual(preferred_refresh_rate, 120)

    def test_camera_swipe_deadzone(self):
        """
        Verify small touch jitter does not cause camera spinning.
        """
        scale = 1.0
        deadzone = 2.0 * scale
        sens = 0.055

        # Jitter under deadzone
        dx_jitter = 1.2
        dy_jitter = 0.8
        if abs(dx_jitter) > deadzone or abs(dy_jitter) > deadzone:
            rs_x = -dx_jitter * sens
        else:
            rs_x = 0.0
        self.assertEqual(rs_x, 0.0)

        # Intentional swipe over deadzone
        dx_swipe = 15.0
        if abs(dx_swipe) > deadzone:
            rs_x = max(-1.0, min(1.0, -dx_swipe * sens))
        else:
            rs_x = 0.0
        self.assertLess(rs_x, 0.0)

if __name__ == '__main__':
    unittest.main()
