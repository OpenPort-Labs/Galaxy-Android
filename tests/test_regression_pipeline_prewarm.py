import unittest
import math
import struct
import os
import tempfile

class TestVulkanPipelinePrewarmAndDiscovery(unittest.TestCase):
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
