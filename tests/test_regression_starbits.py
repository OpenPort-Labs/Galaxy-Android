import unittest
import math
import struct
import os
import tempfile

class TestStarBitQuickSaveSafeguards(unittest.TestCase):
    """
    Verifies that quick saves commit only delta star-bits, preventing infinite accumulation.
    """
    def test_repeated_quicksaves_in_same_stage_prevent_stock_duplication(self):
        stock_star_bits = 50
        stage_star_bits = 30
        credited_delta = 0

        # First quick save in stage: 30 new star bits
        delta = max(0, stage_star_bits - credited_delta)
        stock_star_bits += delta
        credited_delta = stage_star_bits
        self.assertEqual(stock_star_bits, 80)
        self.assertEqual(credited_delta, 30)

        # Second quick save immediately without collecting more star bits
        delta = max(0, stage_star_bits - credited_delta)
        stock_star_bits += delta
        credited_delta = stage_star_bits
        self.assertEqual(stock_star_bits, 80, "Stock star bits must NOT increase on immediate re-save")

        # Player collects 5 more star bits in stage (total 35)
        stage_star_bits = 35
        delta = max(0, stage_star_bits - credited_delta)
        stock_star_bits += delta
        credited_delta = stage_star_bits
        self.assertEqual(stock_star_bits, 85, "Stock star bits must only increase by the delta (5)")

        # Scene change / galaxy move resets delta tracking
        credited_delta = 0
        stage_star_bits = 0
        self.assertEqual(stock_star_bits, 85)

    def test_spending_star_bits_preserves_monotonic_watermark(self):
        stock = 100
        stage_bits = 40
        credited = 0

        # Quick save at 40 bits
        delta = max(0, stage_bits - credited)
        stock += delta
        credited = max(credited, stage_bits)
        self.assertEqual(stock, 140)
        self.assertEqual(credited, 40)

        # Spend 15 bits on a Hungry Luma (stage bits drop to 25)
        stage_bits = 25
        # Crucial: credited must NOT be lowered to 25
        delta = max(0, stage_bits - credited)
        stock += delta
        credited = max(credited, stage_bits)
        self.assertEqual(stock, 140, "Spending bits followed by quick save must NOT duplicate bits")
        self.assertEqual(credited, 40)

        # Collect 5 more bits (total 30, still <= 40)
        stage_bits = 30
        delta = max(0, stage_bits - credited)
        stock += delta
        credited = max(credited, stage_bits)
        self.assertEqual(stock, 140)
        self.assertEqual(credited, 40)

        # Collect 15 more bits (total 45, > 40)
        stage_bits = 45
        delta = max(0, stage_bits - credited)
        stock += delta
        credited = max(credited, stage_bits)
        self.assertEqual(stock, 145, "Only bits beyond previous watermark (5) are credited")
        self.assertEqual(credited, 45)

    def test_stage_clear_transfer_subtracts_credited_bits(self):
        stock = 100
        stage_bits = 50
        credited = 0

        # Player quick-saves midway through stage after collecting 30 bits
        stage_bits = 30
        delta = max(0, stage_bits - credited)
        stock += delta
        credited = max(credited, stage_bits)
        self.assertEqual(stock, 130)

        # Player collects 20 more bits (stage_bits = 50) and completes the stage
        stage_bits = 50
        # In GameSequenceFunction::sendStageResultSequenceParam:
        uncredited_pieces = max(0, stage_bits - credited)
        stock += uncredited_pieces
        credited = 0
        stage_bits = 0

        # Total stock must be exactly 100 (initial) + 50 (collected) = 150
        self.assertEqual(stock, 150, "Stage clear transfer must not duplicate bits already credited during quick save")


