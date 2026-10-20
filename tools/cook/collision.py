"""Collision rebuilt from a model's own triangles, for the models listed in
FROM_MODEL.

Some planets collide against a stand-in mesh rather than their visible
surface.  The moon planets use a scaled generic stone (MayaStone_00 in their
groupinfo) that runs smoothly over every crater, 20-90 units above the crater
floors; the cratered asteroid of Gateway Galaxy's star chips puts a lid over
its small craters, up to 160 units above their floors.  On a TV nobody sees
it; in the VR diorama Mario visibly walks on air across each crater.  For
these archives the cook replaces the KCL with one built from the model's
triangles, and the polygon attributes (.pa) with one row per new triangle,
copied from the nearest original triangle.

Everything here reads and writes the disc's big-endian formats; the regular
conversion (kcl.swap_kcl, bcsv.swap) runs on the result.
"""
import math
import struct

# Archive (relative to files/) -> model name inside it.
import os
FROM_MODEL = {} if os.environ.get('ANDROID_ROOT') or os.environ.get('ANDROID_DATA') else {
    'ObjectData/MoonPlanet.arc': 'moonplanet',
    'ObjectData/FullMoonPlanet.arc': 'fullmoonplanet',
    'ObjectData/HeavensDoorBlackHolePlanet.arc': 'heavensdoorblackholeplanet',
}


# ---------------------------------------------------------------------------
# Vector helpers
# ---------------------------------------------------------------------------
def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def normalize(a):
    n = math.sqrt(dot(a, a))
    return (a[0] / n, a[1] / n, a[2] / n)


def f32(v):
    """Rounds to float32, as the game will read it."""
    return tuple(struct.unpack('>3f', struct.pack('>3f', *v)))


# ---------------------------------------------------------------------------
# J3D model triangles (big-endian BDL/BMD)
# ---------------------------------------------------------------------------
def _blocks(b):
    off = 0x20
    out = {}
    while off + 8 <= len(b):
        size = struct.unpack_from('>I', b, off + 4)[0]
        if size == 0:
            break
        out[bytes(b[off:off + 4])] = off
        off += size
    return out


def _joint_matrix(sx, sy, sz, rx, ry, rz, tx, ty, tz):
    ax, ay, az = [r * math.pi / 32768.0 for r in (rx, ry, rz)]
    cx, snx, cy, sny, cz, snz = math.cos(ax), math.sin(ax), math.cos(ay), math.sin(ay), math.cos(az), math.sin(az)
    r = [[cy * cz, snx * sny * cz - cx * snz, cx * sny * cz + snx * snz],
         [cy * snz, snx * sny * snz + cx * cz, cx * sny * snz - snx * cz],
         [-sny, snx * cy, cx * cy]]
    s, t = (sx, sy, sz), (tx, ty, tz)
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)]


def _concat(a, b):
    return [[a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + (a[i][3] if j == 3 else 0.0) for j in range(4)] for i in range(3)]


def _apply(m, p):
    return tuple(m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3] for i in range(3))


_IDENTITY = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]]


def _joint_world(b, blk):
    o = blk[b'JNT1']
    num = struct.unpack_from('>H', b, o + 8)[0]
    init, index = struct.unpack_from('>II', b, o + 0x0C)
    local = []
    for j in range(num):
        rec = o + init + 0x40 * struct.unpack_from('>H', b, o + index + 2 * j)[0]
        local.append(_joint_matrix(*(struct.unpack_from('>3f', b, rec + 4) + struct.unpack_from('>3h', b, rec + 0x10) +
                                     struct.unpack_from('>3f', b, rec + 0x18))))
    inf = blk[b'INF1']
    p = inf + struct.unpack_from('>I', b, inf + 0x14)[0]
    world = [_IDENTITY] * num
    parents, last = [_IDENTITY], _IDENTITY
    while True:
        kind, value = struct.unpack_from('>HH', b, p)
        p += 4
        if kind == 0:
            break
        if kind == 1:
            parents.append(last)
        elif kind == 2:
            parents.pop()
        elif kind == 0x10:
            last = world[value] = _concat(parents[-1], local[value])
    return world


