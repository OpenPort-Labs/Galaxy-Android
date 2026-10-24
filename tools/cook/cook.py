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



from cook_archive import cook_rarc, cook_archive, cook_dol_data


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
            dst = os.path.abspath(os.path.join(out_dir, 'files', relp))
            out_root = os.path.abspath(os.path.join(out_dir, 'files'))
            if not dst.startswith(out_root + os.sep) and dst != out_root:
                print('[Security Warning] Skipping traversal path in plan: %s' % relp)
                continue
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
    from cook_output import process, promote, stage_dir
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

    staging = stage_dir(args.out_dir)
    try:
        os.makedirs(os.path.join(staging, 'sys'))
        shutil.copyfile(os.path.join(args.data_dir, 'sys', 'fst.bin'),
                        os.path.join(staging, 'sys', 'fst.bin'))
        errors = cook_dol_data(args.data_dir, staging)
        if errors:
            print('[Engine] Conversion failed: %s' % errors[0])
            return 1

        jobs = plan(args.data_dir, staging, args)
        if progress:
            progress(0, len(jobs))
        t0 = time.time()
        total = collections.Counter()
        notes = collections.Counter()
        # The desktop installer calls this from a thread while Tk is running.
        # Spawn fresh workers rather than forking the GUI process on Linux.
        with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs,
                mp_context=multiprocessing.get_context('spawn')) as ex:
            results = ex.map(process, jobs, chunksize=8)
            for done, ((stats, n, err, src), job) in enumerate(zip(results, jobs), 1):
                total.update(stats)
                notes.update(n)
                if err:
                    errors.append(err)
                elif not os.path.isfile(job[1]):
                    errors.append('%s: converter reported success but wrote no output' % job[0])
                if progress and (done % 16 == 0 or done == len(jobs)):
                    progress(done, len(jobs))
        print('cooked %d disc files in %.1fs' % (len(jobs), time.time() - t0))
        for k, v in sorted(total.items()):
            print('  %-24s %6d' % (k, v))
        for msg, v in notes.most_common(40):
            print('note (%dx): %s' % (v, msg))
        if errors:
            print('[Engine] Conversion failed with %d error(s):' % len(errors))
            for e in errors:
                print('  [Error]', e)
            return 1
        if not os.path.isfile(os.path.join(staging, 'sys', 'fst.bin')):
            print('[Engine] Conversion failed: sys/fst.bin missing from the staged output')
            return 1
        try:
            promote(staging, args.out_dir)
            staging = None
        except OSError as e:
            print('[Engine] Could not move converted data into %s: %s' % (args.out_dir, e))
            return 1
    finally:
        if staging is not None:
            shutil.rmtree(staging, ignore_errors=True)
    return 0


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
