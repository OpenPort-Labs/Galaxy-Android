"""BMG message files (MessageHolder.cpp): big-endian -> little-endian.

Header: 'MESG' 'bmg1' u32 size, u32 blocks, u8 encoding.  Blocks:
  INF1  u16 count, u16 item size, u32; items: u32 text offset, u16 camera
        set id, u8 x6
  DAT1  UTF-16 strings with 0x1A message tags
  FLW1  u16 node count, u16 branch count, u32; TalkNode[count] (u8 type,
        u8 group, u16 index, then u16+u16 or, for event nodes, one u32);
        u16 branch table[branch count]; u8[branch count]
  FLI1  u16 count, u8 item size, u8; items: u32 id, u16 index, u16
"""
from util import u16, u32, sw16, sw32
from lyt import swap_text, warnings


def swap_bmg(b):
    nblocks = u32(b, 0x0C)
    sw32(b, 0, 4)
    o = 0x20
    for _ in range(nblocks):
        if o + 8 > len(b):
            break
        kind, size = bytes(b[o:o + 4]), u32(b, o + 4)
        if kind == b'INF1':
            n, item = u16(b, o + 8), u16(b, o + 0xA)
            sw16(b, o + 8, 2)
            sw32(b, o + 0xC)
            for i in range(n):
                it = o + 0x10 + item * i
                sw32(b, it)
                sw16(b, it + 4)
        elif kind == b'DAT1':
            swap_text(b, o + 8, o + size)
        elif kind == b'FLW1':
            nodes, branches = u16(b, o + 8), u16(b, o + 0xA)
            sw16(b, o + 8, 2)
            sw32(b, o + 0xC)
            p = o + 0x10
            for _ in range(nodes):
                sw16(b, p + 2)
                if b[p] == 3:
                    sw32(b, p + 4)
                else:
                    sw16(b, p + 4, 2)
                p += 8
            sw16(b, p, branches)
        elif kind == b'FLI1':
            n, item = u16(b, o + 8), b[o + 0xA]
            sw16(b, o + 8)
            for i in range(n):
                it = o + 0x10 + item * i
                sw32(b, it)
                sw16(b, it + 4)
        else:
            warnings.append('bmg block %r not converted' % kind)
        sw32(b, o, 2)
        if size < 8:
            break
        o += size
    return b
