import struct
from util import u32, s16, sw32, sw16, sw_range, extents, swap_ntab
from j3d import warn, swap_records
def anm_tables(b, o, end, offs_at, kinds):
    """kinds: list of (kind) per u32 offset starting at o+offs_at."""
    offs = {i: u32(b, o + offs_at + 4 * i) for i in range(len(kinds))}
    ends = extents(offs, end - o)
    for i, kind in enumerate(kinds):
        off = offs[i]
        if not off or kind is None:
            continue
        start, stop = o + off, o + ends[i]
        if kind == 'ntab':
            swap_ntab(b, start)
        elif isinstance(kind, tuple):
            swap_records(b, start, stop, kind[0], kind[1])
        else:
            sw_range(b, start, stop, kind)
    sw32(b, o + offs_at, len(kinds))


def ank1(b, o, end):
    n_s, n_r, n_t = s16(b, o + 0x0E), s16(b, o + 0x10), s16(b, o + 0x12)
    tbl, sc, rot, tr = [u32(b, o + 0x14 + 4 * i) for i in range(4)]
    if tbl:
        sw_range(b, o + tbl, (o + sc) if sc else end, 2)
    if sc:
        sw32(b, o + sc, n_s)
    if rot:
        sw16(b, o + rot, n_r)
    if tr:
        sw32(b, o + tr, n_t)
    sw16(b, o + 0x0A, 5)
    sw32(b, o + 0x14, 4)


def anf1(b, o, end):
    anm_tables(b, o, end, 0x14, [2, 4, 2, 4])
    sw16(b, o + 0x0A, 5)


def ttk1(b, o, end):
    # Two offset groups (texture matrices, post-texture matrices); table
    # extents must be computed across both so no table overruns into the other.
    kinds = [2, 2, 'ntab', None, 4, 4, 2, 4]  # table, matID, names, texMtxID(u8), center, scale, rot, trans
    offs = {}
    for g, base in enumerate((0x14, 0x3C)):
        for i in range(8):
            offs[(g, i)] = u32(b, o + base + 4 * i)
    ends = extents(offs, end - o)
    for (g, i), off in offs.items():
        kind = kinds[i]
        if not off or kind is None:
            continue
        start, stop = o + off, o + ends[(g, i)]
        if kind == 'ntab':
            swap_ntab(b, start)
        else:
            sw_range(b, start, stop, kind)
    sw32(b, o + 0x14, 8)
    sw32(b, o + 0x3C, 8)
    sw16(b, o + 0x0A, 5)
    sw16(b, o + 0x34, 4)
    sw32(b, o + 0x5C, 1)


REG_TABLE = (0x1C, [(0x00, 2, 12)])


def trk1(b, o, end):
    kinds = [REG_TABLE, REG_TABLE, 2, 2, 'ntab', 'ntab', 2, 2, 2, 2, 2, 2, 2, 2]
    anm_tables(b, o, end, 0x20, kinds)
    sw16(b, o + 0x0A, 11)


def tpt1(b, o, end):
    kinds = [(0x08, [(0x00, 2, 2), (0x06, 2, 1)]), 2, 2, 'ntab']
    anm_tables(b, o, end, 0x10, kinds)
    sw16(b, o + 0x0A, 3)


def vaf1(b, o, end):
    anm_tables(b, o, end, 0x10, [2, None])
    sw16(b, o + 0x0A, 3)


def pak1(b, o, end):
    kinds = [2, 2, 'ntab', 2, 2, 2, 2]
    anm_tables(b, o, end, 0x18, kinds)
    sw16(b, o + 0x0C, 6)


def paf1(b, o, end):
    kinds = [2, 2, 'ntab', None, None, None, None]
    anm_tables(b, o, end, 0x18, kinds)
    sw16(b, o + 0x0C, 6)


def clk1(b, o, end):
    anm_tables(b, o, end, 0x10, [2, 4])
    sw16(b, o + 0x0A, 1)
    sw32(b, o + 0x0C, 1)


ANIM_BLOCKS = {b'ANK1': ank1, b'ANF1': anf1, b'TTK1': ttk1, b'TRK1': trk1, b'TPT1': tpt1, b'VAF1': vaf1, b'PAK1': pak1, b'PAF1': paf1,
               b'CLK1': clk1, b'CLF1': clk1}


def swap_sound_anim(b, o):
    """JAUSoundAnimation: u16 num, pad, u32 control, then 0x20-byte sounds."""
    if o < 0 or o + 8 > len(b):
        return
    num = u16(b, o)
    sw16(b, o, 1)
    sw32(b, o + 4, 1)
    p = o + 8
    for _ in range(num):
        if p + 0x20 > len(b):
            break
        sw32(b, p, 5)       # sound id, note on/off, pitch, flags
        sw32(b, p + 0x1C, 1)
        p += 0x20


def swap_anim(b):
    se = u32(b, 0x1C)
    nblocks = u32(b, 0x0C)
    sw32(b, 0, 4)
    sw32(b, 0x1C, 1)
    o = 0x20
    for _ in range(nblocks):
        if o + 8 > len(b):
            break
        tag = bytes(b[o:o + 4])
        size = u32(b, o + 4)
        fn = ANIM_BLOCKS.get(tag)
        if fn:
            fn(b, o, o + size)
        else:
            warn('anim block %r not converted' % tag)
        sw32(b, o, 2)
        if size <= 0:
            break
        o += size
    if se and se != 0xFFFFFFFF and se < len(b):
        swap_sound_anim(b, se)


