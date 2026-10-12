"""JAudio2 resources: big-endian -> little-endian.

SMR.baa (JAUAudioArcInterpreter command stream) and the chunks it points at:
  bst  JAUSoundTable        bstn JAUSoundNameTable     bsc  JAUSeqCollection
  ws   JASWSParser (WSYS)   bnk  JASBNKParser Ver1 (IBNK)
Sequence byte code (bms / seq collections) stays big-endian: the sequence
readers were changed to read big-endian operands.

Other formats: .cit chord tables (AudChordTable), ME table (.bmt in JaiMe,
AudMePlayingParamsHolder), ME sequence table (.bme, AudMeTable + byte code),
remix sequence data (.brs, 32-bit words).
"""
from util import u16, u32, s32, sw16, sw32, sw_range

warnings = []

DATA_SE, DATA_BGM, DATA_STREAM = 0x50, 0x60, 0x70


class Swapper:
    """Swaps each 16/32-bit location at most once (records can be shared)."""

    def __init__(self, b):
        self.b = b
        self.done = set()

    def w32(self, o, n=1):
        for i in range(n):
            p = o + 4 * i
            if p not in self.done and p + 4 <= len(self.b):
                self.done.add(p)
                sw32(self.b, p)

    def w16(self, o, n=1):
        for i in range(n):
            p = o + 2 * i
            if p not in self.done and p + 2 <= len(self.b):
                self.done.add(p)
                sw16(self.b, p)


# ---------------------------------------------------------------------------
# Sound table (BST) and name table (BSTN)
# ---------------------------------------------------------------------------
def swap_bst(b, base):
    s = Swapper(b)
    root = base + u32(b, base + 12)
    nsec = u32(b, root)
    secs = [u32(b, root + 4 + 4 * i) for i in range(nsec)]
    for so in secs:
        if not so:
            continue
        sec = base + so
        ngrp = u32(b, sec)
        grps = [u32(b, sec + 4 + 4 * i) for i in range(ngrp)]
        for go in grps:
            if not go:
                continue
            grp = base + go
            nitems = u32(b, grp)
            for i in range(nitems):
                word = u32(b, grp + 8 + 4 * i)
                typ, off = word >> 24, word & 0xFFFFFF
                if not off:
                    continue
                item = base + off
                kind = typ & 0xF0
                if kind == DATA_SE:
                    s.w16(item + 2, 2)
                elif kind == DATA_BGM:
                    s.w16(item + 2, 3)
                elif kind == DATA_STREAM:
                    s.w16(item + 2)
                    s.w32(item + 4)
                else:
                    s.w16(item + 2)
                    warnings.append('bst: item type %#x' % typ)
            s.w32(grp, 2 + nitems)
        s.w32(sec, 1 + ngrp)
    s.w32(root, 1 + nsec)
    s.w32(base, 4)


def swap_bstn(b, base):
    s = Swapper(b)
    root = base + u32(b, base + 12)
    nsec = u32(b, root)
    secs = [u32(b, root + 4 + 4 * i) for i in range(nsec)]
    for so in secs:
        if not so:
            continue
        sec = base + so
        ngrp = u32(b, sec)
        grps = [u32(b, sec + 8 + 4 * i) for i in range(ngrp)]
        for go in grps:
            if not go:
                continue
            grp = base + go
            s.w32(grp, 2 + u32(b, grp))
        s.w32(sec, 2 + ngrp)
    s.w32(root, 1 + nsec)
    s.w32(base, 4)


def swap_bsc(b, base):
    s = Swapper(b)
    ngroups = u16(b, base + 2)
    tables = [u32(b, base + 8 + 4 * i) for i in range(ngroups)]
    for to in tables:
        t = base + to
        s.w32(t, 1 + u32(b, t))
    s.w16(base + 2)
    s.w32(base + 4, 1 + ngroups)


# ---------------------------------------------------------------------------
# Wave system (WSYS)
# ---------------------------------------------------------------------------
def swap_wsys(b, base):
    s = Swapper(b)
    arc_bank = base + u32(b, base + 0x10)
    ctrl_group = base + u32(b, base + 0x14)
    # Wave archives
    narc = u32(b, arc_bank + 4)
    for i in range(narc):
        ao = u32(b, arc_bank + 8 + 4 * i)
        if not ao:
            continue
        arc = base + ao
        nwave = u32(b, arc + 0x70)
        for j in range(nwave):
            wo = u32(b, arc + 0x74 + 4 * j)
            if not wo:
                continue
            wave = base + wo
            s.w32(wave + 4, 7)      # sample rate, AW offset/length, loop flag/start/end, sample count
            s.w16(wave + 0x20, 2)   # last / penultimate sample
        s.w32(arc + 0x70, 1 + nwave)
    s.w32(arc_bank + 4, 1 + narc)
    # Control groups
    ngroups = u32(b, ctrl_group + 8)
    for i in range(ngroups):
        so = u32(b, ctrl_group + 0xC + 4 * i)
        if not so:
            continue
        scene = base + so
        for k in range(3):          # offsets at 0x4, 0x8, 0xC (only 0xC is used)
            co = u32(b, scene + 4 + 4 * k)
            if not co:
                continue
            ctrl = base + co
            if bytes(b[ctrl:ctrl + 4]) != b'C-DF' and k != 2:
                s.w32(scene + 4 + 4 * k)
                continue
            nw = u32(b, ctrl + 4)
            for j in range(nw):
                cw = u32(b, ctrl + 8 + 4 * j)
                if cw:
                    s.w32(base + cw)
            s.w32(ctrl + 4, 1 + nw)
            s.w32(scene + 4 + 4 * k)
    s.w32(ctrl_group + 8, 1 + ngroups)
    s.w32(base + 4, 5)


