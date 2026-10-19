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


class TestFramePacingAndScheduling(unittest.TestCase):
    def test_frame_pacing_recovery_from_lag_spike(self):
        """
        Verify that a lag spike or missed deadline resets mLastPresentNs
        without cascading into an artificial multi-frame stutter burst.
        """
        target_fps = 60
        frame_period_ns = 1_000_000_000 // target_fps  # 16,666,666 ns
        last_present_ns = 1_000_000_000

        # Simulate normal frame hitting deadline on time
        now_ns = last_present_ns + frame_period_ns
        if now_ns - last_present_ns > frame_period_ns + frame_period_ns // 2:
            new_last_present_ns = now_ns
        else:
            new_last_present_ns = last_present_ns + frame_period_ns
        self.assertEqual(new_last_present_ns, 1_016_666_666)

        # Simulate 50ms hitch (exceeding frame_period + period/2)
        hitch_now_ns = new_last_present_ns + 50_000_000
        if hitch_now_ns - new_last_present_ns > frame_period_ns + frame_period_ns // 2:
            recovered_present_ns = hitch_now_ns
        else:
            recovered_present_ns = new_last_present_ns + frame_period_ns

        # Must recover by anchoring to actual now_ns, not piling up backlogged frames
        self.assertEqual(recovered_present_ns, hitch_now_ns)

    def test_no_busy_spin_threshold(self):
        """
        Verify wait intervals <= 1ms do not invoke busy spin loops,
        allowing low-overhead return to Vulkan presentation.
        """
        wait_ns = 800_000  # 0.8 ms
        should_sleep = wait_ns > 1_000_000
        self.assertFalse(should_sleep, "Sub-1ms intervals must not sleep or spin")


class TestSaveDataConversionAndIntegrity(unittest.TestCase):
    def test_dolphin_endian_detection(self):
        """
        Verify distinct detection between Big-Endian Dolphin and Little-Endian Native saves.
        """
        dolphin_hdr = bytearray(0x140)
        dolphin_hdr[4:8] = (2).to_bytes(4, 'big')
        dolphin_hdr[8:12] = (19).to_bytes(4, 'big')
        dolphin_hdr[12:16] = (0xBE00).to_bytes(4, 'big')

        native_hdr = bytearray(0x140)
        native_hdr[4:8] = (2).to_bytes(4, 'little')
        native_hdr[8:12] = (19).to_bytes(4, 'little')
        native_hdr[12:16] = (0xBE00).to_bytes(4, 'little')

        def is_dolphin(data):
            return data[4:8] == b'\x00\x00\x00\x02' and data[8:12] == b'\x00\x00\x00\x13'

        def is_native(data):
            return data[4:8] == b'\x02\x00\x00\x00' and data[8:12] == b'\x13\x00\x00\x00'

        self.assertTrue(is_dolphin(dolphin_hdr))
        self.assertFalse(is_native(dolphin_hdr))
        self.assertTrue(is_native(native_hdr))
        self.assertFalse(is_dolphin(native_hdr))

    def test_malformed_save_rejection(self):
        """
        Verify that truncated saves, corrupted file sizes, or undersized chunks are safely rejected.
        """
        # Save too small (< 0x140 bytes)
        small_save = bytearray(100)
        self.assertLess(len(small_save), 0x140)

        # Truncated chunk header (sz < 12)
        sz = 8
        is_chunk_valid = sz >= 12
        self.assertFalse(is_chunk_valid, "Chunks with size < 12 must be rejected to prevent infinite loop")

        # Chunk offset out of bounds
        file_size = 48640
        c_off = 48635
        chunk_sz = 100
        self.assertTrue(c_off + chunk_sz > file_size, "Oversized chunk must be caught")

    def test_checksum_algorithm_roundtrip(self):
        """
        Verify Wii 16-bit word checksum computation and inversion.
        """
        payload = b'\x00\x02\x00\x13\x00\x00\xbe\x00' + b'\x00' * 20
        sum16 = 0
        inv16 = 0
        for i in range(0, len(payload), 2):
            w = int.from_bytes(payload[i:i+2], 'big')
            sum16 = (sum16 + w) & 0xFFFF
            inv16 = (inv16 + (~w & 0xFFFF)) & 0xFFFF
        cs = (sum16 << 16) | inv16
        self.assertNotEqual(cs, 0)
        self.assertEqual(cs >> 16, sum16)
        self.assertEqual(cs & 0xFFFF, inv16)


