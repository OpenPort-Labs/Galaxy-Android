#!/usr/bin/env python3
"""Converts an extracted Super Mario Galaxy disc into the port's data layout.

  cook.py <extracted disc (or its DATA dir)> <output dir> [--jobs N] [--only SUBSTR] [--no-audio] [--with-movies]

--with-movies adds the prologue and ending movies (2.3 GB).

Output: <out>/sys/fst.bin (copied) and <out>/files/... with every archive
decompressed and every big-endian structure the port maps directly converted
to little-endian.  Formats that the port reads as byte streams (display
lists, textures, sequences, ghost data) keep their original byte order.
<out>/sys/ also gets the data sys/main.dol holds rather than the files
(ErrorMessageArchive.arc, StoryEvent.bcsv, GalaxyID.bcsv; see dol_data.py).
"""
import argparse
import collections
import concurrent.futures
import multiprocessing
import os
import shutil
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from rarc import yaz0_decompress  # noqa: E402
import bcsv  # noqa: E402
import bmg  # noqa: E402
import collision  # noqa: E402
import dol_data  # noqa: E402
import j3d  # noqa: E402
import jaudio  # noqa: E402
import jpa  # noqa: E402
import kcl  # noqa: E402
import lyt  # noqa: E402
import rarc_swap  # noqa: E402
import thp  # noqa: E402
from util import sw32  # noqa: E402

TEXT_EXT = {'.txt', '.ini', '.csv', '.csv#', '.xnim', '.dtp', '.dck'}
UNUSED_EXT = {'.dvmat', '.dvw', '.prj', '.backup', '.bgrv', '.dat', '.gst', '.bin'}
BCSV_EXT = {'.bcsv', '.pa', '.bcam', '.banmt', '.tbl'}


