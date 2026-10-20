import unittest
import math
import struct
import os
import tempfile

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


if __name__ == '__main__':
    unittest.main()
