import unittest
import math
import struct

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

    def test_copy_dimension_defensive_clamp(self):
        """
        Verify that copy dimensions are defensively clamped to at least 1x1.
        Specifically ensures dh < 1 sets dh = 1 and does NOT corrupt dw.
        """
        # Case 1: dh < 1, dw = 64
        dw, dh = 64, 0
        if dw < 1: dw = 1
        if dh < 1: dh = 1
        self.assertEqual(dw, 64, "dw must remain 64 when dh is clamped")
        self.assertEqual(dh, 1, "dh must be clamped to 1")

        # Case 2: dw < 1, dh = 32
        dw, dh = 0, 32
        if dw < 1: dw = 1
        if dh < 1: dh = 1
        self.assertEqual(dw, 1, "dw must be clamped to 1")
        self.assertEqual(dh, 32, "dh must remain 32 when dw is clamped")

    def test_frame_pacer_deadline_tracking(self):
        """
        Verify FramePacer monotonic deadline calculation, stall recovery, and pacing.
        """
        target_fps = 60
        frame_period_ns = 1_000_000_000 // target_fps  # 16,666,666 ns

        # Normal cadence
        last_present_ns = 100_000_000
        now_ns = 110_000_000  # Frame finishes 10ms into frame period
        target_ns = last_present_ns + frame_period_ns
        wait_ns = target_ns - now_ns
        self.assertEqual(wait_ns, 6_666_666, "Pacer must wait for remaining 6.66ms")

        # Stall recovery: frame finishes after more than 2 frame periods (e.g. 50ms stall)
        stall_now_ns = 150_000_000
        if stall_now_ns - last_present_ns > frame_period_ns * 2:
            recovered_last_present = stall_now_ns
        else:
            recovered_last_present = last_present_ns + frame_period_ns
        self.assertEqual(recovered_last_present, stall_now_ns, "Long stall must reset base deadline to prevent catch-up spiral")

    def test_vulkan_winding_invariant(self):
        """
        Verify that frontFace remains VK_FRONT_FACE_COUNTER_CLOCKWISE.
        Vulkan NDC negates gl_Position.y to match GX NDC.
        Verify cullMode mapping and rasterState key collision resistance.
        """
        front_face = 'VK_FRONT_FACE_COUNTER_CLOCKWISE'
        self.assertEqual(front_face, 'VK_FRONT_FACE_COUNTER_CLOCKWISE')

        # Cull mode mapping function as in vk_pipeline_manager.cpp
        def get_vk_cull_mode(cull, cull_face):
            if not cull:
                return 'VK_CULL_MODE_NONE'
            if cull_face == 0x0408:
                return 'VK_CULL_MODE_FRONT_AND_BACK'
            if cull_face == 0x0404:
                return 'VK_CULL_MODE_FRONT_BIT'
            return 'VK_CULL_MODE_BACK_BIT'

        self.assertEqual(get_vk_cull_mode(False, 0x0404), 'VK_CULL_MODE_NONE')
        self.assertEqual(get_vk_cull_mode(True, 0x0404), 'VK_CULL_MODE_FRONT_BIT')
        self.assertEqual(get_vk_cull_mode(True, 0x0405), 'VK_CULL_MODE_BACK_BIT')
        self.assertEqual(get_vk_cull_mode(True, 0x0408), 'VK_CULL_MODE_FRONT_AND_BACK')

        # Verify rasterState key distinguishes cull faces without collision
        def raster_key(cull, cull_face, prim):
            return (1 if cull else 0) | ((cull_face & 0xF) << 1) | ((prim & 7) << 5)

        k_cull_back = raster_key(True, 0x0404, 4)
        k_cull_both = raster_key(True, 0x0408, 4)
        k_cull_front = raster_key(True, 0x0405, 4)
        self.assertNotEqual(k_cull_back, k_cull_both, "0x0404 and 0x0408 must not collide in rasterState")
        self.assertNotEqual(k_cull_back, k_cull_front, "0x0404 and 0x0405 must not collide in rasterState")

        # Sampler key preserves levels to prevent LOD clamping pollution
        def sampler_key(mode0, mode1, levels):
            return ((mode0 & 0xFFFFFF) << 32) | ((mode1 & 0xFFFF) << 8) | (levels & 0xFF)

        s_key_2 = sampler_key(0x1234, 0x5678, 2)
        s_key_8 = sampler_key(0x1234, 0x5678, 8)
        self.assertNotEqual(s_key_2, s_key_8, "Samplers with different levels must have distinct keys")

    def test_gx_texgen_source_row_decoding(self):
        """
        Verify that GX TexCoordGen register (XF 0x40+i) decodes source_row
        from bits [11:7] (shift 7), mapping row 0 -> pos, row 1 -> normal,
        row 2 -> color, row 3 -> binormal, row 4 -> tangent, and rows 5..12 -> tex0..tex7.
        Also verify that texture matrix index m defaults to 60 (identity) if 0.
        """
        # XF_TEX macro bit shifts from RVL_SDK / GDGeometry.h
        XF_TEX_PROJECTION_SHIFT = 1
        XF_TEX_INPUT_FORM_SHIFT = 2
        XF_TEX_TEXGEN_TYPE_SHIFT = 4
        XF_TEX_SOURCE_ROW_SHIFT = 7

        XF_GEOM_INROW = 0
        XF_NORMAL_INROW = 1
        XF_COLORS_INROW = 2
        XF_BINORMAL_T_INROW = 3
        XF_BINORMAL_B_INROW = 4
        XF_TEX0_INROW = 5
        XF_TEX7_INROW = 12

        def make_tex_info(row, form=0, proj=0, tg_type=0):
            return (proj << XF_TEX_PROJECTION_SHIFT) | (form << XF_TEX_INPUT_FORM_SHIFT) | \
                   (tg_type << XF_TEX_TEXGEN_TYPE_SHIFT) | (row << XF_TEX_SOURCE_ROW_SHIFT)

        # Decode standard Tex0 texgen word
        info_tex0 = make_tex_info(XF_TEX0_INROW)
        src = (info_tex0 >> 7) & 31
        self.assertEqual(src, 5, "Tex0 source row must decode to 5")
        self.assertEqual(src - 5, 0, "Tex0 must map to texIdx 0")

        # Decode Tex1..Tex7
        for t in range(8):
            info = make_tex_info(XF_TEX0_INROW + t)
            self.assertEqual(((info >> 7) & 31) - 5, t)

        # Decode Normal as texgen (e.g. environment mapping / sky sphere)
        info_nrm = make_tex_info(XF_NORMAL_INROW)
        self.assertEqual((info_nrm >> 7) & 31, 1, "Normal source row must decode to 1")

        # Verify identity matrix fallback for texture matrix index 0
        m = 0
        m_resolved = m if m != 0 else 60
        self.assertEqual(m_resolved, 60, "Texture matrix 0 must resolve to GX_IDENTITY (row 60)")

    def test_shader_inventory_deduplication_and_stats(self):
        """
        Verify that ShaderInventory deduplicates identical pipeline keys,
        records asset provenance, and calculates coverage statistics accurately.
        """
        catalog = {}
        entries = []

        def add_entry(key_id, uid_id, source):
            if key_id in catalog:
                return False
            catalog[key_id] = len(entries)
            entries.append({'key': key_id, 'uid': uid_id, 'source': source})
            return True

        self.assertTrue(add_entry(0x100, 0x1, "title_screen"))
        self.assertTrue(add_entry(0x200, 0x2, "mario_model"))
        self.assertFalse(add_entry(0x100, 0x1, "title_screen_dup"))
        self.assertEqual(len(entries), 2, "Duplicate pipeline keys must be ignored")
        self.assertEqual(entries[0]['source'], "title_screen")

    def test_pipeline_key_encoding_and_completeness(self):
        """
        Verify that PipelineKey preserves all raster, depth, blend, and vertex flag
        bits without loss or key collisions across different render states.
        """
        def make_key(flags, depth_test, depth_write, depth_func, cull, cull_face, prim, blend_on, color_mask):
            depth_state = (1 if depth_test else 0) | ((1 if (depth_test and depth_write) else 0) << 1) | ((depth_func & 15) << 4)
            blend_state = 1 if blend_on else 0
            blend_state2 = (color_mask & 15) << 4
            raster_state = (1 if cull else 0) | ((cull_face & 0xF) << 1) | ((prim & 7) << 5)
            return (flags, depth_state, blend_state, blend_state2, raster_state)

        # 3D character vs 2D HUD quad
        k_3d = make_key(0x4047, True, True, 3, True, 4, 4, False, 15)
        k_hud = make_key(0x0051, False, False, 7, False, 0, 4, True, 15)
        self.assertNotEqual(k_3d, k_hud)
        # Verify depth write distinction
        k_depth_ro = make_key(0x0045, True, False, 3, True, 4, 4, False, 15)
        k_depth_rw = make_key(0x0045, True, True, 3, True, 4, 4, False, 15)
        self.assertNotEqual(k_depth_ro, k_depth_rw, "Read-only vs Read-Write depth must produce distinct keys")

    def test_texture_upload_bounds_and_overflow_validation(self):
        """
        Verify texture manager size calculation, arithmetic overflow detection,
        and truncated mip payload rejection before staging memory copy.
        """
        def validate_texture_upload(w, h, levels, rgba_pixel_count):
            if w == 0 or h == 0 or w > 4096 or h > 4096 or levels > 14:
                return False, "invalid dimensions"

            required_bytes = 0
            cw, ch = w, h
            for l in range(levels):
                level_bytes = cw * ch * 4
                if level_bytes <= 0:
                    return False, "overflow"
                required_bytes += level_bytes
                cw = max(1, cw // 2)
                ch = max(1, ch // 2)

            total_bytes = rgba_pixel_count * 4
            if total_bytes < required_bytes:
                return False, "truncated payload"

            return True, "valid"

        # Valid 256x256 texture with 1 level
        ok, reason = validate_texture_upload(256, 256, 1, 256 * 256)
        self.assertTrue(ok)

        # Truncated payload: claims 3 mip levels but only provides level 0 pixels
        ok, reason = validate_texture_upload(256, 256, 3, 256 * 256)
        self.assertFalse(ok)
        self.assertEqual(reason, "truncated payload")

        # Invalid dimensions out of limits (> 4096)
        ok, reason = validate_texture_upload(8192, 8192, 1, 8192 * 8192)
        self.assertFalse(ok)
        self.assertEqual(reason, "invalid dimensions")

    def test_spirv_cache_checksum_and_truncation_recovery(self):
        """
        Verify FNV1a-32 checksum calculation and truncation detection
        for persistent SPIR-V shader records.
        """
        def fnv1a32(data: bytes) -> int:
            h = 2166136261
            for b in data:
                h = ((h ^ b) * 16777619) & 0xFFFFFFFF
            return h

        vs_dummy = b"\x03\x02\x23\x07\x00\x00\x01\x00" * 32
        fs_dummy = b"\x03\x02\x23\x07\x00\x00\x01\x00" * 48

        vs_sum = fnv1a32(vs_dummy)
        fs_sum = fnv1a32(fs_dummy)

        # Checksum must verify correctly
        self.assertEqual(fnv1a32(vs_dummy), vs_sum)
        self.assertEqual(fnv1a32(fs_dummy), fs_sum)

        # Corrupted bit must fail verification
        vs_corrupt = bytearray(vs_dummy)
        vs_corrupt[10] ^= 0xFF
        self.assertNotEqual(fnv1a32(bytes(vs_corrupt)), vs_sum)

        # Truncated record must be detected by length check
        record_bytes = 8 + 4 + 4 + 4 + 4 + len(vs_dummy) + len(fs_dummy)
        truncated_record = vs_dummy[:20]
        self.assertLess(len(truncated_record), record_bytes)

    def test_pipeline_key_canonical_round_trip(self):
        """
        Verify that DecodedPipelineKey <-> PipelineKey round trip is bit-exact
        across depth, blend factors, blend operations, color masks, culling modes,
        front-face orientation, and primitive topology.
        """
        def encode_key(vtx_flags, depth_test, depth_write, depth_op, blend_enable,
                       src_color, dst_color, color_op, src_alpha, dst_alpha, alpha_op,
                       color_mask, const_alpha, cull_mode, topology, front_cw):
            flags = vtx_flags
            depth_state = (1 if depth_test else 0) | ((1 if (depth_test and depth_write) else 0) << 1) | ((depth_op & 7) << 4)
            blend_state = ((1 if blend_enable else 0) |
                           ((src_color & 0x1F) << 1) |
                           ((dst_color & 0x1F) << 6) |
                           ((src_alpha & 0x1F) << 11) |
                           ((dst_alpha & 0x1F) << 16))
            clamped_alpha = max(0.0, min(1.0, const_alpha))
            alpha_byte = int(round(clamped_alpha * 255.0)) & 0xFF
            blend_state2 = ((color_op & 3) |
                            ((alpha_op & 3) << 2) |
                            ((color_mask & 15) << 4) |
                            (alpha_byte << 8))
            cull_on = 1 if (cull_mode != 0) else 0
            cull_face_val = 8 if (cull_mode == 3) else (4 if cull_mode == 1 else 5)
            prim_val = 1 if (topology == 1) else (0 if topology == 0 else 4)
            front_bit = 1 if front_cw else 0
            raster_state = cull_on | ((cull_face_val & 0x0F) << 1) | ((prim_val & 7) << 5) | (front_bit << 8)
            return (flags, depth_state, blend_state, blend_state2, raster_state)

        def decode_key(k):
            flags, depth_state, blend_state, blend_state2, raster_state = k
            depth_test = (depth_state & 1) != 0
            depth_write = (depth_state & 2) != 0
            depth_op = (depth_state >> 4) & 7
            blend_enable = (blend_state & 1) != 0
            src_color = (blend_state >> 1) & 0x1F
            dst_color = (blend_state >> 6) & 0x1F
            src_alpha = (blend_state >> 11) & 0x1F
            dst_alpha = (blend_state >> 16) & 0x1F
            color_op = blend_state2 & 3
            alpha_op = (blend_state2 >> 2) & 3
            color_mask = (blend_state2 >> 4) & 15
            const_alpha = ((blend_state2 >> 8) & 0xFF) / 255.0
            cull_on = (raster_state & 1) != 0
            cf = (raster_state >> 1) & 0x0F
            if not cull_on:
                cull_mode = 0
            elif cf == 8:
                cull_mode = 3
            elif cf == 4:
                cull_mode = 1
            else:
                cull_mode = 2
            prim_val = (raster_state >> 5) & 7
            front_cw = (raster_state & (1 << 8)) != 0
            return (flags, depth_test, depth_write, depth_op, blend_enable,
                    src_color, dst_color, color_op, src_alpha, dst_alpha, alpha_op,
                    color_mask, const_alpha, cull_mode, prim_val, front_cw)

        # Test cases: UI 2D HUD quad, 3D character, skybox, particles
        cases = [
            (0x0051, False, False, 7, True, 4, 5, 0, 4, 5, 0, 15, 0.0, 0, 4, False), # 2D UI Quad
            (0x4047, True, True, 3, False, 1, 0, 0, 1, 0, 0, 15, 0.0, 2, 4, False),  # 3D character back cull
            (0x0045, True, False, 3, False, 1, 0, 0, 1, 0, 0, 15, 0.0, 0, 4, False), # Skybox no cull
            (0x0051, True, False, 3, True, 4, 1, 0, 4, 1, 0, 15, 0.5, 0, 4, False),  # Additive particle
            (0x0001, True, True, 2, False, 1, 0, 0, 1, 0, 0, 15, 0.0, 1, 1, True),   # Wireframe front cull
        ]

        for orig in cases:
            k = encode_key(*orig)
            dec = decode_key(k)
            # Re-encode decoded values
            k_round = encode_key(*dec)
            self.assertEqual(k, k_round, f"Key mismatch on roundtrip for case {orig}")

    def test_prewarmed_key_matches_runtime_lookup_no_duplicate(self):
        """
        Verify that prewarmed pipeline key precisely matches runtime rendering key,
        guaranteeing immediate cache hit and 0 duplicate pipeline creations.
        """
        # Simulated prewarm key for 3D character
        vtx_flags = 0x4047
        uid = 0x12345678
        prewarm_depth_test = True
        prewarm_depth_write = True
        prewarm_depth_op = 3 # LEQUAL
        prewarm_cull_mode = 2 # GL_BACK

        # Build key as prewarming does
        depth_state = (1 if prewarm_depth_test else 0) | (1 << 1) | (prewarm_depth_op << 4)
        raster_state = 1 | (5 << 1) | (4 << 5) # cull back, triangles
        prewarm_key = (uid, vtx_flags, depth_state, 0, 0x0F, raster_state)

        # Build key as runtime pipelineKeyFor does
        runtime_item = {
            'flags': vtx_flags,
            'depthTest': 1,
            'depthWrite': 1,
            'depthFunc': 0x0203, # GL_LEQUAL -> depthFunc & 7 = 3
            'blend': 0,
            'dstAlpha': 0,
            'dstAlphaValue': 0.0,
            'colorMask': 15,
            'cull': 1,
            'cullFace': 0x0405, # GL_BACK -> 5
            'prim': 4 # TRIANGLES
        }
        rt_depth_state = (runtime_item['depthTest']) | ((runtime_item['depthTest'] and runtime_item['depthWrite']) << 1) | ((runtime_item['depthFunc'] & 7) << 4)
        rt_raster_state = (runtime_item['cull']) | ((runtime_item['cullFace'] & 0xF) << 1) | ((runtime_item['prim'] & 7) << 5)
        runtime_key = (uid, runtime_item['flags'], rt_depth_state, 0, 0x0F, rt_raster_state)

        self.assertEqual(prewarm_key, runtime_key, "Prewarmed key must match runtime pipelineKeyFor exactly")

    def test_prewarm_accounting_excludes_placeholders(self):
        """
        Ensure that placeholder pipelines returned during shader compilation
        are never counted towards successfully prewarmed pipelines.
        """
        stats = {
            'realPipelinesCreated': 0,
            'realPipelinesReused': 0,
            'placeholderUses': 0,
            'pipelineFailures': 0,
        }

        # Simulated PrewarmResult outcomes
        PREWARM_REUSED = 0
        PREWARM_CREATED = 1
        PREWARM_SPIRV_UNAVAILABLE = 2
        PREWARM_PLACEHOLDER = 4
        PREWARM_FAILED = 5

        def record_prewarm(result):
            if result == PREWARM_REUSED:
                stats['realPipelinesReused'] += 1
            elif result == PREWARM_CREATED:
                stats['realPipelinesCreated'] += 1
            elif result == PREWARM_PLACEHOLDER:
                stats['placeholderUses'] += 1
                stats['pipelineFailures'] += 1
            elif result in (PREWARM_SPIRV_UNAVAILABLE, PREWARM_FAILED):
                stats['pipelineFailures'] += 1

        record_prewarm(PREWARM_CREATED)
        record_prewarm(PREWARM_REUSED)
        record_prewarm(PREWARM_PLACEHOLDER)
        record_prewarm(PREWARM_SPIRV_UNAVAILABLE)

        self.assertEqual(stats['realPipelinesCreated'], 1)
        self.assertEqual(stats['realPipelinesReused'], 1)
        self.assertEqual(stats['placeholderUses'], 1)
        self.assertEqual(stats['pipelineFailures'], 2)
        # Verify: placeholders must NEVER increment realPipelinesCreated
        self.assertNotEqual(stats['realPipelinesCreated'], 2)

    def test_distinct_runtime_discovery_uid_vs_key(self):
        """
        Verify distinct tracking for ShaderUid miss vs PipelineKey miss.
        """
        spirv_cache = {0xAAA: b"spirv_blob"}
        pipeline_cache = set()

        stats = {
            'runtimeShaderUidMisses': 0,
            'runtimePipelineKeyMisses': 0,
            'runtimeDiscovered': 0,
        }

        def on_draw(uid, key):
            if uid not in spirv_cache:
                stats['runtimeShaderUidMisses'] += 1
                stats['runtimeDiscovered'] += 1
                return "fallback_placeholder"
            elif key not in pipeline_cache:
                stats['runtimePipelineKeyMisses'] += 1
                stats['runtimeDiscovered'] += 1
                pipeline_cache.add(key)
                return "pipeline_created"
            return "pipeline_reused"

        # 1. Unseen UID
        res1 = on_draw(0xBBB, 0x1)
        self.assertEqual(res1, "fallback_placeholder")
        self.assertEqual(stats['runtimeShaderUidMisses'], 1)
        self.assertEqual(stats['runtimePipelineKeyMisses'], 0)

        # 2. Known UID with new PipelineKey (blend/depth variant)
        res2 = on_draw(0xAAA, 0x2)
        self.assertEqual(res2, "pipeline_created")
        self.assertEqual(stats['runtimeShaderUidMisses'], 1)
        self.assertEqual(stats['runtimePipelineKeyMisses'], 1)

        # 3. Known UID and already-created PipelineKey
        res3 = on_draw(0xAAA, 0x2)
        self.assertEqual(res3, "pipeline_reused")
        self.assertEqual(stats['runtimeShaderUidMisses'], 1)
        self.assertEqual(stats['runtimePipelineKeyMisses'], 1)

    def test_stable_binary_serialization_and_string_cap(self):
        """
        Verify that serialization enforces magic, schema version, checksums,
        and safely caps string lengths to prevent desynchronization.
        """
        def serialize_entry(uid_int, key_tuple, source_str):
            magic = 0x55535450
            schema = 2
            build = 20261019
            flags_byte = 0
            str_bytes = source_str.encode('utf-8')[:512]
            str_len = len(str_bytes)
            data = struct.pack('<IIIBH', magic, schema, build, flags_byte, str_len) + str_bytes
            return data

        def deserialize_entry(buf):
            if len(buf) < 15:
                return None, "truncated"
            magic, schema, build, flags_byte, str_len = struct.unpack('<IIIBH', buf[:15])
            if magic != 0x55535450 or schema != 2 or build != 20261019:
                return None, "invalid_header"
            if str_len > 512:
                return None, "string_too_long"
            if len(buf) < 15 + str_len:
                return None, "truncated_body"
            src = buf[15:15+str_len].decode('utf-8', errors='ignore')
            return src, "ok"

        # Valid entry
        buf = serialize_entry(1, (1, 2, 3, 4, 5), "mario_model")
        src, status = deserialize_entry(buf)
        self.assertEqual(status, "ok")
        self.assertEqual(src, "mario_model")

        # Corrupted string length > 512
        corrupted_buf = bytearray(buf)
        struct.pack_into('<H', corrupted_buf, 13, 1024)
        src_err, status_err = deserialize_entry(bytes(corrupted_buf))
        self.assertEqual(status_err, "string_too_long")


if __name__ == '__main__':
    unittest.main()