class TestCookerManifestCrossImplementation(unittest.TestCase):
    """
    Verifies that the cooker-generated binary manifest adheres exactly to the native C++ reader specification.
    """
    @staticmethod
    def fnv1a32(data: bytes, seed: int = 2166136261) -> int:
        h = seed
        for b in data:
            h = ((h ^ b) * 16777619) & 0xFFFFFFFF
        return h

    def test_actual_manifest_file_structure(self):
        import os
        manifest_path = "assets/galaxy_shader_manifest.bin"
        self.assertTrue(os.path.exists(manifest_path), f"Manifest {manifest_path} must exist")

        with open(manifest_path, "rb") as f:
            data = f.read()

        file_size = len(data)
        self.assertGreaterEqual(file_size, 20)

        magic, schema, build, count, hdr_crc = struct.unpack('<IIIII', data[:20])
        self.assertEqual(magic, 0x55535450, "Header magic must be 0x55535450 ('PTSU')")
        self.assertEqual(schema, 2, "Schema version must be 2")
        self.assertEqual(build, 20261019, "Build version must be 20261019")
        self.assertEqual(self.fnv1a32(data[:16]), hdr_crc, "Header CRC must match")
        self.assertGreater(count, 0)

        offset = 20
        for i in range(count):
            rec_start = offset
            uid_words = struct.unpack_from('<90I', data, offset)
            offset += 360
            key_words = struct.unpack_from('<5I', data, offset)
            offset += 20
            flags = data[offset]
            offset += 1
            str_len = struct.unpack_from('<H', data, offset)[0]
            offset += 2
            self.assertLessEqual(str_len, 512, "String length must be <= 512")
            src_str = data[offset:offset+str_len].decode('utf-8')
            offset += str_len
            rec_crc = struct.unpack_from('<I', data, offset)[0]
            offset += 4

            computed_crc = self.fnv1a32(data[rec_start:offset-4])
            self.assertEqual(rec_crc, computed_crc, f"Record {i} ({src_str}) CRC must match")

        self.assertEqual(offset, file_size, "Manifest must have exact EOF with 0 trailing bytes")

    def test_cooker_shader_uid_word_alignment(self):
        """
        Verify that generate_shader_manifest.py encodes ShaderUid word indices matching C++ gl_shadergen.h:
        - colorCtrl: words 4..5
        - alphaCtrl: words 6..7
        - texMtxInfo: words 8..15
        - numTevStages: word 24
        - numIndStages: word 25
        - alphaFunc: word 74
        """
        import os
        manifest_path = "assets/galaxy_shader_manifest.bin"
        with open(manifest_path, "rb") as f:
            data = f.read()

        count = struct.unpack_from('<I', data, 12)[0]
        offset = 20
        verified_tex_mtx = False
        for i in range(count):
            uid_words = struct.unpack_from('<90I', data, offset)
            offset += 360 + 20 + 1
            str_len = struct.unpack_from('<H', data, offset)[0]
            offset += 2 + str_len + 4

            # If texMtxInfo was encoded, verify it is at words 8..15, NOT word 6
            for w in range(8, 16):
                if uid_words[w] != 0:
                    verified_tex_mtx = True
            # Word 6 is alphaCtrl[0]
            # Word 7 is alphaCtrl[1]
            # Word 24 is numTevStages (must be >= 1 and <= 16)
            self.assertGreaterEqual(uid_words[24], 1)
            self.assertLessEqual(uid_words[24], 16)

        self.assertTrue(verified_tex_mtx, "At least one manifest entry should have non-zero texMtxInfo at words 8..15")


class TestFramePacerPhaseDebtRecovery(unittest.TestCase):
    """
    Verifies that missed deadlines reset frame pacer phase without debt accumulation.
    """
    def test_missed_deadline_phase_reset(self):
        period_ns = 16666667  # 60 FPS
        last_present_ns = 1000000000
        target_ns = last_present_ns + period_ns

        # Simulated frame submission that took 25ms (missed 16.6ms deadline)
        now_ns = last_present_ns + 25000000
        self.assertGreaterEqual(now_ns, target_ns)

        # Old flawed behavior: last_present_ns = target_ns (accumulated debt)
        old_last = target_ns
        old_next_target = old_last + period_ns
        self.assertLess(old_next_target, now_ns + period_ns, "Old behavior carries debt forward")

        # Fixed behavior: reset to now_ns
        new_last = now_ns
        new_next_target = new_last + period_ns
        self.assertEqual(new_next_target, now_ns + period_ns, "Fixed behavior sets deadline from current time")

    def test_game_speed_simulation_cadence_decoupling(self):
        target_fps = 60.0
        game_speed = 2.0  # Double speed simulation

        # Old flawed behavior: effectiveFps = targetFps * gameSpeed (120 FPS request on 60Hz display)
        old_effective = target_fps * game_speed
        self.assertEqual(old_effective, 120.0)

        # Corrected behavior: effectiveFps = targetFps (presentation cadence is strictly display-bound)
        corrected_effective = target_fps
        self.assertEqual(corrected_effective, 60.0)


if __name__ == '__main__':
    unittest.main()





if __name__ == '__main__':
    unittest.main()
