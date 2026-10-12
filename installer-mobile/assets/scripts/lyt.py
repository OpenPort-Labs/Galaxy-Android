"""NW4R layout (RLYT / RLAN), TPL and RFNT: big-endian -> little-endian.

Field layouts follow libs/nw4r/include/nw4r/lyt/resources.h, lyt/types.h,
ut/fontResourceFormat.h and RVL_SDK tpl.h.  Texture, palette and font sheet
images stay big-endian (the GPU emulation decodes them as such).  Colors
stored as u32 (vertex/text colors) are swapped as values: ut::Color's u32
conversions are byte-order aware in the port.
"""
from util import u16, u32, sw16, sw32, sw_range

warnings = []


def warn(msg):
    warnings.append(msg)


def swap_file_header(b):
    """nw4r BinaryFileHeader: sig, u16 bom, u16 version, u32 size, u16 hdr, u16 blocks."""
    sw32(b, 0)
    sw16(b, 4, 2)
    sw32(b, 8)
    sw16(b, 0xC, 2)


def blocks(b):
    """Yields (kind, offset, size) of each data block, reading big-endian."""
    o = u16(b, 0xC)
    for _ in range(u16(b, 0xE)):
        if o + 8 > len(b):
            break
        kind, size = bytes(b[o:o + 4]), u32(b, o + 4)
        yield kind, o, size
        if size < 8:
            break
        o += size


# ---------------------------------------------------------------------------
# Text with message tags (UTF-16 + 0x1A escapes).  After the 0x001A code unit:
# u8 total length (bytes, including the 0x001A), u8 group, u16 tag, params.
# Param layout per group follows the game's MessageEditorMessageTag users.
# ---------------------------------------------------------------------------
def swap_text(b, start, end):
    p = start
    while p + 2 <= end:
        c = u16(b, p)
        sw16(b, p)
        if c != 0x1A:
            p += 2
            continue
        if p + 6 > end:
            break
        length, group = b[p + 2], b[p + 3]
        sw16(b, p + 4)                    # tag id
        params, pend = p + 6, p + length
        if group == 1 and length >= 8:    # wait etc.: u16 param
            sw16(b, params)
        elif group == 2:                  # sound name: UTF-16 string
            sw_range(b, params, pend, 2)
        elif group in (6, 7) and length >= 14:  # number / string slot + u32 index
            sw32(b, params, 2)
        elif group in (5, 255) or length <= 6:
            pass                          # u8 params
        else:
            warn('text tag group %d length %d left unconverted' % (group, length))
        if length < 6:
            warn('bad text tag length %d' % length)
            break
        p += length


# ---------------------------------------------------------------------------
# RLYT
# ---------------------------------------------------------------------------
def material_size_and_swap(b, m, end):
    bits = u32(b, m + 0x3C)
    tex_map = bits & 0xF
    tex_srt = (bits >> 4) & 0xF
    tex_gen = (bits >> 8) & 0xF
    has_swap = (bits >> 12) & 1
    ind_srt = (bits >> 13) & 3
    ind_stage = (bits >> 15) & 7
    tev_stage = (bits >> 18) & 0x1F
    has_alpha = (bits >> 23) & 1
    has_blend = (bits >> 24) & 1
    chan_ctrl = (bits >> 25) & 1
    mat_col = (bits >> 27) & 1

    sw16(b, m + 0x14, 12)       # tevCols: 3 x GXColorS10
    sw32(b, m + 0x3C)           # resNum bits
    o = m + 0x40
    for _ in range(tex_map):    # TexMap: u16 texIdx, u8, u8
        sw16(b, o)
        o += 4
    sw32(b, o, 5 * tex_srt)     # TexSRT: 5 x f32
    o += 20 * tex_srt
    o += 4 * tex_gen            # TexCoordGen: u8 x4
    o += 4 * chan_ctrl          # ChanCtrl: u8 x4
    o += 4 * mat_col            # ut::Color (bytes)
    o += 4 * has_swap           # TevSwapMode x4 (bytes)
    sw32(b, o, 5 * ind_srt)
    o += 20 * ind_srt
    o += 4 * ind_stage          # IndirectStage: u8 x4
    o += 16 * tev_stage         # TevStage: u8 x16
    o += 4 * has_alpha
    o += 4 * has_blend
    return o - m


def pane_base(b, o):
    sw32(b, o + 0x24, 10)       # translate(3) rotate(3) scale(2) size(2)


def swap_tex_coords(b, o, n):
    sw32(b, o, 8 * n)           # n x 4 x VEC2