# ---------------------------------------------------------------------------
# Instrument bank (IBNK version 1)
# ---------------------------------------------------------------------------
def swap_ibnk(b, base):
    s = Swapper(b)
    size = u32(b, base + 4)
    version = u32(b, base + 0xC)
    if version != 1:
        warnings.append('ibnk version %d not converted' % version)
        return
    end = base + size
    chunks = {}
    order = []
    c = base + 0x20
    while c + 8 <= end:
        cid, csize = bytes(b[c:c + 4]), u32(b, c + 4)
        chunks[cid] = (c, csize)
        order.append((c, csize))
        nxt = (c + 0xB + csize) & ~3
        if nxt <= c:
            break
        c = nxt

    def effect(off):
        if not off:
            return
        e = base + off
        if e in s.done:             # shared by several instruments
            return
        tag = bytes(b[e:e + 4])
        if tag in (b'Rand', b'Sens'):
            s.w32(e + 8, 2)         # two floats; the byte fields at +4 stay
            s.w32(e)
        else:
            warnings.append('ibnk effect %r' % tag)

    def velo_regions(p, count):
        for _ in range(count):
            s.w32(p + 4, 3)         # wave id (u32), volume, pitch; byte at +0 stays
            p += 16
        return p

    if b'ENVT' in chunks:
        c, csize = chunks[b'ENVT']
        s.w16(c + 8, csize // 2)
    if b'OSCT' in chunks:
        c, _ = chunks[b'OSCT']
        n = u32(b, c + 8)
        for i in range(n):
            o = c + 0xC + 0x1C * i
            s.w32(o)                # id
            s.w32(o + 8, 5)         # f32, table offset, release table offset, scale, f32
        s.w32(c + 8)
    if b'LIST' in chunks:
        c, _ = chunks[b'LIST']
        n = u32(b, c + 8)
        for i in range(n):
            off = u32(b, c + 0xC + 4 * i)
            if not off:
                continue
            p = base + off
            tag = bytes(b[p:p + 4])
            if tag == b'Inst':
                q = p + 4
                nosc = u32(b, q)
                s.w32(q, 1 + nosc)              # osc count + osc indices
                q += 4 + 4 * nosc
                neff = u32(b, q)
                for k in range(neff):
                    effect(u32(b, q + 4 + 4 * k))
                s.w32(q, 1 + neff)              # effect count + offsets
                q += 4 + 4 * neff
                nkey = u32(b, q)
                s.w32(q)
                q += 4
                for _ in range(nkey):
                    nvel = u32(b, q + 4)
                    s.w32(q, 2)                 # high key word, velocity region count
                    q = velo_regions(q + 8, nvel)
                s.w32(q, 2)                     # volume, pitch
                s.w32(p)                        # 'Inst'
            elif tag == b'Perc':
                cnt = u32(b, p + 4)
                for k in range(cnt):
                    po = u32(b, p + 8 + 4 * k)
                    if not po:
                        continue
                    pm = base + po
                    s.w32(pm + 4, 2)        # volume, pitch
                    s.w16(pm + 0xE)         # release (pan byte at 0xC stays)
                    neff = u32(b, pm + 0x10)
                    for e in range(neff):
                        effect(u32(b, pm + 0x14 + 4 * e))
                    q = pm + 0x14 + 4 * neff
                    nvel = u32(b, q)
                    velo_regions(q + 4, nvel)
                    s.w32(q)
                    s.w32(pm + 0x10, 1 + neff)
                    s.w32(pm)
                s.w32(p, 2 + cnt)
            else:
                warnings.append('ibnk list entry %r' % tag)
        s.w32(c + 8, 1 + n)
    for c, _ in order:
        s.w32(c, 2)
    s.w32(base, 4)


# ---------------------------------------------------------------------------
# Audio archive (BAA)
# ---------------------------------------------------------------------------
CMD_ARGS = {b'ws  ': 3, b'bnk ': 2, b'bl_<': 2, b'>_bl': 0, b'bsc ': 2, b'bst ': 2, b'bstn': 2, b'bms ': 3, b'bmsa': 1, b'vbnk': 2, b'dsqb': 1,
            b'bsft': 1}


def swap_baa(b):
    if bytes(b[:4]) != b'AA_<':
        warnings.append('baa: bad magic')
        return b
    p = 4
    stream_words = [0]
    while True:
        tag = bytes(b[p:p + 4])
        stream_words.append(p)
        p += 4
        if tag == b'>_AA':
            break
        if tag == b'sect':
            p += 4                  # four u8 arguments
            continue
        n = CMD_ARGS.get(tag)
        if n is None:
            warnings.append('baa: unknown command %r' % tag)
            break
        args = [u32(b, p + 4 * i) for i in range(n)]
        stream_words.extend(p + 4 * i for i in range(n))
        p += 4 * n
        if tag == b'ws  ':
            swap_wsys(b, args[1])
        elif tag == b'bnk ':
            swap_ibnk(b, args[1])
        elif tag == b'bst ':
            swap_bst(b, args[0])
        elif tag == b'bstn':
            swap_bstn(b, args[0])
        elif tag == b'bsc ':
            swap_bsc(b, args[0])
        elif tag == b'bsft':
            warnings.append('baa: bsft not converted')
    for o in stream_words:
        sw32(b, o)
    return b


# ---------------------------------------------------------------------------
# Rhythm / ME / remix resources
# ---------------------------------------------------------------------------
def swap_cit(b):
    """AudChordTable: s32 relocated flag, 'CITS', u32, u16 chords, u16 scales,
    u32 chord offsets, u32 scale offsets; scales are {u32 up, u32 down}."""
    if bytes(b[4:8]) != b'CITS':
        warnings.append('cit: bad magic')
        return b
    nchord, nscale = u16(b, 0xC), u16(b, 0xE)
    scales = [u32(b, 0x10 + 4 * (nchord + i)) for i in range(nscale)]
    s = Swapper(b)
    for so in scales:
        s.w32(so, 2)
    s.w32(0x10, nchord + nscale)
    s.w16(0xC, 2)
    s.w32(0)
    s.w32(8)
    return b


def swap_me_table(b):
    """AudMePlayingParamsHolder: s32 count, entry offset, names offset;
    8-byte entries (u16 at +4); u32 name offsets."""
    n, entries, names = s32(b, 0), s32(b, 4), s32(b, 8)
    for i in range(n):
        sw16(b, entries + 8 * i + 4)
    sw32(b, names, n)
    sw32(b, 0, 3)
    return b


def swap_me_seq(b):
    """AudMeTable: s32 count, s32 start positions; byte code follows."""
    n = s32(b, 0)
    sw32(b, 0, 1 + n)
    return b


def swap_words(b):
    sw32(b, 0, len(b) // 4)
    return b


def swap_spk_table(b):
    """SpkTable (.bct): s32 count, entry offset, names offset, initialized;
    8-byte SpkParameters (u16, u8, u8, u16, u16); u32 name offsets."""
    n, entries, names = s32(b, 0), s32(b, 4), s32(b, 8)
    for i in range(n):
        e = entries + 8 * i
        sw16(b, e)
        sw16(b, e + 4, 2)
    sw32(b, names, n)
    sw32(b, 0, 4)
    return b


def swap_spk_wave(b):
    """SpkWave (.csw): u32, u32 count, u32 wave offsets; waves are u32 size,
    loop start, loop end, then s16 PCM samples."""
    n = u32(b, 4)
    offs = [u32(b, 8 + 4 * i) for i in range(n)]
    ends = sorted(set(offs + [len(b)]))
    for o in offs:
        end = ends[ends.index(o) + 1] if o in ends else len(b)
        sw32(b, o, 3)
        sw_range(b, o + 12, end, 2)
    sw32(b, 0, 2 + n)
    return b


def swap_ast(b):
    """Stream (.ast, JASAramStream): the 0x40-byte STRM header's u16 channels
    and loop flag and its u32 sample rate / sample count / loop points / block
    size; then each block's 0x20-byte header (u32 per-channel size, six s16
    ADPCM history pairs).  Tags, byte fields and the samples stay as they are
    (the DSP reads sample data big-endian)."""
    channels = u16(b, 0x0C)
    sw16(b, 0x0C, 2)
    sw32(b, 0x10, 5)
    o = 0x40
    while o + 0x20 <= len(b):
        size = u32(b, o + 4)
        sw32(b, o + 4)
        sw16(b, o + 8, 12)
        o += 0x20 + size * channels
    # The last block's size is rounded up past the end of the file.
    if o < len(b) or o > len(b) + 0x20 * channels:
        warnings.append('ast: blocks end at %#x, file is %#x bytes' % (o, len(b)))
    return b
