"""SHP1 vertex-layout and MAT3 material-ID tests.

These build tiny synthetic J3D models with the documented block layout
(``decomp/libs/JSystem``) so they run without the untracked game assets, and
they exercise the real cooker modules rather than a reimplementation.
"""

import os
import struct
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
for p in (ROOT, os.path.join(ROOT, "tools"), os.path.join(ROOT, "tools", "cook")):
    if p not in sys.path:
        sys.path.insert(0, p)

from tools.cook.j3d_shape_attrs import (  # noqa: E402
    GXA_CLR0, GXA_NBT, GXA_NRM, GXA_PNMTXIDX, GXA_POS, GXA_TEX0, GXA_TEX0MTXIDX,
    ShapeAttrError, VALID_VF_MASK, attrs_to_flags, material_vertex_flags,
)
from tools.cook.j3d_mat3_parser import parse_j3d_mat3  # noqa: E402
from tools.cook.manifest_schema import UID_WORDS  # noqa: E402

MAT_INIT_SIZE = 0x14C


def model(blocks):
    body = b"".join(blocks)
    header = b"J3D2" + b"bdl4" + struct.pack(">I", 0x20 + len(body)) + struct.pack(">I", len(blocks))
    return header + b"\x00" * (0x20 - len(header)) + body


def block(tag, payload):
    return tag + struct.pack(">I", len(payload) + 8) + payload


def vtx_desc_pairs(attr_sets):
    """Encodes several GXVtxDescList entries; returns (table, byte offsets)."""
    table, offsets = b"", []
    for attrs in attr_sets:
        offsets.append(len(table))
        for a in attrs:
            table += struct.pack(">II", a, 3)
        table += struct.pack(">II", 0xFF, 0)
    return table, offsets


def shp1(desc_per_shape):
    """Builds an SHP1 block whose shape s uses descriptor entry s."""
    table, offsets = vtx_desc_pairs(desc_per_shape)
    num = len(desc_per_shape)
    init_off = 0x0C + 8 * 4
    index_off = init_off + num * 0x28
    desc_off = index_off + 2 * num
    recs, index = b"", b""
    for s in range(num):
        recs += struct.pack(">BBHHHHH", 0, 0xFF, 1, offsets[s], s, s, 0xFFFF)
        recs += struct.pack(">7f", 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0)
        index += struct.pack(">H", s)
    offsets_tbl = b"".join(struct.pack(">I", off) for off in
                           (init_off, index_off, 0, desc_off, 0, 0, 0, 0))
    payload = struct.pack(">H", num) + b"\x00\x00" + offsets_tbl + recs + index + table
    return block(b"SHP1", payload)


def mat3(ids, blend_types, record_count=None):
    """Builds a MAT3 block whose material m uses init record ids[m]."""
    num = len(ids)
    init_off = 0x0C + 30 * 4
    rec_count = record_count if record_count is not None else (max(ids) + 1 if ids else 1)
    id_off = init_off + rec_count * MAT_INIT_SIZE
    cull_off = id_off + 2 * num
    tev_num_off = cull_off + 8
    tev_stage_off = tev_num_off + 4
    blend_off = tev_stage_off + 20
    zmode_off = blend_off + 4 * len(blend_types)

    recs = b""
    for r in range(rec_count):
        rec = bytearray(MAT_INIT_SIZE)
        rec[1] = 0            # cull index
        rec[2] = 0            # color channel count index
        rec[3] = 0            # texgen index
        rec[4] = 0            # tev stage count index
        rec[6] = 0            # zmode index
        struct.pack_into(">H", rec, 0x146, 0)
        struct.pack_into(">H", rec, 0x148, r % max(1, len(blend_types)))
        recs += bytes(rec)

    tables = {
        0: init_off, 1: id_off, 4: cull_off, 6: 0, 10: 0, 19: tev_num_off,
        20: tev_stage_off, 21: 0, 24: 0, 25: blend_off, 26: zmode_off,
    }
    offsets_tbl = b"".join(struct.pack(">I", tables.get(i, 0)) for i in range(30))
    payload = (struct.pack(">H", num) + b"\xff\xff" + offsets_tbl + recs +
               b"".join(struct.pack(">H", i) for i in ids) +
               struct.pack(">II", 2, 0) +          # cull table
               struct.pack(">II", 1, 0) +          # tev stage count
               b"\x00" * 20 +                      # tev stage info
               b"".join(bytes([t, 1, 1, 0]) for t in blend_types) +
               b"\x00" * 16)                       # zmode table
    return block(b"MAT3", payload)