def swap_rlyt(b):
    blist = list(blocks(b))
    swap_file_header(b)
    for kind, o, size in blist:
        end = o + size
        if kind == b'lyt1':
            sw32(b, o + 0xC, 2)
        elif kind in (b'txl1', b'fnl1'):
            n = u16(b, o + 8)
            sw16(b, o + 8)
            for i in range(n):
                sw32(b, o + 0xC + 8 * i)
        elif kind == b'mat1':
            n = u16(b, o + 8)
            offs = [u32(b, o + 0xC + 4 * i) for i in range(n)]
            sw16(b, o + 8)
            sw32(b, o + 0xC, n)
            for i, mo in enumerate(offs):
                nxt = offs[i + 1] if i + 1 < n else size
                used = material_size_and_swap(b, o + mo, end)
                if used != nxt - mo and not (i + 1 == n and 0 <= (nxt - mo) - used < 4):
                    warn('rlyt material %d size %d != %d' % (i, used, nxt - mo))
        elif kind == b'pan1' or kind == b'bnd1':
            pane_base(b, o)
        elif kind == b'pic1':
            ntc = b[o + 0x5E]
            pane_base(b, o)
            sw32(b, o + 0x4C, 4)
            sw16(b, o + 0x5C)
            swap_tex_coords(b, o + 0x60, ntc)
        elif kind == b'txt1':
            str_bytes = u16(b, o + 0x4E)
            str_off = u32(b, o + 0x58)
            pane_base(b, o)
            sw16(b, o + 0x4C, 4)
            sw32(b, o + 0x58, 3)        # textStrOffset, textCols[2]
            sw32(b, o + 0x64, 4)        # fontSize, charSpace, lineSpace
            if str_bytes:
                swap_text(b, o + str_off, min(end, o + str_off + str_bytes))
        elif kind == b'wnd1':
            nframes = b[o + 0x5C]
            content, frames = u32(b, o + 0x60), u32(b, o + 0x64)
            pane_base(b, o)
            sw32(b, o + 0x4C, 4)
            sw32(b, o + 0x60, 2)
            c = o + content
            ntc = b[c + 0x12]
            sw32(b, c, 4)
            sw16(b, c + 0x10)
            swap_tex_coords(b, c + 0x14, ntc)
            for i in range(nframes):
                fo = u32(b, o + frames + 4 * i)
                sw32(b, o + frames + 4 * i)
                sw16(b, o + fo)
        elif kind == b'grp1':
            sw16(b, o + 0x18)
        elif kind in (b'pas1', b'pae1', b'grs1', b'gre1'):
            pass
        else:
            warn('rlyt block %r not converted' % kind)
        sw32(b, o, 2)
    return b


# ---------------------------------------------------------------------------
# RLAN
# ---------------------------------------------------------------------------
def swap_rlan(b):
    blist = list(blocks(b))
    swap_file_header(b)
    for kind, o, size in blist:
        if kind == b'pat1':
            sw16(b, o + 8, 2)
            sw32(b, o + 0xC, 2)
            sw16(b, o + 0x14, 2)
        elif kind == b'pah1':
            sw32(b, o + 8)
            sw16(b, o + 0xC)
        elif kind == b'pai1':
            file_num = u16(b, o + 0xC)
            cont_num = u16(b, o + 0xE)
            cont_offs_at = o + u32(b, o + 0x10)
            conts = [u32(b, cont_offs_at + 4 * i) for i in range(cont_num)]
            sw16(b, o + 8)
            sw16(b, o + 0xC, 2)
            sw32(b, o + 0x10)
            sw32(b, o + 0x14, file_num)
            sw32(b, cont_offs_at, cont_num)
            for co in conts:
                c = o + co
                ninfo = b[c + 20]
                infos = [u32(b, c + 24 + 4 * i) for i in range(ninfo)]
                sw32(b, c + 24, ninfo)
                for io in infos:
                    info = c + io
                    ntgt = b[info + 4]
                    tgts = [u32(b, info + 8 + 4 * i) for i in range(ntgt)]
                    sw32(b, info)           # kind
                    sw32(b, info + 8, ntgt)
                    for to in tgts:
                        t = info + to
                        curve = b[t + 2]
                        nkeys = u16(b, t + 4)
                        keys = t + u32(b, t + 8)
                        sw16(b, t + 4)
                        sw32(b, t + 8)
                        if curve == 1:      # step: f32 frame, u16 value, u16 pad
                            for k in range(nkeys):
                                sw32(b, keys + 8 * k)
                                sw16(b, keys + 8 * k + 4)
                        elif curve == 2:    # hermite: 3 x f32
                            sw32(b, keys, 3 * nkeys)
                        else:
                            warn('rlan curve type %d not converted' % curve)
        else:
            warn('rlan block %r not converted' % kind)
        sw32(b, o, 2)
    return b


# ---------------------------------------------------------------------------
# TPL (texture palette) and BTI (ResTIMG)
# ---------------------------------------------------------------------------
TPL_VERSION = 2142000


def swap_tpl(b):
    n, table = u32(b, 4), u32(b, 8)
    descs = [(u32(b, table + 8 * i), u32(b, table + 8 * i + 4)) for i in range(n)]
    sw32(b, 0, 3)
    sw32(b, table, 2 * n)
    for tex, clut in descs:
        if tex:
            sw16(b, tex, 2)             # height, width
            sw32(b, tex + 4, 7)         # format, data, wrapS/T, min/mag filter, LODBias
        if clut:
            sw16(b, clut)               # numEntries
            sw32(b, clut + 4, 2)        # format, data
    return b


def swap_timg(b, o=0):
    sw16(b, o + 0x02, 2)
    sw16(b, o + 0x0A)
    sw32(b, o + 0x0C)
    sw16(b, o + 0x1A)
    sw32(b, o + 0x1C)
    return b


# ---------------------------------------------------------------------------
# RFNT (nw4r::ut::ResFont)
# ---------------------------------------------------------------------------
def swap_rfnt(b):
    blist = list(blocks(b))
    swap_file_header(b)
    for kind, o, size in blist:
        d = o + 8
        if kind == b'FINF':
            sw16(b, d + 2)
            sw32(b, d + 8, 3)
        elif kind == b'TGLP':
            sw32(b, d + 4)
            sw16(b, d + 8, 6)
            sw32(b, d + 0x14)
        elif kind == b'CWDH':
            sw16(b, d, 2)
            sw32(b, d + 4)
        elif kind == b'CMAP':
            sw16(b, d, 4)
            sw32(b, d + 8)
            sw_range(b, d + 0xC, o + size, 2)
        else:
            warn('rfnt block %r not converted' % kind)
        sw32(b, o, 2)
    return b