def cook_blob(path, b, stats):
    """Converts one file's bytes (bytearray) in place; returns it."""
    name = path.rsplit('/', 1)[-1].lower()
    ext = os.path.splitext(name)[1]
    magic = bytes(b[:4])

    if magic == b'J3D2':
        j3d.swap_model(b)
        kind = 'j3d-model'
    elif magic == b'J3D1':
        j3d.swap_anim(b)
        kind = 'j3d-anim'
    elif ext == '.kcl' and kcl.looks_like_kcl(b):
        kcl.swap_kcl(b)
        kind = 'kcl'
    elif ext == '.bas':
        j3d.swap_sound_anim(b, 0)
        kind = 'bas'
    elif magic == b'RLYT':
        lyt.swap_rlyt(b)
        kind = 'rlyt'
    elif magic == b'RLAN':
        lyt.swap_rlan(b)
        kind = 'rlan'
    elif magic == b'RFNT':
        lyt.swap_rfnt(b)
        kind = 'rfnt'
    elif magic == b'MESG':
        bmg.swap_bmg(b)
        kind = 'bmg'
    elif ext == '.tpl' and struct.unpack_from('>I', b, 0)[0] == lyt.TPL_VERSION:
        lyt.swap_tpl(b)
        kind = 'tpl'
    elif ext == '.bti':
        lyt.swap_timg(b)
        kind = 'bti'
    elif magic == b'ANDO':
        sw32(b, 8, (len(b) - 8) // 4)
        kind = 'canm'
    elif magic == b'JPAC':
        jpa.swap_jpc(b)
        kind = 'jpc'
    elif magic == b'AA_<':
        jaudio.swap_baa(b)
        kind = 'baa'
    elif ext == '.cit':
        jaudio.swap_cit(b)
        kind = 'cit'
    elif ext == '.bmt' and magic != b'J3D2':
        jaudio.swap_me_table(b)
        kind = 'me-table'
    elif ext == '.bme':
        jaudio.swap_me_seq(b)
        kind = 'me-seq'
    elif ext == '.brs':
        jaudio.swap_words(b)
        kind = 'remix'
    elif ext == '.bct':
        jaudio.swap_spk_table(b)
        kind = 'spk-table'
    elif ext == '.csw':
        jaudio.swap_spk_wave(b)
        kind = 'spk-wave'
    elif ext == '.ast' and magic == b'STRM':
        jaudio.swap_ast(b)
        kind = 'ast'
    elif (ext in BCSV_EXT or ext == '' or ext[1:].isdigit()) and bcsv.looks_like_bcsv(b):
        bcsv.swap(b)
        kind = 'bcsv'
    elif ext in TEXT_EXT or name == 'collisionversion':
        kind = 'text'
    elif ext in UNUSED_EXT:
        kind = 'raw' + ext
    else:
        kind = 'UNKNOWN' + (ext or ':' + magic.hex())
    stats[kind] += 1
    return b


def is_converted_kind(stats_before, stats_after):
    """True if the last cook_blob call converted something (not text/raw/unknown)."""
    for k in stats_after:
        if stats_after[k] != stats_before.get(k, 0):
            return not (k == 'text' or k.startswith('raw') or k.startswith('UNKNOWN'))
    return False


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
    for mod in (j3d, lyt, jpa, jaudio):
        del mod.warnings[:]
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
            return stats, notes, None
        else:
            shutil.copyfile(src, dst)
            stats['copied'] += 1
            return stats, notes, None
        with open(dst + '.tmp', 'wb') as fh:
            fh.write(out)
        os.replace(dst + '.tmp', dst)
    except Exception as e:  # report and continue with the other files
        return stats, notes, '%s: %r' % (src, e)
    return stats, notes + j3d.warnings + lyt.warnings + jpa.warnings + jaudio.warnings, None


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
    found = len(out) == 1 + len(dol_data.TABLES)
    print('main.dol: %s' % ', '.join('%s (%d bytes)' % (n, len(d)) for n, d in out.items()))
    return [] if found else ['%s: the error message archive or a table was not found (not Super Mario Galaxy?)' % path]


def plan(data_dir, out_dir, args):
    jobs = []
    files = os.path.join(data_dir, 'files')
    for dp, _, names in os.walk(files):
        rel = os.path.relpath(dp, files).replace('\\', '/')
        top = rel.split('/')[0]
        if top == 'AudioRes' and args.no_audio:
            continue
        if top == 'MovieData' and not args.with_movies:
            continue
        for n in names:
            src = os.path.join(dp, n)
            relp = n if rel == '.' else rel + '/' + n
            if args.only and args.only.lower() not in relp.lower():
                continue
            dst = os.path.join(out_dir, 'files', relp)
            ext = os.path.splitext(n)[1].lower()
            if ext in ('.arc', '.szs'):
                mode = 'archive'
            elif ext in ('.bcsv', '.tbl', '.ast'):
                mode = 'file'
            elif ext == '.thp':
                mode = 'thp'
            else:
                mode = 'copy'
            jobs.append((src, dst, mode))
    return jobs


def convert(data_dir, out_dir, jobs=None, only=None, no_audio=False,
            with_movies=False, progress=None):
    """Run the same conversion from the CLI or the desktop installer.

    progress(completed, total) is called in the parent process, never in
    the conversion workers. A nonzero return means files were not converted.
    """
    args = argparse.Namespace(data_dir=data_dir, out_dir=out_dir,
                              jobs=jobs or os.cpu_count(), only=only,
                              no_audio=no_audio, with_movies=with_movies)
    # The game partition holds sys/ and files/; Dolphin extracts it to DATA/
    # under the folder it was given.
    if not os.path.isfile(os.path.join(args.data_dir, 'sys', 'fst.bin')):
        inner = os.path.join(args.data_dir, 'DATA')
        if not os.path.isfile(os.path.join(inner, 'sys', 'fst.bin')):
            sys.exit('%s holds no sys/fst.bin: give the folder the game partition was extracted to (with sys/ and files/)' % args.data_dir)
        args.data_dir = inner

    os.makedirs(os.path.join(args.out_dir, 'sys'), exist_ok=True)
    shutil.copyfile(os.path.join(args.data_dir, 'sys', 'fst.bin'), os.path.join(args.out_dir, 'sys', 'fst.bin'))
    errors = cook_dol_data(args.data_dir, args.out_dir)

    jobs = plan(args.data_dir, args.out_dir, args)
    if progress:
        progress(0, len(jobs))
    t0 = time.time()
    total = collections.Counter()
    notes = collections.Counter()
    # The desktop installer calls this from a thread while Tk is running.
    # Spawn fresh workers rather than forking the GUI process on Linux.
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs,
            mp_context=multiprocessing.get_context('spawn')) as ex:
        for done, (stats, n, err) in enumerate(ex.map(process, jobs, chunksize=8), 1):
            total.update(stats)
            notes.update(n)
            if err:
                errors.append(err)
            if progress and (done % 16 == 0 or done == len(jobs)):
                progress(done, len(jobs))
    print('cooked %d disc files in %.1fs' % (len(jobs), time.time() - t0))
    for k, v in sorted(total.items()):
        print('  %-24s %6d' % (k, v))
    for msg, v in notes.most_common(40):
        print('note (%dx): %s' % (v, msg))
    for e in errors:
        print('ERROR', e)
    return 1 if errors else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('data_dir')
    ap.add_argument('out_dir')
    ap.add_argument('--jobs', type=int, default=os.cpu_count())
    ap.add_argument('--only')
    ap.add_argument('--no-audio', action='store_true')
    ap.add_argument('--with-movies', action='store_true')
    return convert(**vars(ap.parse_args()))


if __name__ == '__main__':
    sys.exit(main())
