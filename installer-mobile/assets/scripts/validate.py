#!/usr/bin/env python3
"""Sanity-checks cooked (little-endian) data: re-walks every RARC container
and the block chains / headers of the main formats using little-endian reads."""
import collections
import os
import struct
import sys


def le32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def le16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def rarc_files(b):
    hdr = le32(b, 8)
    data_start = hdr + le32(b, 0xC)
    ndirs, dir_off, nfiles, file_off, _, str_off = (le32(b, hdr + 4 * i) for i in range(6))
    str_off += hdr

    def cstr(o):
        return bytes(b[str_off + o:b.index(b'\0', str_off + o)]).decode('shift_jis', 'replace')

    dirs = [(cstr(le32(b, hdr + dir_off + 16 * i + 4)), le16(b, hdr + dir_off + 16 * i + 0xA), le32(b, hdr + dir_off + 16 * i + 0xC))
            for i in range(ndirs)]
    out = []

    def walk(di, prefix):
        name, count, first = dirs[di]
        for e in range(first, first + count):
            f = hdr + file_off + 0x14 * e
            word = le32(b, f + 4)
            flags, name_off = word >> 24, word & 0xFFFFFF
            ename = cstr(name_off)
            if flags & 2:
                if ename not in ('.', '..'):
                    walk(le32(b, f + 8), prefix + ename + '/')
            else:
                off, size = data_start + le32(b, f + 8), le32(b, f + 0xC)
                if off + size > len(b):
                    raise ValueError('file %s outside archive' % ename)
                out.append((prefix + ename, flags, b[off:off + size]))

    walk(0, dirs[0][0] + '/')
    return out


def check_blob(path, b, problems):
    m = b[:4]
    if m in (b'2D3J', b'1D3J'):  # swapped magic reads as 'J3D2'/'J3D1'
        nblocks = le32(b, 0xC)
        o = 0x20
        for _ in range(nblocks):
            size = le32(b, o + 4)
            if size < 8 or o + size > len(b) + 0x20:
                problems.append('%s: bad J3D block chain at %#x' % (path, o))
                return
            o += size
    elif path.endswith('.kcl'):
        pos, nrm, prism, octree = struct.unpack_from('<4I', b, 0)
        if not (pos == 0x38 and pos <= nrm <= prism + 0x10 <= octree <= len(b)):
            problems.append('%s: bad KCL header' % path)


def main(root):
    problems = []
    counts = collections.Counter()
    for dp, _, names in os.walk(root):
        for n in names:
            if not n.endswith(('.arc', '.szs')):
                continue
            p = os.path.join(dp, n)
            b = open(p, 'rb').read()
            if b[:4] != b'CRAR':
                counts['non-rarc'] += 1
                continue
            try:
                for path, flags, blob in rarc_files(b):
                    counts['files'] += 1
                    check_blob(path, blob, problems)
            except Exception as e:
                problems.append('%s: %r' % (p, e))
            counts['archives'] += 1
    print(dict(counts))
    for pr in problems[:50]:
        print('PROBLEM', pr)
    print('%d problems' % len(problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
