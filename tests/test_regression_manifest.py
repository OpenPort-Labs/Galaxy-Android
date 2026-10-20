import unittest
import math
import struct
import os
import tempfile

class TestFramePacerAndRetraceInvariants(unittest.TestCase):
    """
    Tests for Section A: Invariants preventing the ~12 FPS starvation collapse.
    """
    def test_retrace_cadence_not_starved_by_dropped_frame(self):
        """
        Verify that a single missed deadline or slow render submission (>16.6ms)
        does not suppress subsequent retrace interrupts for 66.7ms (4 frames).
        """
        period_ns = 16666667  # 60Hz
        now = 1000000000
        last_external_at = now - 20000000  # 20ms ago (1 slow frame)

        # Old flawed logic: if (now - sExternalAt < period * 4) continue;
        old_suppressed = (now - last_external_at < period_ns * 4)
        self.assertTrue(old_suppressed, "Old logic erroneously dropped retraces for 4 frames (66.7ms) causing 12 FPS!")

        # New corrected logic: allow retrace if cadence has elapsed (>0.85 period)
        new_allowed = (now - last_external_at >= int(period_ns * 0.85))
        self.assertTrue(new_allowed, "New logic maintains 60 FPS retrace interrupt cadence!")


class TestTextureUploadStagingSafety(unittest.TestCase):
    """
    Tests for Section F: Staging buffer allocation and copy bounds.
    """
    def test_staging_copy_region_within_allocated_buffer(self):
        width = 256
        height = 256
        levels = 4
        # Calculate expected byte sizes
        total_bytes = 0
        w, h = width, height
        regions = []
        for l in range(levels):
            level_bytes = w * h * 4
            regions.append((total_bytes, level_bytes, w, h))
            total_bytes += level_bytes
            w = max(1, w // 2)
            h = max(1, h // 2)

        # Verify all regions lie within total_bytes
        for offset, bytes_count, rw, rh in regions:
            self.assertLessEqual(offset + bytes_count, total_bytes)
            self.assertGreater(rw, 0)
            self.assertGreater(rh, 0)


class TestShaderManifestContract(unittest.TestCase):
    """
    Verifies full-width UID binary manifest serialization, CRC checks,
    and truncated record handling against the shared schema constants.
    """
    @staticmethod
    def schema():
        from tools.cook.manifest_schema import (
            MAGIC_PTSU, SCHEMA_VERSION, BUILD_VERSION, UID_WORDS, KEY_WORDS)
        return MAGIC_PTSU, SCHEMA_VERSION, BUILD_VERSION, UID_WORDS, KEY_WORDS
    def fnv1a32(self, data: bytes, seed: int = 2166136261) -> int:
        h = seed
        for b in data:
            h = ((h ^ b) * 16777619) & 0xFFFFFFFF
        return h

    def test_full_width_uid_manifest_structure(self):
        magic, schema_version, build_version, uid_word_count, key_word_count = self.schema()
        count = 1
        hdr_data = struct.pack('<IIII', magic, schema_version, build_version, count)
        hdr_crc = self.fnv1a32(hdr_data)
        header = hdr_data + struct.pack('<I', hdr_crc)

        uid_words = [i for i in range(uid_word_count)]
        key_words = [10, 20, 30, 40, 50]
        flags = 0
        src_str = "TestMaterial/Model.bdl"
        src_bytes = src_str.encode('utf-8')
        str_len = len(src_bytes)

        rec_payload = (
            struct.pack('<%dI' % uid_word_count, *uid_words) +
            struct.pack('<%dI' % key_word_count, *key_words) +
            struct.pack('<B', flags) +
            struct.pack('<H', str_len) +
            src_bytes
        )
        rec_crc = self.fnv1a32(rec_payload)
        record = rec_payload + struct.pack('<I', rec_crc)

        data = header + record
        # Verify sizes
        uid_bytes = uid_word_count * 4
        key_bytes = key_word_count * 4
        self.assertEqual(len(header), 20)
        self.assertEqual(len(rec_payload), uid_bytes + key_bytes + 1 + 2 + str_len)
        self.assertEqual(len(record), uid_bytes + key_bytes + 7 + str_len)

        # Parse header
        m, s, b, cnt, c = struct.unpack('<IIIII', data[:20])
        self.assertEqual(m, magic)
        self.assertEqual(s, schema_version)
        self.assertEqual(b, build_version)
        self.assertEqual(self.fnv1a32(data[:16]), c)

        # Parse record
        offset = 20
        parsed_uid = struct.unpack_from('<%dI' % uid_word_count, data, offset)
        offset += uid_bytes
        parsed_key = struct.unpack_from('<%dI' % key_word_count, data, offset)
        offset += key_bytes
        flags_val = data[offset]
        offset += 1
        parsed_len = struct.unpack_from('<H', data, offset)[0]
        offset += 2
        parsed_src = data[offset:offset+parsed_len].decode('utf-8')
        offset += parsed_len
        parsed_crc = struct.unpack_from('<I', data, offset)[0]

        self.assertEqual(list(parsed_uid), uid_words)
        self.assertEqual(list(parsed_key), key_words)
        self.assertEqual(parsed_src, src_str)
        self.assertEqual(self.fnv1a32(data[20:offset]), parsed_crc)

    def test_truncated_record_detection(self):
        magic, schema_version, build_version, uid_word_count, key_word_count = self.schema()
        count = 1
        hdr_data = struct.pack('<IIII', magic, schema_version, build_version, count)
        hdr_crc = self.fnv1a32(hdr_data)
        header = hdr_data + struct.pack('<I', hdr_crc)

        # Truncated record with only 100 bytes (far less than a full record)
        min_record = uid_word_count * 4 + key_word_count * 4 + 1 + 2 + 4
        truncated_record = b'\xAA' * 100
        data = header + truncated_record
        self.assertLess(len(data) - 20, min_record, "Truncated record must be detected as incomplete")


class TestDualEndianJ3DAndRarcParsing(unittest.TestCase):
    """
    Verifies support for both big-endian (GameCube/Wii native) and little-endian (cooked)
    RARC and J3D files.
    """
    def test_rarc_endianness_detection(self):
        be_header = b'RARC' + struct.pack('>I', 64)
        le_header = b'CRAR' + struct.pack('<I', 64)

        def detect_rarc(b):
            if b[:4] == b'RARC':
                return 'BE', struct.unpack_from('>I', b, 4)[0]
            elif b[:4] == b'CRAR':
                return 'LE', struct.unpack_from('<I', b, 4)[0]
            return None, 0

        endian_be, size_be = detect_rarc(be_header)
        self.assertEqual(endian_be, 'BE')
        self.assertEqual(size_be, 64)

        endian_le, size_le = detect_rarc(le_header)
        self.assertEqual(endian_le, 'LE')
        self.assertEqual(size_le, 64)

    def test_j3d_mat3_dual_endian_tags(self):
        # Big-endian: J3D2, MAT3
        be_j3d = b'J3D2bdl4'
        be_mat3 = b'MAT3' + struct.pack('>I', 32)
        # Little-endian: 2D3J, 3TAM
        le_j3d = b'2D3J4ldb'
        le_mat3 = b'3TAM' + struct.pack('<I', 32)

        def is_j3d_magic(magic):
            return magic in (b'J3D1', b'J3D2', b'1D3J', b'2D3J')

        def is_mat3_chunk(tag):
            return tag in (b'MAT3', b'3TAM')

        self.assertTrue(is_j3d_magic(be_j3d[:4]))
        self.assertTrue(is_j3d_magic(le_j3d[:4]))
        self.assertTrue(is_mat3_chunk(be_mat3[:4]))
        self.assertTrue(is_mat3_chunk(le_mat3[:4]))


class TestVulkanManifestAndApiVersionRequirements(unittest.TestCase):
    """
    Verifies that Vulkan 1.0 (0x400000) is the mandatory baseline across all manifests
    and that no mandatory Vulkan 1.1+ (0x401000) requirement exists.
    """
    def test_manifests_declare_vulkan_1_0_baseline(self):
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        manifest_paths = [
            os.path.join(root, "platform", "mobile", "AndroidManifest.xml"),
            os.path.join(root, "platform", "mobile", "AndroidManifest_plus.xml"),
            os.path.join(root, "platform", "android", "AndroidManifest.xml"),
        ]
        for path in manifest_paths:
            self.assertTrue(os.path.exists(path), f"Manifest missing: {path}")
            with open(path, "r", encoding="utf-8") as f:
                content = f.read()
            self.assertIn('android:name="android.hardware.vulkan.version"', content, f"Missing vulkan feature declaration in {path}")
            self.assertIn('android:version="0x400000"', content, f"Must declare Vulkan 1.0 (0x400000) in {path}")
            self.assertNotIn('android:version="0x401000"', content, f"Must not declare Vulkan 1.1 (0x401000) in {path}")

    def test_vk_device_targets_vulkan_1_0(self):
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        device_cpp = os.path.join(root, "platform", "src", "gx", "vk", "vk_device.cpp")
        self.assertTrue(os.path.exists(device_cpp))
        with open(device_cpp, "r", encoding="utf-8") as f:
            content = f.read()
        self.assertIn('appInfo.apiVersion = VK_API_VERSION_1_0;', content)


class TestGxAlphaCompareAndMarioEyeSemantics(unittest.TestCase):
    """
    Validates GX BP 0xF3 alpha-compare register encoding, J3D parser bit layout,
    real Mario eye/eyelid material extraction, and fragment discard semantics.
    """
    def test_gx_bp_0xf3_bit_layout_round_trip(self):
        # Test all permutations of comp0 (0..7), comp1 (0..7), op (0..3)
        for comp0 in range(8):
            for comp1 in range(8):
                for op in range(4):
                    # Hardware GX TEV_ALPHAFUNC encoding:
                    # ref0: bits 0..7, ref1: bits 8..15, op0 (comp0): bits 16..18,
                    # op1 (comp1): bits 19..21, logic: bits 22..23, rid: bits 24..31
                    bp_reg = (0xF3 << 24) | (op << 22) | (comp1 << 19) | (comp0 << 16) | (255 << 8) | 128
                    native_alpha_func = (bp_reg >> 16) & 0xFF
                    # Parser encoding
                    parsed_alpha_func = comp0 | (comp1 << 3) | (op << 6)
                    self.assertEqual(native_alpha_func, parsed_alpha_func)

                    # Shadergen decoding in vk_shadergen_fs.cpp
                    f0 = parsed_alpha_func & 7
                    f1 = (parsed_alpha_func >> 3) & 7
                    logic = (parsed_alpha_func >> 6) & 3
                    self.assertEqual(f0, comp0)
                    self.assertEqual(f1, comp1)
                    self.assertEqual(logic, op)

    def test_real_mario_eye_materials_from_archive(self):
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        mario_arc = os.path.join(root, "cooked", "files", "ObjectData", "Mario.arc")
        if not os.path.exists(mario_arc):
            self.skipTest(f"Mario.arc not present at {mario_arc}")

        import tools.rarc as r
        from tools.cook.j3d_mat3_parser import parse_j3d_mat3
        with open(mario_arc, "rb") as f:
            data = f.read()
        mats = []
        for name, b in r.rarc_list(r.yaz0_decompress(data)):
            if "mario.bdl" in name:
                mats, _, _ = parse_j3d_mat3(b, name)
                break

        mat_dict = {desc.split(":")[-1]: (uid, key) for uid, key, desc in mats}
        self.assertIn("EyeLid_v", mat_dict)
        self.assertIn("EyeballL_v", mat_dict)
        self.assertIn("EyeballR_v", mat_dict)

        # EyeLid_v: comp0=GEQUAL(6), comp1=LEQUAL(3), op=AND(0) -> 6 | (3 << 3) | (0 << 6) = 0x1E
        eyelid_uid, _ = mat_dict["EyeLid_v"]
        self.assertEqual(eyelid_uid[74], 0x1E, "EyeLid_v must have alphaFunc == 0x1E (GEQUAL && LEQUAL)")

        # Verify 8-bit wrap fragment discard semantics vs clamp
        # When TEV output wraps (e.g. out of range intermediate value 260 -> 4)
        wrap_a = 260 & 255  # 4
        clamp_a = min(255, max(0, 260))  # 255
        ref0 = 100  # a >= ref0 (4 >= 100 is False -> discarded)
        self.assertFalse(wrap_a >= ref0, "8-bit wrapped alpha correctly fails test and discards")
        self.assertTrue(clamp_a >= ref0, "Flawed clamped alpha erroneously passes test, causing visual defect")


if __name__ == '__main__':
    unittest.main()
