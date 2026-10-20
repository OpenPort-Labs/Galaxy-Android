import struct
from jaudio import u32, warnings, Swapper

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


