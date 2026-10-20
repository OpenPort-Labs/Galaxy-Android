import math
import struct
from collision import sub, cross, dot



def _prism_box(tri, normal, depth, center, half):
    """Does the prism a triangle sweeps `depth` behind its face (the volume
    the game tests points and spheres against) reach the box?  Separating
    axes of two convex solids: the faces of each and the pairs of edges."""
    pts = [sub(p, center) for p in tri]
    pts += [(p[0] - normal[0] * depth, p[1] - normal[1] * depth, p[2] - normal[2] * depth) for p in pts]
    edges = [sub(tri[1], tri[0]), sub(tri[2], tri[1]), sub(tri[0], tri[2]), normal]
    axes = [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), normal]
    axes += [cross(e, normal) for e in edges[:3]]
    for e in edges:
        axes += [(0.0, -e[2], e[1]), (e[2], 0.0, -e[0]), (-e[1], e[0], 0.0)]
    for axis in axes:
        r = half[0] * abs(axis[0]) + half[1] * abs(axis[1]) + half[2] * abs(axis[2])
        proj = [dot(p, axis) for p in pts]
        if min(proj) > r or max(proj) < -r:
            return False
    return True


def build_kcl(tris, thickness, max_per_leaf=16, min_width=32):
    """Big-endian KCL for triangles (a, b, c) facing along cross(b - a, c - a).
    Returns (kcl bytes, indices of the triangles kept, in prism order)."""
    positions, normals = [], []
    pos_index, nrm_index = {}, {}

    def intern(table, index, v):
        key = struct.pack('>3f', *v)
        if key not in index:
            index[key] = len(table)
            table.append(v)
        return index[key]

    prisms, kept, shapes, normals_of = [], [], [], []
    for i, (a, b, c) in enumerate(tris):
        a, b, c = f32(a), f32(b), f32(c)
        n = cross(sub(b, a), sub(c, a))
        if dot(n, n) < 1e-6:
            continue
        n = normalize(n)
        e0 = normalize(cross(n, sub(c, a)))
        e1 = normalize(cross(sub(b, a), n))
        e2 = normalize(cross(sub(c, b), n))
        height = dot(sub(b, a), e2)
        if height < 0.01:
            continue
        prisms.append((height, intern(positions, pos_index, a), intern(normals, nrm_index, n), intern(normals, nrm_index, e0),
                       intern(normals, nrm_index, e1), intern(normals, nrm_index, e2)))
        kept.append(i)
        shapes.append((a, b, c))
        normals_of.append(n)
    if len(positions) > 0xFFFF or len(normals) > 0xFFFF or len(prisms) > 0xFFFE:
        raise ValueError('too many triangles for a KCL')

    # Octree over the prisms' bounds.  A triangle belongs to every cell its
    # prism (the triangle and `thickness` behind it) reaches, as in the
    # game's own files.
    swept = [p for t, n in zip(shapes, normals_of) for p in t + tuple((q[0] - n[0] * thickness, q[1] - n[1] * thickness, q[2] - n[2] * thickness) for q in t)]
    lo = [min(p[k] for p in swept) - 8.0 for k in range(3)]
    hi = [max(p[k] for p in swept) + 8.0 for k in range(3)]
    origin = [math.floor(v) for v in lo]
    bits = [max(1, math.ceil(math.log2(max(1.0, hi[k] - origin[k])))) for k in range(3)]
    shift = min(min(bits), max(int(math.log2(min_width)), min(bits) - 2))
    counts = [1 << (bits[k] - shift) for k in range(3)]

    blocks = []  # each block's words: ('node', child block) or ('leaf', prism list)
    lists = []  # identical lists are shared, as in the game's own files
    list_index = {}

    def leaf(members):
        key = tuple(members)
        if key not in list_index:
            list_index[key] = len(lists)
            lists.append(key)
        return ('leaf', list_index[key])

    def cell(x, y, z, width, members):
        half = width / 2.0 + 1.0  # a unit to spare for rounding
        center = (origin[0] + x + width / 2.0, origin[1] + y + width / 2.0, origin[2] + z + width / 2.0)
        inside = [m for m in members if _prism_box(shapes[m], normals_of[m], thickness, center, (half, half, half))]
        if len(inside) <= max_per_leaf or width <= min_width:
            return leaf(inside)
        block = len(blocks)
        blocks.append(None)
        w = width // 2
        blocks[block] = [cell(x + (i & 1) * w, y + ((i >> 1) & 1) * w, z + ((i >> 2) & 1) * w, w, inside) for i in range(8)]
        return ('node', block)

    root = []
    width = 1 << shift
    everyone = list(range(len(shapes)))
    blocks.append(None)
    for iz in range(counts[2]):
        for iy in range(counts[1]):
            for ix in range(counts[0]):
                root.append(cell(ix * width, iy * width, iz * width, width, everyone))
    blocks[0] = root

    # Layout: the blocks in order, then the prism lists (u16: a skipped
    # first entry, the prism numbers from 1, a 0 terminator).
    offsets, at = [], 0
    for words in blocks:
        offsets.append(at)
        at += 4 * len(words)
    list_offsets = []
    for members in lists:
        list_offsets.append(at)
        at += 2 * (len(members) + 2)
    octree = bytearray(at)
    for bi, words in enumerate(blocks):
        base = offsets[bi]
        for wi, (kind, target) in enumerate(words):
            if kind == 'node':
                value = offsets[target] - base
            else:
                value = 0x80000000 | (list_offsets[target] - base)
            struct.pack_into('>I', octree, base + 4 * wi, value)
    for li, members in enumerate(lists):
        struct.pack_into('>%dH' % (len(members) + 2), octree, list_offsets[li], 0, *[m + 1 for m in members], 0)

    header_size = 0x38
    pos_off = header_size
    nrm_off = pos_off + 12 * len(positions)
    prism_off = nrm_off + 12 * len(normals) - 0x10
    octree_off = prism_off + 0x10 + 16 * len(prisms)
    out = bytearray(octree_off)
    masks = [(~((1 << bits[k]) - 1)) & 0xFFFFFFFF for k in range(3)]
    xshift = bits[0] - shift
    xyshift = xshift + bits[1] - shift
    struct.pack_into('>4I4f6I', out, 0, pos_off, nrm_off, prism_off, octree_off, thickness, origin[0], origin[1], origin[2], masks[0], masks[1],
                     masks[2], shift, xshift, xyshift)
    for i, p in enumerate(positions):
        struct.pack_into('>3f', out, pos_off + 12 * i, *p)
    for i, n in enumerate(normals):
        struct.pack_into('>3f', out, nrm_off + 12 * i, *n)
    for i, (height, p, n, e0, e1, e2) in enumerate(prisms):
        struct.pack_into('>f6H', out, prism_off + 16 * (i + 1), height, p, n, e0, e1, e2, i)
    return bytes(out) + bytes(octree), kept


