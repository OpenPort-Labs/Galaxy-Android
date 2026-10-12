"""JMap (BCSV) tables: big-endian -> little-endian in place.

Layout (see Game/Util/JMapInfo.hpp):
  header  s32 numEntries, s32 numItems, s32 dataOffset, u32 entrySize
  items   numItems x { u32 hash, u32 mask, u16 offsData, u8 shift, u8 type }
  data    numEntries x entrySize, at dataOffset
  strings after the entries (referenced by STRING_PTR offsets)
"""
import struct

LONG, STRING, FLOAT, LONG_2, SHORT, BYTE, STRING_PTR = 0, 1, 2, 3, 4, 5, 6
SIZE_OF = {LONG: 4, FLOAT: 4, LONG_2: 4, STRING_PTR: 4, SHORT: 2, BYTE: 1, STRING: 0}


def looks_like_bcsv(data):
    if len(data) < 16:
        return False
    n, items, off, esize = struct.unpack_from('>iiiI', data, 0)
    if n < 0 or items < 0 or items > 512 or n > 1000000:
        return False
    if off != 16 + items * 12:
        return False
    if off + n * esize > len(data):
        return False
    return True


def swap(buf):
    """buf: bytearray holding one BCSV file (big-endian). Converted in place."""
    n, items, off, esize = struct.unpack_from('>iiiI', buf, 0)
    struct.pack_into('<iiiI', buf, 0, n, items, off, esize)
    fields = []
    for i in range(items):
        p = 16 + i * 12
        h, mask, offs, shift, typ = struct.unpack_from('>IIHBB', buf, p)
        struct.pack_into('<IIHBB', buf, p, h, mask, offs, shift, typ)
        size = SIZE_OF.get(typ, 0)
        if size > 1:
            fields.append((offs, size))
    slots = sorted(set(fields))
    for e in range(n):
        base = off + e * esize
        for offs, size in slots:
            p = base + offs
            if p + size > len(buf):
                continue
            if size == 4:
                buf[p:p + 4] = buf[p:p + 4][::-1]
            elif size == 2:
                buf[p:p + 2] = buf[p:p + 2][::-1]
    return buf
