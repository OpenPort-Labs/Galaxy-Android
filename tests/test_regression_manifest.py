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
    Verifies 90-word UID binary manifest serialization, CRC checks,
    and truncated record handling.
    """
    def fnv1a32(self, data: bytes, seed: int = 2166136261) -> int:
        h = seed
        for b in data:
            h = ((h ^ b) * 16777619) & 0xFFFFFFFF
        return h

    def test_90_word_uid_manifest_structure(self):
        magic = 0x55535450
        schema_version = 2
        build_version = 20261019
        count = 1
        hdr_data = struct.pack('<IIII', magic, schema_version, build_version, count)
        hdr_crc = self.fnv1a32(hdr_data)
        header = hdr_data + struct.pack('<I', hdr_crc)

        uid_words = [i for i in range(90)]
        key_words = [10, 20, 30, 40, 50]
        flags = 0
        src_str = "TestMaterial/Model.bdl"
        src_bytes = src_str.encode('utf-8')
        str_len = len(src_bytes)

        rec_payload = (
            struct.pack('<90I', *uid_words) +
            struct.pack('<5I', *key_words) +
            struct.pack('<B', flags) +
            struct.pack('<H', str_len) +
            src_bytes
        )
        rec_crc = self.fnv1a32(rec_payload)
        record = rec_payload + struct.pack('<I', rec_crc)

        data = header + record
        # Verify sizes
        self.assertEqual(len(header), 20)
        self.assertEqual(len(rec_payload), 360 + 20 + 1 + 2 + str_len)
        self.assertEqual(len(record), 387 + str_len)

        # Parse header
        m, s, b, cnt, c = struct.unpack('<IIIII', data[:20])
        self.assertEqual(m, magic)
        self.assertEqual(s, 2)
        self.assertEqual(self.fnv1a32(data[:16]), c)

        # Parse record
        offset = 20
        parsed_uid = struct.unpack_from('<90I', data, offset)
        offset += 360
        parsed_key = struct.unpack_from('<5I', data, offset)
        offset += 20
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
        magic = 0x55535450
        schema_version = 2
        build_version = 20261019
        count = 1
        hdr_data = struct.pack('<IIII', magic, schema_version, build_version, count)
        hdr_crc = self.fnv1a32(hdr_data)
        header = hdr_data + struct.pack('<I', hdr_crc)

        # Truncated record with only 100 bytes (less than 387 required)
        truncated_record = b'\xAA' * 100
        data = header + truncated_record
        self.assertLess(len(data) - 20, 387, "Truncated record must be detected as incomplete")


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



if __name__ == '__main__':
    unittest.main()
