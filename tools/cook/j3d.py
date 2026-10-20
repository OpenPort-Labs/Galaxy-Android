"""J3D models (BMD/BDL) and animations (BCK/BCA/BTK/BRK/BTP/BVA/BPK/BLA/BLK):
big-endian -> little-endian in place.

Field layouts follow the decomp's loader structures
(JSystem/J3DGraphLoader/*.hpp, J3DGraphAnimator/J3DAnimation.hpp).  Display
lists, texture images and palettes stay big-endian: they are byte streams the
GPU emulation reads as such.
"""
from util import u16, s16, u32, s32, sw16, sw32, sw_range, extents, swap_ntab

GX_VA_POS, GX_VA_NRM, GX_VA_CLR0, GX_VA_CLR1, GX_VA_TEX0, GX_VA_NBT, GX_VA_NULL = 9, 10, 11, 12, 13, 25, 0xFF
COMP_WIDTH = {0: 1, 1: 1, 2: 2, 3: 2, 4: 4}        # u8 s8 u16 s16 f32
COLOR_WIDTH = {0: 2, 1: 1, 2: 1, 3: 2, 4: 1, 5: 1}  # 565 888 888x 4444 6666 8888

warnings = []


def warn(msg):
    warnings.append(msg)


# ---------------------------------------------------------------------------
# Record layouts: lists of (offset, width, count) spans to swap per record.
# ---------------------------------------------------------------------------
def swap_records(b, start, end, rec_size, spans):
    if rec_size <= 0:
        return
    n = (min(end, len(b)) - start) // rec_size
    for i in range(n):
        base = start + i * rec_size
        for off, width, count in spans:
            if width == 2:
                sw16(b, base + off, count)
            elif width == 4:
                sw32(b, base + off, count)


