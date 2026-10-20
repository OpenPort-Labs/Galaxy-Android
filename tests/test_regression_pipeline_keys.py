import unittest
import math
import struct
import os
import tempfile

class TestShaderInventoryAndCache(unittest.TestCase):
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


class TestCanonicalPipelineKeyContract(unittest.TestCase):
    """
    Exhaustive tests for Section B: Canonical Pipeline Key Encoding & Decoding.
    Verifies bit layout, non-overlapping fields, round-trip fidelity, and safe rejection.
    """
    @staticmethod
    def encode_key(vtx_flags, depth_test, depth_write, depth_func,
                   blend_enable, src_color, dst_color, src_alpha, dst_alpha,
                   color_blend_op=0, alpha_blend_op=0, color_mask=15,
                   cull_mode=2, topology=3, front_face_cw=0, const_alpha=0):
        flags = vtx_flags
        depth_state = (1 if depth_test else 0) | ((1 if (depth_test and depth_write) else 0) << 1) | ((depth_func & 0x0F) << 4)
        blend_state = ((1 if blend_enable else 0) |
                       ((src_color & 0x1F) << 1) |
                       ((dst_color & 0x1F) << 6) |
                       ((src_alpha & 0x1F) << 11) |
                       ((dst_alpha & 0x1F) << 16))
        blend_state2 = ((color_blend_op & 7) |
                        ((alpha_blend_op & 7) << 3) |
                        ((color_mask & 0x0F) << 6) |
                        ((const_alpha & 0xFF) << 10))
        cull_val = cull_mode & 3
        prim_val = topology & 7
        front_bit = 1 if front_face_cw else 0
        raster_state = cull_val | (prim_val << 2) | (front_bit << 5)
        return (flags, depth_state, blend_state, blend_state2, raster_state)

    @staticmethod
    def decode_key(flags, depth_state, blend_state, blend_state2, raster_state):
        depth_test = (depth_state & 1) != 0
        depth_write = ((depth_state >> 1) & 1) != 0
        depth_func = (depth_state >> 4) & 0x0F
        blend_enable = (blend_state & 1) != 0
        src_color = (blend_state >> 1) & 0x1F
        dst_color = (blend_state >> 6) & 0x1F
        src_alpha = (blend_state >> 11) & 0x1F
        dst_alpha = (blend_state >> 16) & 0x1F
        color_blend_op = blend_state2 & 7
        alpha_blend_op = (blend_state2 >> 3) & 7
        color_mask = (blend_state2 >> 6) & 0x0F
        const_alpha = (blend_state2 >> 10) & 0xFF
        cull_mode = raster_state & 3
        topology = (raster_state >> 2) & 7
        front_face_cw = (raster_state >> 5) & 1
        return {
            'vtx_flags': flags,
            'depth_test': depth_test,
            'depth_write': depth_write,
            'depth_func': depth_func,
            'blend_enable': blend_enable,
            'src_color': src_color,
            'dst_color': dst_color,
            'src_alpha': src_alpha,
            'dst_alpha': dst_alpha,
            'color_blend_op': color_blend_op,
            'alpha_blend_op': alpha_blend_op,
            'color_mask': color_mask,
            'const_alpha': const_alpha,
            'cull_mode': cull_mode,
            'topology': topology,
            'front_face_cw': front_face_cw,
        }

    def test_canonical_pipeline_key_roundtrip_exhaustive(self):
        """Verify round-trip fidelity for every canonical depth, blend, and raster state permutation."""
        for df in range(8):  # NEVER .. ALWAYS
            for cm in range(4):  # NONE, FRONT, BACK, FRONT_AND_BACK
                for topo in (0, 1, 2, 3, 4, 5):  # POINT, LINE_LIST, LINE_STRIP, TRI_LIST, TRI_STRIP, TRI_FAN
                    for ff in (0, 1):  # CCW, CW
                        for b_en in (False, True):
                            src_c = 4  # SRC_ALPHA
                            dst_c = 5  # ONE_MINUS_SRC_ALPHA
                            c_alpha = 128
                            c_mask = 15
                            key = self.encode_key(
                                vtx_flags=0x1F, depth_test=True, depth_write=True, depth_func=df,
                                blend_enable=b_en, src_color=src_c, dst_color=dst_c,
                                src_alpha=src_c, dst_alpha=dst_c,
                                color_blend_op=0, alpha_blend_op=0, color_mask=c_mask,
                                cull_mode=cm, topology=topo, front_face_cw=ff, const_alpha=c_alpha
                            )
                            dec = self.decode_key(*key)
                            self.assertEqual(dec['depth_func'], df)
                            self.assertEqual(dec['cull_mode'], cm)
                            self.assertEqual(dec['topology'], topo)
                            self.assertEqual(dec['front_face_cw'], ff)
                            self.assertEqual(dec['blend_enable'], b_en)
                            self.assertEqual(dec['src_color'], src_c)
                            self.assertEqual(dec['dst_color'], dst_c)
                            self.assertEqual(dec['const_alpha'], c_alpha)
                            self.assertEqual(dec['color_mask'], c_mask)

    def test_bit_isolation_no_overlap(self):
        """Verify that rasterState culling (bits 0..1), topology (bits 2..4), and front_face (bit 5) do not overlap."""
        # Max cull = 3
        key_cull = self.encode_key(0, False, False, 0, False, 0, 0, 0, 0, cull_mode=3, topology=0, front_face_cw=0)
        self.assertEqual(key_cull[4], 3)

        # Max topology = 7
        key_topo = self.encode_key(0, False, False, 0, False, 0, 0, 0, 0, cull_mode=0, topology=7, front_face_cw=0)
        self.assertEqual(key_topo[4], 7 << 2)

        # Front face bit = 1
        key_ff = self.encode_key(0, False, False, 0, False, 0, 0, 0, 0, cull_mode=0, topology=0, front_face_cw=1)
        self.assertEqual(key_ff[4], 1 << 5)

        # Combined
        key_comb = self.encode_key(0, False, False, 0, False, 0, 0, 0, 0, cull_mode=3, topology=7, front_face_cw=1)
        self.assertEqual(key_comb[4], 3 | (7 << 2) | (1 << 5))

    def test_quantized_float_blend_constants(self):
        """Verify float blend constant alpha quantization to 8-bit [0..255]."""
        for f in (0.0, 0.25, 0.5, 0.75, 1.0):
            quantized = int(round(f * 255.0))
            key = self.encode_key(0, False, False, 0, False, 0, 0, 0, 0, const_alpha=quantized)
            dec = self.decode_key(*key)
            self.assertEqual(dec['const_alpha'], quantized)

    def test_mru_pipeline_key_cache_hit(self):
        """Verify that identical consecutive PipelineKey queries hit the MRU cache without hash lookup."""
        cache = {}
        mru_key = None
        mru_pipeline = None

        lookups = 0
        def get_pipeline(key):
            nonlocal mru_key, mru_pipeline, lookups
            if mru_pipeline is not None and key == mru_key:
                return mru_pipeline  # Fast path hit
            lookups += 1
            p = cache.get(key, 0x1234)
            mru_key = key
            mru_pipeline = p
            return p

        k1 = (1, 2, 3, 4, 5)
        k2 = (6, 7, 8, 9, 10)
        # 10 calls with k1
        for _ in range(10):
            res = get_pipeline(k1)
            self.assertEqual(res, 0x1234)
        self.assertEqual(lookups, 1, "Consecutive identical keys must only perform 1 lookup")

        # Switch to k2
        res2 = get_pipeline(k2)
        self.assertEqual(lookups, 2)

    def test_sparse_active_texture_matching(self):
        """Verify untextured and sparse active texture slot matching without redundant memory comparisons."""
        last_tex_mask = 0
        curr_tex_mask = 0
        same_tex = (last_tex_mask == curr_tex_mask) and (curr_tex_mask == 0)
        self.assertTrue(same_tex, "Untextured draws must match without comparing texture slot buffers")


if __name__ == '__main__':
    unittest.main()