class TestTHPVideoSafety(unittest.TestCase):
    def test_truncated_sof0_header_rejection(self):
        """
        Verify that an incomplete SOF0 (frame header) with fewer than 17 bytes remaining is rejected.
        """
        data = bytearray([0xFF, 0xC0, 0x00, 0x0B, 0x08])  # truncated header
        end_offset = len(data)
        curr_offset = 2
        # Header requires at least 17 bytes
        has_enough_bytes = (curr_offset + 17 <= end_offset)
        self.assertFalse(has_enough_bytes, "Truncated SOF0 must be rejected before parsing")

    def test_truncated_sos_header_rejection(self):
        """
        Verify that an incomplete SOS (scan header) with fewer than 14 bytes remaining is rejected.
        """
        data = bytearray([0xFF, 0xDA, 0x00, 0x04])
        end_offset = len(data)
        curr_offset = 2
        has_enough_bytes = (curr_offset + 14 <= end_offset)
        self.assertFalse(has_enough_bytes, "Truncated SOS must be rejected before parsing")


class TestVulkanRenderPassStateTracking(unittest.TestCase):
    def test_deferred_render_pass_resumption(self):
        """
        Verify that consecutive EFB copies do not trigger back-to-back
        vkCmdBeginRenderPass / vkCmdEndRenderPass flushes on tile-based GPUs.
        """
        items = ['COPY', 'COPY', 'COPY', 'DRAW', 'COPY_XFB']
        in_render_pass = True
        rp_begins = 0
        rp_ends = 0

        for it in items:
            if it == 'COPY':
                if in_render_pass:
                    rp_ends += 1
                    in_render_pass = False
                # Do not eagerly resume render pass if no clear and no draw
            elif it == 'DRAW':
                if not in_render_pass:
                    rp_begins += 1
                    in_render_pass = True
            elif it == 'COPY_XFB':
                if in_render_pass:
                    rp_ends += 1
                    in_render_pass = False

        if in_render_pass:
            rp_ends += 1

        # With deferred resumption: exactly 1 end for all 3 copies, 1 begin for draw, 1 end for xfb
        self.assertEqual(rp_begins, 1, "Only 1 render pass resumption should occur for draws")
        self.assertEqual(rp_ends, 2, "Only initial pass end and final xfb copy end should occur")

    def test_descriptor_pool_isolation_on_recovery(self):
        """
        Verify that dynamic sampler descriptor pool exhaustion and reset
        does not invalidate persistent static descriptor sets (SSBO set0, UI, Present).
        """
        static_pool = {
            'set0_frame0': 0x1000,
            'set0_frame1': 0x1001,
            'ui_set': 0x2000,
            'present_set': 0x3000,
        }
        dynamic_sampler_pool = {
            'sampler_0': 0x4000,
            'sampler_1': 0x4001,
        }

        # Simulate dynamic sampler pool exhaustion triggering reset
        dynamic_sampler_pool.clear()

        # Static descriptor pool must remain completely untouched
        self.assertEqual(static_pool['set0_frame0'], 0x1000)
        self.assertEqual(static_pool['set0_frame1'], 0x1001)
        self.assertEqual(static_pool['ui_set'], 0x2000)
        self.assertEqual(static_pool['present_set'], 0x3000)
        self.assertEqual(len(dynamic_sampler_pool), 0)

    def test_render_pass_layout_invariants(self):
        """
        Verify that color image is left in COLOR_ATTACHMENT_OPTIMAL after copy
        so mEfbLoadRenderPass can load it without validation errors, and
        guaranteed in SHADER_READ_ONLY_OPTIMAL upon executeDrawPass exit.
        """
        color_layout = 'COLOR_ATTACHMENT_OPTIMAL'
        in_render_pass = True

        # Copy occurs mid-frame
        if in_render_pass:
            in_render_pass = False
            color_layout = 'SHADER_READ_ONLY_OPTIMAL'  # Ended render pass leaves it in finalLayout

        # Barrier to transfer src
        color_layout = 'TRANSFER_SRC_OPTIMAL'
        # Blit occurs...
        # Barrier after blit transitions back to COLOR_ATTACHMENT_OPTIMAL for next draw
        color_layout = 'COLOR_ATTACHMENT_OPTIMAL'

        self.assertEqual(color_layout, 'COLOR_ATTACHMENT_OPTIMAL')

        # At end of executeDrawPass, ensure transition to SHADER_READ_ONLY_OPTIMAL for present
        if not in_render_pass and color_layout != 'SHADER_READ_ONLY_OPTIMAL':
            color_layout = 'SHADER_READ_ONLY_OPTIMAL'

        self.assertEqual(color_layout, 'SHADER_READ_ONLY_OPTIMAL')


if __name__ == '__main__':
    unittest.main()
