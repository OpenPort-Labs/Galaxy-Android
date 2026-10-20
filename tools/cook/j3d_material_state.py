"""Per-material state readers for the MAT3 tables.

Kept separate from j3d_mat3_parser.py so both files stay inside the 300-line
budget.  Each reader mirrors the decompiled J3D code that turns one table record
into GX register state; the packing itself lives in j3d_gx_state.py.

Table offsets used here come from J3DMaterialInitData (J3DMaterialFactory.hpp):
0x00C mColorChanIdx[4], 0x028 mTexCoordIdx[8], 0x0BC mTevOrderIdx[16],
0x0E4 mTevStageIdx[16], 0x104 mTevSwapModeIdx[16], 0x124 mTevSwapModeTableIdx[4].

A table whose header offset is 0 is simply absent; J3DMaterialFactory answers
with the j3dDefault* record for that slot (J3DTevs.cpp), so an absent table is
modelled with the defaults rather than reported as a gap.  An index that points
past a table that *is* present is a real gap and returns None.
"""

import struct

try:
    import j3d_gx_state as gx
except ImportError:  # imported as tools.cook.j3d_material_state
    import tools.cook.j3d_gx_state as gx

FMT16 = "<H"

OFF_COLOR_CHAN_IDX = 0x00C   # u16[4]
OFF_TEX_COORD_IDX = 0x028    # u16[8]
OFF_TEV_KCOLOR_SEL = 0x09C   # u8[16]
OFF_TEV_KALPHA_SEL = 0x0AC   # u8[16]
OFF_TEV_ORDER_IDX = 0x0BC    # u16[16]
OFF_TEV_STAGE_IDX = 0x0E4    # u16[16]
OFF_TEV_SWAP_SEL_IDX = 0x104  # u16[16]
OFF_TEV_SWAP_TABLE_IDX = 0x124  # u16[4]
MATERIAL_RECORD_SIZE = 0x14C
TEV_STAGE_RECORD_SIZE = 20
SWAP_MODE_RECORD_SIZE = 4

# J3DTevs.cpp: j3dDefaultTevStageInfo / j3dDefaultTevOrderInfoNull /
# j3dDefaultTevSwapMode, used when the material has no record to index.
DEFAULT_TEV_STAGE = bytes((0x04, 0x0A, 0x0F, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                           0x05, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00))
DEFAULT_TEV_ORDER = bytes((0xFF, 0xFF, 0xFF, 0x00))
DEFAULT_SWAP_MODE = bytes((0x00, 0x00, 0x00, 0x00))
DEFAULT_SWAP_TABLE = bytes((0x00, 0x01, 0x02, 0x03))


def _indices(buf, m_off, field, count, fmt16):
    return [struct.unpack_from(fmt16, buf, m_off + field + 2 * i)[0] for i in range(count)]


def _record(buf, table_off, block_end, idx, size, default):
    if not table_off or idx == 0xFFFF:
        return default
    start = table_off + idx * size
    if start < 0 or start + size > block_end:
        return None
    return buf[start:start + size]


def channel_words(buf, table_off, block_end, m_off, fmt16=FMT16):
    """The four XF 0x100E..0x1011 words for one material, in slot order."""
    words = []
    for idx in _indices(buf, m_off, OFF_COLOR_CHAN_IDX, 4, fmt16):
        rec = _record(buf, table_off, block_end, idx, 8, gx.DEFAULT_COLOR_CHAN)
        if rec is None:
            return None
        words.append(gx.color_chan_word(rec))
    return words


