"""RARC archive container: big-endian -> little-endian in place.

Layout (JKRArchive.hpp): RarcHeader (8 x u32), RarcInfoBlock (6 x u32, 2 x u16,
u32), SDIDirEntry[] (u32 id, u32 name, u16 hash, u16 count, u32 first),
SDIFileEntry[] (u16 id, u16 hash, u32 flag<<24|name, u32 data, u32 size,
u32 runtime pointer), string table.  File contents are handed to `cook_file`
before the container is converted; it may also replace a file with data of
a different size (e.g. a decompressed and converted nested archive), in
which case the data section is laid out again.
"""
from util import u16, u32, sw16, sw32

FLAG_FOLDER = 0x02
FLAG_COMPRESSED = 0x04
FLAG_MRAM = 0x10
FLAG_ARAM = 0x20
FLAG_YAZ0 = 0x80


def entries(b):
    """Yields (index, path, flags, data_offset, size) for every file entry."""
    hdr = u32(b, 8)
    data_start = hdr + u32(b, 0xC)
    info = hdr
    ndirs, dir_off, nfiles, file_off, _, str_off = (u32(b, info + 4 * i) for i in range(6))
    dir_off += info
    file_off += info
    str_off += info

    def cstr(o):
        end = b.index(b'\0', str_off + o)
        return bytes(b[str_off + o:end]).decode('shift_jis', errors='replace')

    dirs = []
    for i in range(ndirs):
        d = dir_off + 0x10 * i
        dirs.append((cstr(u32(b, d + 4)), u16(b, d + 0xA), u32(b, d + 0xC)))

    out = []

    def walk(di, prefix, depth):
        if depth > 64:
            raise ValueError('RARC directory loop')
        name, count, first = dirs[di]
        for e in range(first, first + count):
            f = file_off + 0x14 * e
            word = u32(b, f + 4)
            flags, name_off = word >> 24, word & 0xFFFFFF
            ename = cstr(name_off)
            if flags & FLAG_FOLDER:
                if ename in ('.', '..'):
                    continue
                walk(u32(b, f + 8), prefix + ename + '/', depth + 1)
            else:
                out.append((e, prefix + ename, flags, data_start + u32(b, f + 8), u32(b, f + 0xC)))

    walk(0, dirs[0][0] + '/', 0)
    return out


def _relayout(b, repl):
    """Rebuilds the data section (big-endian tables) with replaced files.
    repl: {entry index: (bytes, flags)}.  Keeps the original file order so
    the MRAM/ARAM/DVD grouping JKRCompArchive relies on is preserved."""
    hdr = u32(b, 8)
    info = hdr
    data_start = hdr + u32(b, 0xC)
    nfiles, file_off = u32(b, info + 8), info + u32(b, info + 0xC)
    files = []
    for i in range(nfiles):
        f = file_off + 0x14 * i
        flags = u32(b, f + 4) >> 24
        if not flags & FLAG_FOLDER:
            files.append((u32(b, f + 8), i, flags, u32(b, f + 0xC)))
    files.sort()
    data = bytearray()
    placed = {}
    mram = aram = 0
    for off, i, flags, size in files:
        if i in repl:
            blob, flags = repl[i]
            key = ('new', i)
        else:
            blob = b[data_start + off:data_start + off + size]
            key = (off, size)
        if key in placed:
            noff = placed[key]
        else:
            noff = len(data)
            placed[key] = noff
            data += blob
            data += b'\0' * (-len(data) & 31)
        f = file_off + 0x14 * i
        name_off = u32(b, f + 4) & 0xFFFFFF
        b[f + 4:f + 16] = ((flags << 24) | name_off).to_bytes(4, 'big') + noff.to_bytes(4, 'big') + len(blob).to_bytes(4, 'big')
        aligned = (len(blob) + 31) & ~31
        if flags & FLAG_MRAM:
            mram += aligned
        elif flags & FLAG_ARAM:
            aram += aligned
    new = bytearray(b[:data_start]) + data
    new[4:8] = len(new).to_bytes(4, 'big')
    new[0x10:0x14] = len(data).to_bytes(4, 'big')
    new[0x14:0x18] = mram.to_bytes(4, 'big')
    new[0x18:0x1C] = aram.to_bytes(4, 'big')
    return new


def swap_rarc(b, cook_file):
    """cook_file(path, blob, flags) -> None (unchanged), a bytearray of the same
    size, or (new bytes, new flags) to replace the file.  For compressed
    entries `blob` is the stored (compressed) data.  Returns the converted
    archive (a new object if the data section had to be rebuilt)."""
    repl = {}
    for index, path, flags, off, size in entries(b):
        blob = bytearray(b[off:off + size])
        new = cook_file(path, blob, flags)
        if new is None:
            continue
        if isinstance(new, tuple):
            repl[index] = new
        elif len(new) != size:
            raise ValueError('%s changed size' % path)
        else:
            b[off:off + size] = new
    if repl:
        b = _relayout(b, repl)

    hdr = u32(b, 8)
    info = hdr
    ndirs, dir_off, nfiles, file_off = (u32(b, info + 4 * i) for i in range(4))
    sw32(b, 0, 8)
    sw32(b, info, 6)
    sw16(b, info + 0x18, 2)
    sw32(b, info + 0x1C)
    for i in range(ndirs):
        d = info + dir_off + 0x10 * i
        sw32(b, d, 2)
        sw16(b, d + 8, 2)
        sw32(b, d + 0xC)
    for i in range(nfiles):
        f = info + file_off + 0x14 * i
        sw16(b, f, 2)
        sw32(b, f + 4, 4)
    return b
