"""Conversion worker and output staging shared by the desktop and app cookers."""
import collections
import os
import shutil
import tempfile

import cook
import cook_archive
import j3d
import jaudio
import jpa
import lyt
import thp


def process(job):
    """Converts one file; returns (stats, notes, error, source path)."""
    src, dst, mode = job
    stats = collections.Counter()
    notes = []
    for mod in (j3d, lyt, jpa, jaudio):
        del mod.warnings[:]
    try:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if mode == 'archive':
            out = cook_archive.cook_archive(src, dst, stats, notes)
        elif mode == 'file':
            with open(src, 'rb') as fh:
                out = cook.cook_blob(src.replace('\\', '/'), bytearray(fh.read()), stats)
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
    except Exception as e:  # report and let the caller discard the staged tree
        return stats, notes, '%s: %r' % (src, e), src
    return stats, notes + j3d.warnings + lyt.warnings + jpa.warnings + jaudio.warnings, None, src


def stage_dir(out_dir):
    parent = os.path.dirname(os.path.abspath(out_dir)) or '.'
    os.makedirs(parent, exist_ok=True)
    return tempfile.mkdtemp(prefix='.galaxy-cook-', dir=parent)


def promote(staging, out_dir):
    """Moves finished output into place; the live tree is untouched on failure."""
    if not os.path.isdir(out_dir):
        os.replace(staging, out_dir)
        return
    for root, _, names in os.walk(staging):
        rel = os.path.relpath(root, staging)
        target = out_dir if rel == '.' else os.path.join(out_dir, rel)
        os.makedirs(target, exist_ok=True)
        for name in names:
            os.replace(os.path.join(root, name), os.path.join(target, name))
    shutil.rmtree(staging, ignore_errors=True)
