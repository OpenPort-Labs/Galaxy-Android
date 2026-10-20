"""Shader-manifest schema, determinism and reproducibility tests.

The committed manifest is a derived artifact: the game assets it is cooked from
are never tracked.  These tests therefore (a) pin the on-disk schema in both
Python and C++, (b) prove serialization is independent of input order, and
(c) regenerate the artifact from the canonical cooked root and compare bytes
whenever that root is present locally.
"""

import os
import re
import struct
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

from tools.cook import manifest_schema as schema


def fnv1a32(data: bytes, seed: int = 2166136261) -> int:
    h = seed
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


class TestManifestSchemaContract(unittest.TestCase):
    def test_cpp_constants_match_python_schema(self):
        header_path = os.path.join(ROOT, "platform", "src", "gx", "vk_shader_manifest.h")
        with open(header_path, "r", encoding="utf-8") as f:
            text = f.read()

        def cpp_int(name):
            m = re.search(r"constexpr\s+\w+\s+" + name + r"\s*=\s*(0x[0-9A-Fa-f]+|\d+)", text)
            if not m:
                self.fail(f"{name} not found in vk_shader_manifest.h")
            return int(m.group(1), 0)

        self.assertEqual(cpp_int("kInventoryMagic"), schema.MAGIC_PTSU)
        self.assertEqual(cpp_int("kInventoryVersion"), schema.SCHEMA_VERSION)
        self.assertEqual(cpp_int("kBuildVersion"), schema.BUILD_VERSION)
        self.assertEqual(cpp_int("kManifestUidWords"), schema.UID_WORDS)

    def test_serialization_is_independent_of_input_order(self):
        sys.path.insert(0, os.path.join(ROOT, "tools", "cook"))
        from tools.cook.generate_shader_manifest import build_manifest_bytes

        def entry(seed, src):
            uid = [seed * 1000 + i for i in range(schema.UID_WORDS)]
            key = [seed, seed + 1, seed + 2, seed + 3, seed + 4]
            return (uid, key, src)

        materials = [entry(1, "A.arc"), entry(2, "B.arc"), entry(3, "C.arc")]
        first = build_manifest_bytes(materials)
        shuffled = [materials[2], materials[0], materials[1]]
        self.assertEqual(first, build_manifest_bytes(shuffled))
        self.assertEqual(first, build_manifest_bytes(list(reversed(materials))))

    def test_record_width_is_full_shader_uid(self):
        from tools.cook.generate_shader_manifest import encode_record
        uid = list(range(schema.UID_WORDS))
        rec = encode_record(uid, [1, 2, 3, 4, 5], "M.arc/M.bdl:Mat")
        self.assertEqual(struct.unpack_from("<%dI" % schema.UID_WORDS, rec, 0), tuple(uid))
        self.assertEqual(rec[schema.UID_WORDS * 4], 1)  # first pipeline-key word
        with self.assertRaises(ValueError):
            encode_record(uid[:-1], [1, 2, 3, 4, 5], "M")


class TestCommittedManifest(unittest.TestCase):
    MANIFEST = os.path.join(ROOT, "assets", "galaxy_shader_manifest.bin")

    def walk(self, data):
        magic, ver, build, count, crc = struct.unpack_from("<5I", data, 0)
        self.assertEqual(magic, schema.MAGIC_PTSU)
        self.assertEqual(ver, schema.SCHEMA_VERSION)
        self.assertEqual(build, schema.BUILD_VERSION)
        self.assertEqual(crc, fnv1a32(data[:16]))
        offset = schema.HEADER_SIZE
        records = []
        for i in range(count):
            start = offset
            uid = struct.unpack_from("<%dI" % schema.UID_WORDS, data, offset)
            offset += schema.UID_WORDS * 4
            key = struct.unpack_from("<%dI" % schema.KEY_WORDS, data, offset)
            offset += schema.KEY_WORDS * 4
            flags = data[offset]
            offset += 1
            src_len = struct.unpack_from("<H", data, offset)[0]
            offset += 2
            self.assertLessEqual(src_len, schema.MAX_SOURCE_BYTES)
            src = data[offset:offset + src_len].decode("utf-8")
            offset += src_len
            rec_crc = struct.unpack_from("<I", data, offset)[0]
            offset += 4
            self.assertEqual(rec_crc, fnv1a32(data[start:offset - 4]), f"record {i} CRC")
            records.append((uid, key, flags, src))
        self.assertEqual(offset, len(data), "manifest must end exactly at EOF")
        return records

    def test_committed_manifest_is_valid_and_full_width(self):
        with open(self.MANIFEST, "rb") as f:
            data = f.read()
        records = self.walk(data)
        self.assertGreater(len(records), 0)
        self.assertGreaterEqual(len(records[0][0]), schema.UID_WORDS)

    def test_committed_manifest_is_reproducible_from_cooked_root(self):
        cooked = os.path.join(ROOT, "cooked")
        if not os.path.isdir(cooked):
            self.skipTest("cooked/ asset root is not present (game assets are not tracked)")
        from tools.cook.generate_shader_manifest import build_manifest_bytes, scan_directory_for_models

        materials, report = scan_directory_for_models(cooked)
        self.assertGreater(len(materials), 0)
        self.assertEqual(report["root"], os.path.abspath(cooked))
        if report["skipped_inputs"]:
            print(f"[INFO] {report['skipped_inputs']} inputs skipped: {report['gaps'][:3]}")
        generated = build_manifest_bytes(materials)
        with open(self.MANIFEST, "rb") as f:
            committed = f.read()
        self.assertEqual(len(generated), len(committed),
                         f"regenerated {len(materials)} records, committed file differs in size; "
                         "regenerate with `python tools/cook/generate_shader_manifest.py`")
        self.assertEqual(generated, committed,
                         "committed manifest does not match a clean regeneration from cooked/")


if __name__ == "__main__":
    unittest.main()