def model_triangles(b):
    """Triangles (v0, v1, v2) of a rigid J3D model in model space, wound so
    that cross(v1 - v0, v2 - v0) points out of the front face (GX draws
    front faces clockwise, so the display lists' order is reversed)."""
    blk = _blocks(b)
    vtx = blk[b'VTX1']
    p = vtx + struct.unpack_from('>I', b, vtx + 8)[0]
    fmt = None
    while True:
        attr, cnt, comp, frac = struct.unpack_from('>IIIB', b, p)
        if attr == 0xFF:
            break
        if attr == 9:
            fmt = (comp, frac)
        p += 16
    comp, frac = fmt
    pos_base = vtx + struct.unpack_from('>I', b, vtx + 0x0C)[0]
    code = {0: 'B', 1: 'b', 2: 'H', 3: 'h', 4: 'f'}[comp]
    width = struct.calcsize('>' + code)
    scale = 1.0 if comp == 4 else 1.0 / (1 << frac)

    def position(i):
        return tuple(v * scale for v in struct.unpack_from('>3' + code, b, pos_base + i * 3 * width))

    world = _joint_world(b, blk)
    drw = blk[b'DRW1']
    drw_flag, drw_index = struct.unpack_from('>II', b, drw + 0x0C)
    shp = blk[b'SHP1']
    count = struct.unpack_from('>H', b, shp + 8)[0]
    o_init, _, _, o_desc, o_table, o_dl, o_mtx, o_draw = struct.unpack_from('>8I', b, shp + 0x0C)
    tris = []
    for s in range(count):
        rec = shp + o_init + 0x28 * s
        groups, desc, mtx_first, draw_first = struct.unpack_from('>4H', b, rec + 2)
        attrs = []
        q = shp + o_desc + desc
        while True:
            attr, kind = struct.unpack_from('>II', b, q)
            q += 8
            if attr == 0xFF:
                break
            attrs.append((attr, kind))
        for g in range(groups):
            use, use_count, first = struct.unpack_from('>HHI', b, shp + o_mtx + 8 * (mtx_first + g))
            size, start = struct.unpack_from('>II', b, shp + o_draw + 8 * (draw_first + g))
            table = [struct.unpack_from('>H', b, shp + o_table + 2 * (first + k))[0] for k in range(use_count)]

            def matrix(slot):
                d = table[slot] if slot < len(table) and table[slot] != 0xFFFF else use
                if b[drw + drw_flag + d] != 0:
                    raise ValueError('skinned shape %d' % s)
                return world[struct.unpack_from('>H', b, drw + drw_index + 2 * d)[0]]

            d = shp + o_dl + start
            end = d + size
            while d < end:
                cmd = b[d]
                d += 1
                if cmd == 0:
                    continue
                prim = cmd & 0xF8
                if prim not in (0x90, 0x98, 0xA0):
                    break
                n = struct.unpack_from('>H', b, d)[0]
                d += 2
                verts = []
                for _ in range(n):
                    slot, index = 0, 0
                    for attr, kind in attrs:
                        if attr <= 8:  # matrix indices: direct bytes
                            if attr == 0:
                                slot = b[d] // 3
                            d += 1
                            continue
                        if kind == 2:
                            v = b[d]
                            d += 1
                        elif kind == 3:
                            v = struct.unpack_from('>H', b, d)[0]
                            d += 2
                        elif kind == 1:
                            raise ValueError('direct vertex attribute %d' % attr)
                        else:
                            continue
                        if attr == 9:
                            index = v
                    verts.append(_apply(matrix(slot), position(index)))
                if prim == 0x90:
                    faces = [(k, k + 1, k + 2) for k in range(0, n - 2, 3)]
                elif prim == 0x98:
                    faces = [(k + 1, k, k + 2) if k & 1 else (k, k + 1, k + 2) for k in range(n - 2)]
                else:
                    faces = [(0, k, k + 1) for k in range(1, n - 1)]
                for i0, i1, i2 in faces:
                    tris.append((verts[i0], verts[i2], verts[i1]))
    return tris


# ---------------------------------------------------------------------------
# KCL (KCollisionServer) writer
# ---------------------------------------------------------------------------
def kcl_triangles(b, with_attributes=False):
    """(a, b, c) of every prism of a big-endian KCL, as the game rebuilds them
    (KCollisionServer::getPos); with_attributes: (a, b, c, attribute row)."""
    pos_off, nrm_off, prism_off, octree_off = struct.unpack_from('>4I', b, 0)

    def vec(off, i):
        return struct.unpack_from('>3f', b, off + 12 * i)

    out = []
    for i in range(1, (octree_off - prism_off) // 16):
        height, p, n, e0, e1, e2, attribute = struct.unpack_from('>f6H', b, prism_off + 16 * i)
        a, fn = vec(pos_off, p), vec(nrm_off, n)
        en0, en1, en2 = vec(nrm_off, e0), vec(nrm_off, e1), vec(nrm_off, e2)
        d1, d2 = cross(fn, en1), cross(en0, fn)
        try:
            v1 = tuple(a[k] + d1[k] * height / dot(d1, en2) for k in range(3))
            v2 = tuple(a[k] + d2[k] * height / dot(d2, en2) for k in range(3))
        except ZeroDivisionError:
            v1 = v2 = a  # degenerate
        out.append((a, v1, v2, attribute) if with_attributes else (a, v1, v2))
    return out



from kcl_builder import build_kcl, build_pa

def rebuild(files, model):
    """files: {archive path: big-endian bytes}.  Returns {path: new bytes}
    for the model's KCL and .pa."""
    prefix = model + '/' + model
    kcl_path, pa_path = prefix + '.kcl', prefix + '.pa'
    model_path = next((prefix + ext for ext in ('.bdl', '.bmd') if prefix + ext in files), None)
    if model_path is None or kcl_path not in files or pa_path not in files:
        raise ValueError('%s: model, KCL or .pa missing' % model)
    old_kcl = files[kcl_path]
    thickness = struct.unpack_from('>f', old_kcl, 0x10)[0]
    tris = model_triangles(files[model_path])
    kcl, kept = build_kcl(tris, thickness)
    pa = build_pa(files[pa_path], kcl_triangles(old_kcl, True), [tris[i] for i in kept])
    return {kcl_path: kcl, pa_path: pa}
