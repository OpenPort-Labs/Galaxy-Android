"""KCL collision (KCollisionServer) big-endian -> little-endian conversion.

Layout (KCLFile in Game/Map/KCollision.hpp):
  0x00 u32 x4   offsets of positions, normals, prisms, octree
  0x10 f32 x4   thickness, min.xyz
  0x20 s32 x6   x/y/z masks, block width shift, x shift, xy shift
Positions and normals are f32 triples.  Prisms are 0x10-byte records indexed
from 1 (index 0 overlaps the last normal).  The octree is a tree of s32 node
words: a value >= 0 is the offset of an 8-word child block, a negative value
is a leaf whose low 31 bits give the offset of a u16 prism list; both offsets
are relative to the block that holds the word.  The game reads a leaf list
starting at list + 2 up to a 0 terminator.
"""
from util import u16, s32, u32, sw16, sw32, sw_range

HEADER_SIZE = 0x38


def looks_like_kcl(b):
    if len(b) < HEADER_SIZE:
        return False
    pos, nrm, prism, octree = (u32(b, i) for i in range(0, 16, 4))
    return pos == HEADER_SIZE and pos <= nrm <= prism + 0x10 <= octree <= len(b)


def _root_count(b):
    xmask, ymask, zmask = s32(b, 0x20), s32(b, 0x24), s32(b, 0x28)
    shift, xshift, xyshift = s32(b, 0x2C), s32(b, 0x30), s32(b, 0x34)
    if xshift == -1 and xyshift == -1:
        return 1
    nx = ((~xmask & 0xFFFFFFFF) >> shift) + 1
    ny = ((~ymask & 0xFFFFFFFF) >> shift) + 1
    nz = ((~zmask & 0xFFFFFFFF) >> shift) + 1
    return nx * ny * nz


def swap_kcl(b, warnings=None):
    """Converts a KCL file (bytearray) in place."""
    n = len(b)
    pos_off, nrm_off, prism_off, octree_off = (u32(b, i) for i in range(0, 16, 4))

    # Walk the octree while the data is still big-endian.
    node_words = set()
    list_halves = set()
    stack = [(octree_off, _root_count(b))]
    seen_blocks = set()
    while stack:
        base, count = stack.pop()
        if (base, count) in seen_blocks:
            continue
        seen_blocks.add((base, count))
        for i in range(count):
            off = base + 4 * i
            if off + 4 > n:
                raise ValueError('octree node outside file')
            node_words.add(off)
            v = s32(b, off)
            if v >= 0:
                stack.append((base + v, 8))
            else:
                p = base + (v & 0x7FFFFFFF) + 2
                while True:
                    if p + 2 > n:
                        raise ValueError('prism list runs off the end of the file')
                    list_halves.add(p)
                    if u16(b, p) == 0:
                        break
                    p += 2

    overlap = [o for o in list_halves if (o & ~3) in node_words]
    if overlap:
        raise ValueError('octree nodes and prism lists overlap (%d cells)' % len(overlap))

    sw32(b, 0, HEADER_SIZE // 4)
    sw_range(b, pos_off, nrm_off, 4)
    sw_range(b, nrm_off, prism_off + 0x10, 4)
    for rec in range(prism_off + 0x10, octree_off, 0x10):
        # height, then position, face normal, three edge normals and the
        # attribute (the polygon's row in the .pa table)
        sw32(b, rec)
        sw16(b, rec + 4, 6)
    for off in node_words:
        sw32(b, off)
    for off in list_halves:
        sw16(b, off)
    if warnings is not None and (octree_off - prism_off) % 0x10:
        warnings.append('kcl: prism table is not a whole number of records')
    return b
