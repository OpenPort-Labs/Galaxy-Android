import os
import collections
import rarc_swap
import collision
import dol_data
from cook import cook_blob, is_converted_kind, yaz0_decompress

def cook_rarc(data, stats, replace=None):
    """Converts a (decompressed, big-endian) RARC; nested compressed files that
    need conversion are decompressed, converted and stored uncompressed.
    replace: {path: big-endian bytes} to convert in place of those files."""
    def cook_file(path, blob, flags):
        if replace and path in replace:
            stats['collision-from-model'] += 1
            return (cook_blob(path, bytearray(replace[path]), stats), flags & ~(rarc_swap.FLAG_COMPRESSED | rarc_swap.FLAG_YAZ0))
        if not flags & rarc_swap.FLAG_COMPRESSED:
            return cook_blob(path, blob, stats)
        if not flags & rarc_swap.FLAG_YAZ0 or bytes(blob[:4]) != b'Yaz0':
            stats['nested-yay0-kept'] += 1
            return None
        inner = bytearray(yaz0_decompress(bytes(blob)))
        plain = flags & ~(rarc_swap.FLAG_COMPRESSED | rarc_swap.FLAG_YAZ0)
        if inner[:4] == b'RARC':
            stats['nested-rarc'] += 1
            return (cook_rarc(inner, stats), plain)
        before = collections.Counter(stats)
        cooked = cook_blob(path, inner, stats)
        if is_converted_kind(before, stats):
            stats['nested-decompressed'] += 1
            return (cooked, plain)
        stats['nested-compressed-kept'] += 1
        return None

    return rarc_swap.swap_rarc(data, cook_file)


def cook_archive(src, dst, stats, notes):
    raw = open(src, 'rb').read()
    data = bytearray(yaz0_decompress(raw))
    if data[:4] == b'AA_<':
        return cook_blob(src.replace('\\', '/'), data, stats)
    if data[:4] != b'RARC':
        notes.append('%s: not a RARC archive (%r), copied decompressed' % (src, bytes(data[:4])))
        return data
    # Planets whose collision is rebuilt from their model (collision.py).
    replace = None
    rel = src.replace('\\', '/')
    for arc, model in collision.FROM_MODEL.items():
        if rel.endswith('/' + arc):
            files = {path: bytes(data[off:off + size]) for _, path, flags, off, size in rarc_swap.entries(data)
                     if not flags & rarc_swap.FLAG_COMPRESSED}
            replace = collision.rebuild(files, model)
    return cook_rarc(data, stats, replace)


def process(job):
    src, dst, mode = job
    stats = collections.Counter()
    notes = []
    try:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if mode == 'archive':
            out = cook_archive(src, dst, stats, notes)
        elif mode == 'file':
            out = cook_blob(src.replace('\\', '/'), bytearray(open(src, 'rb').read()), stats)
        elif mode == 'thp':
            thp.cook_thp(src, dst + '.tmp')
            os.replace(dst + '.tmp', dst)
            stats['movies'] += 1
            return stats, notes, None, src
        else:
            shutil.copyfile(src, dst)
            stats['copied'] += 1
            return stats, notes, None, src
        with open(dst + '.tmp', 'wb') as fh:
            fh.write(out)
        os.replace(dst + '.tmp', dst)
    except Exception as e:  # report and continue with the other files
        err_msg = '%s: %r' % (src, e)
        print('[Cook Warning]', err_msg)
        try:
            shutil.copyfile(src, dst)
            stats['copied-fallback'] += 1
        except Exception:
            pass
        return stats, notes, err_msg, src
    return stats, notes, None, src


def cook_dol_data(data_dir, out_dir):
    """Writes the data main.dol holds (dol_data.py) to <out>/sys; returns the
    errors."""
    path = os.path.join(data_dir, 'sys', 'main.dol')
    if not os.path.isfile(path):
        return ['%s is missing: extract the whole disc (its sys/ and files/ folders)' % path]
    dol = open(path, 'rb').read()
    out = {}
    archive = dol_data.find_error_archive(dol)
    if archive:
        out['ErrorMessageArchive.arc'] = cook_rarc(bytearray(archive), collections.Counter())
    for name in dol_data.TABLES:
        table = dol_data.find_table(dol, name)
        if table:
            table = bytearray(table)
            bcsv.swap(table)
            out[name + '.bcsv'] = table
    for name, data in out.items():
        with open(os.path.join(out_dir, 'sys', name), 'wb') as fh:
            fh.write(data)