class TestAttrFlags(unittest.TestCase):
    def test_every_gxattr_maps_to_its_vtx_flag(self):
        self.assertEqual(attrs_to_flags({GXA_POS}), 0x0)
        self.assertEqual(attrs_to_flags({GXA_POS, GXA_PNMTXIDX}), 0x1)
        self.assertEqual(attrs_to_flags({GXA_POS, GXA_NRM}), 0x2)
        self.assertEqual(attrs_to_flags({GXA_POS, GXA_NRM, GXA_NBT}), 0x6)
        self.assertEqual(attrs_to_flags({GXA_POS, GXA_CLR0}), 0x8)
        self.assertEqual(attrs_to_flags({GXA_POS, GXA_TEX0}), 0x20)
        self.assertEqual(attrs_to_flags({GXA_POS, GXA_TEX0 + 7}), 0x1000)
        self.assertEqual(attrs_to_flags({GXA_POS, GXA_TEX0MTXIDX}), 0x2000)

    def test_flags_never_escape_the_renderer_mask(self):
        allattrs = set(range(22))
        self.assertEqual(attrs_to_flags(allattrs) & ~VALID_VF_MASK, 0)

    def test_shape_descriptor_lists_are_read_per_shape(self):
        m = model([shp1([{GXA_POS}, {GXA_POS, GXA_NRM, GXA_TEX0}])])
        self.assertEqual(material_vertex_flags(m, 2), [0x0, 0x22])

    def test_shared_descriptor_entry_is_reused(self):
        table, offs = vtx_desc_pairs([{GXA_POS, GXA_CLR0}])
        self.assertEqual(offs, [0])
        m = model([shp1([{GXA_POS, GXA_CLR0}, {GXA_POS, GXA_CLR0}])])
        self.assertEqual(material_vertex_flags(m, 2), [0x8, 0x8])

    def test_shape_material_count_mismatch_is_rejected(self):
        m = model([shp1([{GXA_POS}])])
        with self.assertRaises(ShapeAttrError):
            material_vertex_flags(m, 2)

    def test_unterminated_descriptor_list_is_rejected(self):
        m = bytearray(model([shp1([{GXA_POS}])]))
        # Overwrite the 0xFF terminator with a valid attribute.
        pos = m.rfind(struct.pack(">II", 0xFF, 0))
        struct.pack_into(">II", m, pos, GXA_POS, 3)
        with self.assertRaises(ShapeAttrError):
            material_vertex_flags(bytes(m), 1)


class TestMaterialIdTable(unittest.TestCase):
    def test_shared_init_record_is_not_read_as_neighbouring_data(self):
        shared = model([shp1([{GXA_POS}, {GXA_POS}]), mat3([0, 0], [1, 0])])
        distinct = model([shp1([{GXA_POS}, {GXA_POS}]), mat3([0, 1], [1, 0])])
        shared_mats, gaps, _ = parse_j3d_mat3(shared, "shared.bdl")
        distinct_mats, _, _ = parse_j3d_mat3(distinct, "distinct.bdl")
        self.assertFalse(gaps)
        self.assertEqual(len(shared_mats), 2)
        self.assertEqual(shared_mats[0][1], shared_mats[1][1])
        self.assertNotEqual(distinct_mats[0][1], distinct_mats[1][1])

    def test_out_of_range_material_id_is_rejected(self):
        m = model([shp1([{GXA_POS}, {GXA_POS}]), mat3([0, 7], [1, 0], record_count=2)])
        mats, gaps, _ = parse_j3d_mat3(m, "bad_id.bdl")
        self.assertEqual(mats, [])
        self.assertTrue(any("ID table" in g for g in gaps), gaps)

    def test_uid_carries_the_derived_vertex_flags(self):
        m = model([shp1([{GXA_POS, GXA_NRM, GXA_TEX0}]), mat3([0], [0])])
        mats, gaps, _ = parse_j3d_mat3(m, "flags.bdl")
        self.assertFalse(gaps)
        uid, key, _src = mats[0]
        self.assertEqual(len(uid), UID_WORDS)
        self.assertEqual(uid[0], 0x22)
        self.assertEqual(key[0], 0x22)
        self.assertNotEqual(uid[0], 0x4047)


if __name__ == "__main__":
    unittest.main()
