"""Finds the data the game's executable holds rather than its files.

The port runs its own executable, built from the decompilation, which
leaves out three pieces of data that sit in the original main.dol: the
error message archive (the disc error screens' texts, font and window) and
the StoryEvent and GalaxyID tables (BCSV).  cook.py takes them from the
player's own sys/main.dol; the app loads them from the converted sys/
folder (platform/src/dvd/dol_data.cpp).

The archive is the RARC holding the error window's layout, the tables the
BCSV tables with their known columns (JMap hashes of the names the game's
code looks them up by).
"""
import struct

from rarc import rarc_list

ERROR_WINDOW = 'layoutdata/errormessagewindow.arc'
# name -> (column count, columns the game reads)
TABLES = {
    'StoryEvent': (2, ('name', 'progress')),
    'GalaxyID': (7, ('name', 'MapPaneName', 'PowerStarNum')),
}


def jmap_hash(name):
    h = 0
    for c in name.encode():
        c = c - 256 if c >= 128 else c
        h = (c + h * 31) & 0xFFFFFFFF
    return h


def find_error_archive(dol):
    """The error message archive (big-endian RARC with Yaz0 files), or None."""
    pos = dol.find(b'RARC')
    while pos >= 0:
        size = struct.unpack_from('>I', dol, pos + 4)[0]
        if 0x40 <= size <= len(dol) - pos:
            try:
                names = [p.lower().split('/', 1)[-1] for p, _ in rarc_list(dol[pos:pos + size])]
            except Exception:  # not an archive after all
                names = []
            if ERROR_WINDOW in names:
                return bytes(dol[pos:pos + size])
        pos = dol.find(b'RARC', pos + 4)
    return None


def _table_at(dol, pos, columns, wanted):
    """The table whose header starts at pos, if it is the one sought."""
    if pos < 0 or pos + 16 + 12 * columns > len(dol):
        return None
    rows, fields, data, entry = struct.unpack_from('>iiii', dol, pos)
    if fields != columns or data != 16 + 12 * fields or not 0 < rows < 4096 or not 0 < entry <= 1024:
        return None
    descs = [struct.unpack_from('>IIHBB', dol, pos + 16 + 12 * i) for i in range(fields)]
    if not wanted <= {d[0] for d in descs}:
        return None
    # The table runs to the end of its string pool, padded to 32 bytes.
    strings = pos + data + rows * entry
    end = strings
    for row in range(rows):
        for _, _, offset, _, kind in descs:
            if kind == 6:  # string: offset into the pool
                at = strings + struct.unpack_from('>I', dol, pos + data + row * entry + offset)[0]
                end = max(end, dol.index(b'\0', at) + 1)
    end = pos + ((end - pos + 31) & ~31)
    return bytes(dol[pos:min(end, len(dol))])


def find_table(dol, name):
    """Table `name` of TABLES (big-endian BCSV), or None."""
    columns, names = TABLES[name]
    wanted = {jmap_hash(n) for n in names}
    key = struct.pack('>I', jmap_hash(names[0]))
    hit = dol.find(key)
    while hit >= 0:
        # The hash is the first word of one of the column descriptors.
        for i in range(columns):
            table = _table_at(dol, hit - 16 - 12 * i, columns, wanted)
            if table:
                return table
        hit = dol.find(key, hit + 1)
    return None
