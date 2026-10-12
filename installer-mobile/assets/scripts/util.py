"""Byte-order helpers for converting big-endian game data in place."""
import struct


def u16(b, o):
    return struct.unpack_from('>H', b, o)[0]


def s16(b, o):
    return struct.unpack_from('>h', b, o)[0]


def u32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def s32(b, o):
    return struct.unpack_from('>i', b, o)[0]


def _swap_block(chunk, width):
    """Byte-swaps a bytes-like chunk made of `width`-byte elements."""
    n = len(chunk) // width
    code = 'H' if width == 2 else 'I'
    return struct.pack('<%d%s' % (n, code), *struct.unpack('>%d%s' % (n, code), bytes(chunk[:n * width])))


def sw16(b, o, n=1):
    """Swaps n consecutive 16-bit values starting at o (clipped to the buffer)."""
    n = min(n, (len(b) - o) // 2)
    if n > 0:
        b[o:o + 2 * n] = _swap_block(b[o:o + 2 * n], 2)


def sw32(b, o, n=1):
    """Swaps n consecutive 32-bit values starting at o (clipped to the buffer)."""
    n = min(n, (len(b) - o) // 4)
    if n > 0:
        b[o:o + 4 * n] = _swap_block(b[o:o + 4 * n], 4)


def sw_range(b, start, end, width):
    """Swaps every `width`-byte element in [start, end)."""
    end = min(end, len(b))
    n = (end - start) // width
    if n <= 0 or width == 1:
        return
    if width == 2:
        sw16(b, start, n)
    elif width == 4:
        sw32(b, start, n)


def extents(offsets, limit):
    """Given {name: offset}, returns {name: end} where end is the next larger
    non-zero offset (or `limit`).  Used to size tables whose length is not
    stored explicitly."""
    vals = sorted(set(v for v in offsets.values() if v))
    out = {}
    for k, v in offsets.items():
        if not v:
            continue
        nxt = limit
        for w in vals:
            if w > v:
                nxt = w
                break
        out[k] = nxt
    return out


def swap_ntab(b, o):
    """ResNTAB: u16 count, u16 pad, {u16 hash, u16 offset}[count], strings."""
    if o <= 0 or o + 4 > len(b):
        return
    n = u16(b, o)
    sw16(b, o, 2 + 2 * n)