def tex_coord_words(buf, table_off, block_end, m_off, count, fmt16=FMT16):
    """XF 0x1040+n texgen words for a material, or None if a record is missing.

    The register is derived from the J3DTexCoordInfo the material indexes (its
    texgen type and source), not from the texture matrix it selects.  The
    default info for slot i reads GX_TG_TEX<i> through GX_MTX2x4.
    """
    words = []
    for slot, idx in enumerate(_indices(buf, m_off, OFF_TEX_COORD_IDX, min(count, 8), fmt16)):
        rec = _record(buf, table_off, block_end, idx, 4, bytes((1, 4 + slot, 0x3C, 0)))
        if rec is None:
            return None
        words.append(gx.tex_coord_gen_word(rec[0], rec[1]))
    return words


def tev_stage_words(buf, stage_off, swap_off, block_end, m_off, count, fmt16=FMT16):
    """The (color, alpha) BP words for a material's TEV stages, or None.

    J3DTevBlock::load sets each stage's swap selectors from mTevSwapModeIdx, so
    the alpha word cannot be built from the stage record alone.
    """
    colors, alphas = [], []
    for s in range(min(count, 16)):
        stage_idx = struct.unpack_from(fmt16, buf, m_off + OFF_TEV_STAGE_IDX + 2 * s)[0]
        info = _record(buf, stage_off, block_end, stage_idx, TEV_STAGE_RECORD_SIZE, DEFAULT_TEV_STAGE)
        if info is None:
            return None
        swap_idx = struct.unpack_from(fmt16, buf, m_off + OFF_TEV_SWAP_SEL_IDX + 2 * s)[0]
        swap = _record(buf, swap_off, block_end, swap_idx, SWAP_MODE_RECORD_SIZE, DEFAULT_SWAP_MODE)
        if swap is None:
            return None
        colors.append(gx.tev_color_word(info))
        alphas.append(gx.tev_alpha_word(info, swap[0], swap[1]))
    return colors, alphas


def tev_order_fields(buf, table_off, block_end, m_off, count, fmt16=FMT16):
    """The 12-bit BP TEV order fields for a material's stages, or None."""
    fields = []
    for s in range(min(count, 16)):
        idx = struct.unpack_from(fmt16, buf, m_off + OFF_TEV_ORDER_IDX + 2 * s)[0]
        rec = _record(buf, table_off, block_end, idx, 4, DEFAULT_TEV_ORDER)
        if rec is None:
            return None
        fields.append(gx.tev_order_field(rec))
    return fields


def tev_ksel_words(buf, swap_off, block_end, m_off, stages, fmt16=FMT16):
    """The eight BP 0xF6..0xFD words, or None when the swap tables are missing.

    Each block runs the same 0..15 step-4 loop (J3DMatBlock.cpp), writing
    0xF6+g from selector pair g and 0xF6+g+1 from the next pair, with swap-mode
    table g supplying (R, G) and then (B, A).  A material that owns fewer
    selectors leaves the rest 0xFF, which becomes the block default here.
    """
    kcs = [_default_byte(v, 0x0C) for v in buf[m_off + OFF_TEV_KCOLOR_SEL:m_off + OFF_TEV_KALPHA_SEL]]
    kas = [_default_byte(v, 0x1C) for v in buf[m_off + OFF_TEV_KALPHA_SEL:m_off + OFF_TEV_ORDER_IDX]]
    tables = []
    for si in _indices(buf, m_off, OFF_TEV_SWAP_TABLE_IDX, 4, fmt16):
        rec = _record(buf, swap_off, block_end, si, SWAP_MODE_RECORD_SIZE, DEFAULT_SWAP_TABLE)
        if rec is None:
            return None
        tables.append(rec)
    words = []
    for group in range(4):
        table = tables[group]
        for half in range(2):
            c1, c2 = (table[0], table[1]) if half == 0 else (table[2], table[3])
            i1, i2 = 4 * group + 2 * half, 4 * group + 2 * half + 1
            words.append(gx.tev_ksel_word(kcs[i1], kas[i1], kcs[i2], kas[i2], c1, c2))
    return words


def _default_byte(value, default):
    """J3DMaterialFactory replaces a 0xFF selector with the block default."""
    return default if value == 0xFF else value
