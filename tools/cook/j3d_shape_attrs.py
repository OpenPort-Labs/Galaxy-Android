"""J3D shape -> vertex-attribute flags (SHP1/DLW1 side of the pipeline key).

The native renderer stores the vertex layout of every draw in ``Item::flags``
(see ``platform/src/gx/gx_record.h``), derived from the GX vertex descriptor the
game programs from the model's shape.  Prewarming therefore has to reproduce
that layout from the asset data; guessing a constant produces keys the runtime
never asks for.

Authoritative layout (``decomp/libs/JSystem``):

    J3DShapeBlock  : u16 mShapeNum, then 8 pointers
                     [init, index, name, vtxDesc, mtxTable, dl, mtxInit, draw]
    J3DShapeInitData (0x28): u8 mShapeMtxType, pad, u16 mMtxGroupNum,
                     u16 mVtxDescListIndex, u16 mMtxInitDataIndex,
                     u16 mDrawInitDataIndex, u16 pad, f32 radius, Vec min, Vec max
    shape s uses init record ``index[s]`` and the descriptor list at
    ``vtxDesc + mVtxDescListIndex`` (a *byte* offset), a list of
    (GXAttr, GXAttrType) u32 pairs terminated by GXAttr 0xFF.

Material n is drawn with shape n: the engine keeps material and shape packets in
the same index order, and every shipped model has one shape per material.  A
mismatch is reported instead of guessed.
"""

import struct

# Mirrors gpu::VtxFlags in platform/src/gx/gx_record.h.
VF_POSMTX = 1 << 0
VF_NRM = 1 << 1
VF_NBT = 1 << 2
VF_CLR0 = 1 << 3
VF_CLR1 = 1 << 4
VF_TEX0 = 1 << 5      # VF_TEX0 << n for texture coordinate n
VF_TEXMTX = 1 << 13

VALID_VF_MASK = VF_POSMTX | VF_NRM | VF_NBT | VF_CLR0 | VF_CLR1 | VF_TEXMTX | (0xFF << 5)

# GXAttr (GXAttr.h): 0 PNMTXIDX, 1..8 TEXnMTXIDX, 9 POS, 10 NRM, 11 CLR0,
# 12 CLR1, 13..20 TEX0..TEX7, 21 NBT.
GXA_PNMTXIDX = 0
GXA_TEX0MTXIDX = 1
GXA_TEX7MTXIDX = 8
GXA_POS = 9
GXA_NRM = 10
GXA_CLR0 = 11
GXA_CLR1 = 12
GXA_TEX0 = 13
GXA_TEX7 = 20
GXA_NBT = 21

SHP_INIT_SIZE = 0x28
MAX_ATTRS = 32


class ShapeAttrError(Exception):
    """Malformed SHP1 data: callers must skip the model, not guess flags."""


def model_blocks(b):
    """Returns (is_big_endian, {tag: block_offset}) from the J3D block chain."""
    if len(b) < 0x20:
        raise ShapeAttrError("model shorter than its header")
    magic = b[:4]
    if magic in (b'J3D2', b'J3D1'):
        be, fmt = True, '>I'
    elif magic in (b'2D3J', b'1D3J'):
        be, fmt = False, '<I'
    else:
        raise ShapeAttrError("not a J3D model (magic %r)" % magic)

    count = struct.unpack_from(fmt, b, 0x0C)[0]
    if count == 0 or count > 64:
        raise ShapeAttrError("invalid block count %d" % count)

    blocks = {}
    offset = 0x20
    for _ in range(count):
        if offset + 8 > len(b):
            raise ShapeAttrError("block chain runs past end of model")
        tag = b[offset:offset + 4]
        size = struct.unpack_from(fmt, b, offset + 4)[0]
        if size <= 0 or offset + size > len(b):
            raise ShapeAttrError("block %r has invalid size %d" % (tag, size))
        blocks[bytes(reversed(tag)) if not be else tag] = offset
        offset += size
    return be, blocks


def attrs_to_flags(attrs):
    """Maps a GXAttr set to gpu::VtxFlags bits."""
    flags = 0
    for a in attrs:
        if a == GXA_PNMTXIDX:
            flags |= VF_POSMTX
        elif GXA_TEX0MTXIDX <= a <= GXA_TEX7MTXIDX:
            flags |= VF_TEXMTX
        elif a == GXA_NRM:
            flags |= VF_NRM
        elif a == GXA_NBT:
            flags |= VF_NRM | VF_NBT
        elif a == GXA_CLR0:
            flags |= VF_CLR0
        elif a == GXA_CLR1:
            flags |= VF_CLR1
        elif GXA_TEX0 <= a <= GXA_TEX7:
            flags |= VF_TEX0 << (a - GXA_TEX0)
    return flags


def _read_vtx_desc(b, base, offset, fmt, limit):
    """Reads one GXVtxDescList; returns its GXAttr set."""
    pair = fmt[0] + '2I'
    attrs = set()
    p = base + offset
    for _ in range(MAX_ATTRS):
        if p + 8 > limit:
            raise ShapeAttrError("vertex descriptor list overruns SHP1")
        attr, _kind = struct.unpack_from(pair, b, p)
        p += 8
        if attr == 0xFF:
            return attrs
        if attr > GXA_NBT:
            raise ShapeAttrError("invalid GXAttr %d in vertex descriptor" % attr)
        attrs.add(attr)
    raise ShapeAttrError("vertex descriptor list is not 0xFF terminated")


def shape_flags(b, be, blocks):
    """Per-shape VtxFlags list, plus the GXAttr set of each shape.

    ``shape_flags[i]`` is the layout material i is drawn with.
    """
    if b'SHP1' not in blocks:
        raise ShapeAttrError("model has no SHP1 block")

    fmt32 = '>I' if be else '<I'
    fmt16 = '>H' if be else '<H'
    shp = blocks[b'SHP1']
    size = struct.unpack_from(fmt32, b, shp + 4)[0]
    end = shp + size
    num = struct.unpack_from(fmt16, b, shp + 8)[0]
    if num == 0:
        raise ShapeAttrError("SHP1 declares zero shapes")

    off_init, off_index, _name, off_desc = [struct.unpack_from(fmt32, b, shp + 0x0C + 4 * i)[0]
                                            for i in range(4)]
    if not off_init or not off_index or not off_desc:
        raise ShapeAttrError("SHP1 is missing its init/index/vtxDesc tables")

    out_flags, out_attrs = [], []
    for s in range(num):
        rec_index = struct.unpack_from(fmt16, b, shp + off_index + 2 * s)[0]
        rec = shp + off_init + rec_index * SHP_INIT_SIZE
        if rec + SHP_INIT_SIZE > end:
            raise ShapeAttrError("shape %d init record out of range" % s)
        desc_off = struct.unpack_from(fmt16, b, rec + 0x04)[0]
        attrs = _read_vtx_desc(b, shp + off_desc, desc_off, fmt32, end)
        out_attrs.append(attrs)
        out_flags.append(attrs_to_flags(attrs))
    return out_flags, out_attrs


def material_vertex_flags(b, material_count):
    """VtxFlags for each material index of a model, or raises ShapeAttrError."""
    be, blocks = model_blocks(b)
    flags, _ = shape_flags(b, be, blocks)
    if len(flags) != material_count:
        raise ShapeAttrError("shape count %d does not match material count %d"
                             % (len(flags), material_count))
    return flags
