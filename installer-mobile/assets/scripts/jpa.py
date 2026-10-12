"""JParticle resources (JPAC2-10 .jpc): big-endian -> little-endian.

Layouts follow JSystem/JParticle/*.hpp and JPAResourceLoader.cpp.  Texture
images stay big-endian.
"""
from util import u16, u32, s16, sw16, sw32, sw_range

warnings = []


def blk_bem1(b, o):
    sw32(b, o + 0x08, 24)       # flags, user work, 22 x f32
    sw16(b, o + 0x68, 8)        # rot(3), max/start frame, life, volume size, div


def blk_bsp1(b, o):
    flags = u32(b, o + 0x08)
    prm_off, env_off = s16(b, o + 0x0C), s16(b, o + 0x0E)
    clr_flg, prm_num, env_num = b[o + 0x21], b[o + 0x22], b[o + 0x23]
    sw32(b, o + 0x08)
    sw16(b, o + 0x0C, 2)
    sw32(b, o + 0x10, 2)
    sw16(b, o + 0x18)
    sw16(b, o + 0x24)
    if flags & 0x01000000:      # texture coordinate animation: 10 x f32
        sw32(b, o + 0x34, 10)
    if clr_flg & 0x02:
        for i in range(prm_num):
            sw16(b, o + prm_off + 6 * i)
    if clr_flg & 0x08:
        for i in range(env_num):
            sw16(b, o + env_off + 6 * i)


def blk_esp1(b, o):
    sw32(b, o + 0x08, 8)
    sw16(b, o + 0x28, 2)
    sw32(b, o + 0x2C, 13)


def blk_ssp1(b, o):
    sw32(b, o + 0x08, 11)
    sw32(b, o + 0x3C)
    sw16(b, o + 0x40, 2)
    sw16(b, o + 0x46)


def blk_etx1(b, o):
    sw32(b, o + 0x08, 7)


def blk_fld1(b, o):
    sw32(b, o + 0x08, 14)


def blk_kfa1(b, o):
    sw32(b, o + 0x0C, 4 * b[o + 9])


BLOCKS = {b'BEM1': blk_bem1, b'BSP1': blk_bsp1, b'ESP1': blk_esp1, b'SSP1': blk_ssp1, b'ETX1': blk_etx1, b'FLD1': blk_fld1,
          b'KFA1': blk_kfa1}


def swap_jpc(b):
    from lyt import swap_timg
    if bytes(b[4:8]) != b'2-10':
        warnings.append('jpc version %r not converted' % bytes(b[4:8]))
        return b
    nres, ntex, tex_off = u16(b, 8), u16(b, 0xA), u32(b, 0xC)
    sw32(b, 4)
    sw16(b, 8, 2)
    sw32(b, 0xC)
    o = 0x10
    for _ in range(nres):
        nblocks = u16(b, o + 2)
        sw16(b, o, 2)
        o += 8
        for _ in range(nblocks):
            kind, size = bytes(b[o:o + 4]), u32(b, o + 4)
            fn = BLOCKS.get(kind)
            if fn:
                fn(b, o)
            elif kind == b'TDB1':
                sw_range(b, o + 8, o + size, 2)
            else:
                warnings.append('jpc block %r not converted' % kind)
            sw32(b, o, 2)
            o += size
    o = tex_off
    for _ in range(ntex):
        size = u32(b, o + 4)
        sw32(b, o, 3)
        swap_timg(b, o + 0x20)
        o += size
    return b