# ---------------------------------------------------------------------------
# Polygon attributes (.pa, a BCSV with a row per triangle)
# ---------------------------------------------------------------------------
def build_pa(old_pa, old_tris, new_tris):
    """The old attribute table with a row per new triangle, each copied from
    the old triangle whose centre is nearest.  old_tris: (a, b, c, row)."""
    n, items, data_off, entry = struct.unpack_from('>iiiI', old_pa, 0)
    strings = old_pa[data_off + n * entry:]
    table = [bytes(old_pa[data_off + i * entry:data_off + (i + 1) * entry]) for i in range(n)]
    if any(t[3] >= n for t in old_tris):
        raise ValueError('KCL attribute beyond the %d rows of the .pa' % n)
    rows = [table[t[3]] for t in old_tris]

    def centre(t):
        return tuple((t[0][k] + t[1][k] + t[2][k]) / 3.0 for k in range(3))

    # Old triangle centres in a grid, searched outwards ring by ring.
    cell = 256.0
    grid = {}
    for i, t in enumerate(old_tris):
        c = centre(t)
        grid.setdefault(tuple(int(math.floor(c[k] / cell)) for k in range(3)), []).append((c, i))
    reach = max(max(abs(k) for k in key) for key in grid) * 2 + 2 if grid else 0

    def nearest(c):
        home = tuple(int(math.floor(c[k] / cell)) for k in range(3))
        best, best_d = 0, float('inf')
        for ring in range(reach + 1):
            for dx in range(-ring, ring + 1):
                for dy in range(-ring, ring + 1):
                    for dz in range(-ring, ring + 1):
                        if max(abs(dx), abs(dy), abs(dz)) != ring:
                            continue
                        for oc, i in grid.get((home[0] + dx, home[1] + dy, home[2] + dz), ()):
                            dd = sum((oc[k] - c[k]) ** 2 for k in range(3))
                            if dd < best_d:
                                best, best_d = i, dd
            # Anything in a later ring is at least `ring` cells away.
            if best_d <= (ring * cell) ** 2:
                break
        return best

    uniform = len(set(rows)) <= 1
    out = bytearray(old_pa[:data_off])
    struct.pack_into('>i', out, 0, len(new_tris))
    for t in new_tris:
        out += rows[0] if uniform else rows[nearest(centre(t))]
    out += strings
    return bytes(out)