MAT_INIT = (0x14C, [(0x08, 2, (0x9C - 0x08) // 2), (0xBC, 2, (0x14C - 0xBC) // 2)])
IND_INIT = (0x138, [(0x14, 4, 6), (0x30, 4, 6), (0x4C, 4, 6)])
LIGHT = (0x34, [(0x00, 4, 6), (0x1C, 4, 6)])
TEXMTX = (0x64, [(0x04, 4, 3), (0x10, 4, 2), (0x18, 2, 1), (0x1C, 4, 2), (0x24, 4, 16)])
FOG = (0x2C, [(0x02, 2, 1), (0x04, 4, 4), (0x18, 2, 10)])
NBT = (0x10, [(0x04, 4, 3)])
JOINT = (0x40, [(0x00, 2, 1), (0x04, 4, 3), (0x10, 2, 3), (0x18, 4, 3), (0x24, 4, 1), (0x28, 4, 6)])
SHAPE_INIT = (0x28, [(0x02, 2, 4), (0x0C, 4, 7)])
SHAPE_MTX = (0x08, [(0x00, 2, 2), (0x04, 4, 1)])
TIMG = (0x20, [(0x02, 2, 2), (0x0A, 2, 1), (0x0C, 4, 1), (0x1A, 2, 1), (0x1C, 4, 1)])

# MAT3 sub-tables in J3DMaterialBlock order: (name, kind)
MAT3_TABLES = [
    ('init', MAT_INIT), ('id', 2), ('name', 'ntab'), ('ind', IND_INIT), ('cull', 4), ('matColor', 1), ('chanNum', 1),
    ('chanInfo', 1), ('ambColor', 1), ('light', LIGHT), ('texGenNum', 1), ('texCoord', 1), ('texCoord2', 1), ('texMtx', TEXMTX),
    ('postTexMtx', TEXMTX), ('texNo', 2), ('tevOrder', 1), ('tevColor', 2), ('kColor', 1), ('tevStageNum', 1), ('tevStage', 1),
    ('swapMode', 1), ('swapTable', 1), ('fog', FOG), ('alphaComp', 1), ('blend', 1), ('zMode', 1), ('zCompLoc', 1), ('dither', 1),
    ('nbt', NBT),
]


def swap_tables(b, blk, blk_end, offs_base, tables):
    """Generic helper: `tables` is a list of (name, kind); offsets are u32s at
    offs_base, relative to the block start."""
    offs = {}
    for i, (name, _) in enumerate(tables):
        offs[name] = u32(b, offs_base + 4 * i)
    ends = extents(offs, blk_end - blk)
    for name, kind in tables:
        o = offs[name]
        if not o:
            continue
        start, end = blk + o, blk + ends[name]
        if kind == 'ntab':
            swap_ntab(b, start)
        elif isinstance(kind, tuple):
            swap_records(b, start, end, kind[0], kind[1])
        elif kind in (2, 4):
            sw_range(b, start, end, kind)
    sw32(b, offs_base, len(tables))


# ---------------------------------------------------------------------------
# Model blocks
# ---------------------------------------------------------------------------
def blk_inf1(b, o, end):
    hier = u32(b, o + 0x14)
    if hier:
        sw_range(b, o + hier, end, 2)
    sw16(b, o + 0x08, 1)
    sw32(b, o + 0x0C, 3)


def blk_vtx1(b, o, end):
    names = ['fmt', 'pos', 'nrm', 'nbt', 'clr0', 'clr1'] + ['tex%d' % i for i in range(8)]
    offs = {n: u32(b, o + 0x08 + 4 * i) for i, n in enumerate(names)}
    ends = extents(offs, end - o)
    # attribute formats
    fmts = {}
    p = o + offs['fmt'] if offs['fmt'] else 0
    while p and p + 16 <= end:
        attr, cnt, typ = u32(b, p), u32(b, p + 4), u32(b, p + 8)
        sw32(b, p, 3)
        if attr == GX_VA_NULL:
            break
        fmts[attr] = (cnt, typ)
        p += 16
    attr_of = {'pos': GX_VA_POS, 'nrm': GX_VA_NRM, 'nbt': GX_VA_NBT, 'clr0': GX_VA_CLR0, 'clr1': GX_VA_CLR1}
    for i in range(8):
        attr_of['tex%d' % i] = GX_VA_TEX0 + i
    for n in names[1:]:
        if not offs[n]:
            continue
        attr = attr_of[n]
        fmt = fmts.get(attr)
        if fmt is None and attr == GX_VA_NBT:
            fmt = fmts.get(GX_VA_NRM)
        if fmt is None:
            warn('VTX1 array %s without format' % n)
            continue
        width = COLOR_WIDTH.get(fmt[1], 1) if attr in (GX_VA_CLR0, GX_VA_CLR1) else COMP_WIDTH.get(fmt[1], 1)
        sw_range(b, o + offs[n], o + ends[n], width)
    sw32(b, o + 0x08, len(names))


def blk_evp1(b, o, end):
    names = ['mixNum', 'mixIndex', 'weight', 'invMtx']
    offs = {n: u32(b, o + 0x0C + 4 * i) for i, n in enumerate(names)}
    ends = extents(offs, end - o)
    if offs['mixIndex']:
        sw_range(b, o + offs['mixIndex'], o + ends['mixIndex'], 2)
    if offs['weight']:
        sw_range(b, o + offs['weight'], o + ends['weight'], 4)
    if offs['invMtx']:
        sw_range(b, o + offs['invMtx'], o + ends['invMtx'], 4)
    sw16(b, o + 0x08, 1)
    sw32(b, o + 0x0C, 4)


def blk_drw1(b, o, end):
    offs = {'flag': u32(b, o + 0x0C), 'index': u32(b, o + 0x10)}
    ends = extents(offs, end - o)
    if offs['index']:
        sw_range(b, o + offs['index'], o + ends['index'], 2)
    sw16(b, o + 0x08, 1)
    sw32(b, o + 0x0C, 2)


def blk_jnt1(b, o, end):
    num = u16(b, o + 0x08)
    init, index, name = u32(b, o + 0x0C), u32(b, o + 0x10), u32(b, o + 0x14)
    if init:
        swap_records(b, o + init, o + init + num * JOINT[0], JOINT[0], JOINT[1])
    if index:
        sw16(b, o + index, num)
    if name:
        swap_ntab(b, o + name)
    sw16(b, o + 0x08, 1)
    sw32(b, o + 0x0C, 3)


def blk_mat3(b, o, end):
    swap_tables(b, o, end, o + 0x0C, MAT3_TABLES)
    sw16(b, o + 0x08, 1)


def blk_mdl3(b, o, end):
    num = u16(b, o + 0x08)
    dl, patch, cur, mode, _1c, name = [u32(b, o + 0x0C + 4 * i) for i in range(6)]
    if dl:
        sw32(b, o + dl, 2 * num)
    if patch:
        swap_records(b, o + patch, o + patch + 16 * num, 16, [(0, 2, 6)])
    if cur:
        sw32(b, o + cur, 2 * num)
    if name:
        swap_ntab(b, o + name)
    sw16(b, o + 0x08, 1)
    sw32(b, o + 0x0C, 6)


def blk_shp1(b, o, end):
    num = u16(b, o + 0x08)
    names = ['init', 'index', 'name', 'vtxDesc', 'mtxTable', 'dl', 'mtxInit', 'drawInit']
    offs = {n: u32(b, o + 0x0C + 4 * i) for i, n in enumerate(names)}
    ends = extents(offs, end - o)
    if offs['init']:
        swap_records(b, o + offs['init'], o + offs['init'] + num * SHAPE_INIT[0], SHAPE_INIT[0], SHAPE_INIT[1])
    if offs['index']:
        sw16(b, o + offs['index'], num)
    if offs['name']:
        swap_ntab(b, o + offs['name'])
    if offs['vtxDesc']:
        sw_range(b, o + offs['vtxDesc'], o + ends['vtxDesc'], 4)
    if offs['mtxTable']:
        sw_range(b, o + offs['mtxTable'], o + ends['mtxTable'], 2)
    if offs['mtxInit']:
        swap_records(b, o + offs['mtxInit'], o + ends['mtxInit'], SHAPE_MTX[0], SHAPE_MTX[1])
    if offs['drawInit']:
        sw_range(b, o + offs['drawInit'], o + ends['drawInit'], 4)
    sw16(b, o + 0x08, 1)
    sw32(b, o + 0x0C, len(names))


def blk_tex1(b, o, end):
    num = u16(b, o + 0x08)
    res, name = u32(b, o + 0x0C), u32(b, o + 0x10)
    if res:
        swap_records(b, o + res, o + res + num * TIMG[0], TIMG[0], TIMG[1])
    if name:
        swap_ntab(b, o + name)
    sw16(b, o + 0x08, 1)
    sw32(b, o + 0x0C, 2)


MODEL_BLOCKS = {b'INF1': blk_inf1, b'VTX1': blk_vtx1, b'EVP1': blk_evp1, b'DRW1': blk_drw1, b'JNT1': blk_jnt1, b'MAT3': blk_mat3,
                b'MDL3': blk_mdl3, b'SHP1': blk_shp1, b'TEX1': blk_tex1}


def swap_model(b):
    nblocks = u32(b, 0x0C)
    sw32(b, 0, 4)
    o = 0x20
    for _ in range(nblocks):
        if o + 8 > len(b):
            break
        tag = bytes(b[o:o + 4])
        size = u32(b, o + 4)
        fn = MODEL_BLOCKS.get(tag)
        if fn:
            fn(b, o, o + size)
        else:
            warn('model block %r not converted' % tag)
        sw32(b, o, 2)
        if size <= 0:
            break
        o += size


# ---------------------------------------------------------------------------
# Animations
# ---------------------------------------------------------------------------
from j3d_anim import swap_anim

def is_model(b):
    return len(b) >= 0x20 and bytes(b[0:4]) in (b'J3D2',)


def is_anim(b):
    return len(b) >= 0x20 and bytes(b[0:4]) == b'J3D1'
