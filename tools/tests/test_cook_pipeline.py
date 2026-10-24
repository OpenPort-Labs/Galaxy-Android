"""End-to-end cooking tests using a synthetic disc; no game data is needed."""
import contextlib
import io
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
COOK = TOOLS / 'cook'
MOBILE_SCRIPTS = TOOLS.parent / 'installer-mobile' / 'assets' / 'scripts'
sys.path.insert(0, str(COOK))
sys.path.insert(0, str(TOOLS))

import cook  # noqa: E402
import dol_data  # noqa: E402

STRING_PTR = 6


def _rarc(folder, name, payload):
    """Minimal big-endian RARC holding one folder and one uncompressed file."""
    strings = b'\0' + folder.encode() + b'\0' + name.encode() + b'\0'
    folder_off = 1
    name_off = 1 + len(folder) + 1
    dirs_at, files_at = 0x40, 0x60
    strings_at = files_at + 2 * 0x14
    data_at = (strings_at + len(strings) + 0x1F) & ~0x1F
    total = data_at + len(payload)
    out = bytearray(total)
    out[0:4] = b'RARC'
    struct.pack_into('>IIIIII', out, 4, total, 0x20, data_at - 0x20, len(payload), 0, 0)
    struct.pack_into('>IIIIII', out, 0x20, 2, dirs_at - 0x20, 2, files_at - 0x20,
                     len(strings), strings_at - 0x20)
    struct.pack_into('>IIHHI', out, dirs_at, 0, 0, 0, 1, 0)
    struct.pack_into('>IIHHI', out, dirs_at + 0x10, 1, folder_off, 0, 1, 1)
    struct.pack_into('>HHIIII', out, files_at, 0, 0, (0x02 << 24) | folder_off, 1, 0x10, 0)
    struct.pack_into('>HHIIII', out, files_at + 0x14, 1, 0, name_off, 0, len(payload), 0)
    out[strings_at:strings_at + len(strings)] = strings
    out[data_at:] = payload
    return bytes(out)


def _bcsv(columns):
    """One-row big-endian BCSV; columns are (name, type) at 4-byte slots."""
    fields = len(columns)
    entry = 4 * fields
    data_at = 16 + 12 * fields
    body = bytearray(data_at + entry + 1)
    struct.pack_into('>iiii', body, 0, 1, fields, data_at, entry)
    for i, (name, kind) in enumerate(columns):
        struct.pack_into('>IIHBB', body, 16 + 12 * i, dol_data.jmap_hash(name), 0, 4 * i, 0, kind)
    return bytes(body + b'\0' * (-len(body) % 32))


def _main_dol():
    """The error archive and the two BCSV tables the cooker extracts from it."""
    names = [('name', STRING_PTR), ('progress', STRING_PTR)]
    galaxy = [('name', STRING_PTR), ('MapPaneName', STRING_PTR), ('PowerStarNum', STRING_PTR),
              ('unused0', 0), ('unused1', 0), ('unused2', 0), ('unused3', 0)]
    out = bytearray(32)
    for part in (_rarc('layoutdata', 'errormessagewindow.arc', b'\0' * 16),
                 _bcsv(names), _bcsv(galaxy)):
        out += b'\0' * (-len(out) % 32)
        out += part
    return bytes(out)


def _disc(root):
    data = root / 'DATA'
    (data / 'sys').mkdir(parents=True)
    (data / 'files' / 'StageData').mkdir(parents=True)
    (data / 'files' / 'ObjectData').mkdir()
    (data / 'sys' / 'fst.bin').write_bytes(b'fst-fixture')
    (data / 'sys' / 'main.dol').write_bytes(_main_dol())
    (data / 'files' / 'StageData' / 'Model.arc').write_bytes(
        _rarc('data', 'note.txt', b'hello'))
    (data / 'files' / 'StageData' / 'Table.bcsv').write_bytes(
        _bcsv([('name', STRING_PTR), ('value', 0)]))
    (data / 'files' / 'ObjectData' / 'Readme.txt').write_bytes(b'plain text')
    return data


class CookPipelineTests(unittest.TestCase):
    def test_desktop_convert_promotes_a_complete_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = _disc(root / 'disc')
            out = root / 'cooked'
            progress = []
            with contextlib.redirect_stdout(io.StringIO()):
                result = cook.convert(str(data), str(out), jobs=1, progress=lambda *a: progress.append(a))
            self.assertEqual(result, 0)
            self.assertEqual(progress[:1], [(0, 3)])
            self.assertEqual(progress[-1], (3, 3))
            self.assertEqual((out / 'sys' / 'fst.bin').read_bytes(), b'fst-fixture')
            self.assertEqual((out / 'sys' / 'ErrorMessageArchive.arc').read_bytes()[:4], b'CRAR')
            for name in ('StoryEvent.bcsv', 'GalaxyID.bcsv'):
                self.assertTrue((out / 'sys' / name).is_file(), name)
            self.assertEqual((out / 'files' / 'ObjectData' / 'Readme.txt').read_bytes(), b'plain text')
            self.assertEqual((out / 'files' / 'StageData' / 'Model.arc').read_bytes()[:4], b'CRAR')
            header = (out / 'files' / 'StageData' / 'Table.bcsv').read_bytes()[:16]
            self.assertEqual(struct.unpack('<iiii', header), (1, 2, 40, 8))
            self.assertEqual(list(root.glob('.galaxy-cook-*')), [])

    def test_worker_failure_is_reported_and_live_tree_is_untouched(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = _disc(root / 'disc')
            (data / 'files' / 'StageData' / 'Broken.arc').write_bytes(b'RARC')
            out = root / 'GalaxyData'
            (out / 'sys').mkdir(parents=True)
            (out / 'sys' / 'fst.bin').write_bytes(b'previous')
            (out / 'saves' / 'slot1').mkdir(parents=True)
            (out / 'saves' / 'slot1' / 'GameData.bin').write_bytes(b'save')
            with contextlib.redirect_stdout(io.StringIO()):
                result = cook.convert(str(data), str(out), jobs=1)
            self.assertEqual(result, 1)
            self.assertEqual((out / 'sys' / 'fst.bin').read_bytes(), b'previous')
            self.assertEqual((out / 'saves' / 'slot1' / 'GameData.bin').read_bytes(), b'save')
            self.assertFalse((out / 'sys' / 'StoryEvent.bcsv').exists())
            self.assertEqual(list(root.glob('.galaxy-cook-*')), [])

    def test_missing_system_tables_fail_before_conversion(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = _disc(root / 'disc')
            (data / 'sys' / 'main.dol').write_bytes(b'not a dol')
            out = root / 'cooked'
            with contextlib.redirect_stdout(io.StringIO()):
                result = cook.convert(str(data), str(out), jobs=1)
            self.assertEqual(result, 1)
            self.assertFalse(out.exists())
            self.assertEqual(list(root.glob('.galaxy-cook-*')), [])

    def test_mobile_scripts_convert_and_preserve_saves(self):
        if not MOBILE_SCRIPTS.is_dir():
            self.skipTest('mobile scripts are not present')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = _disc(root / 'disc')
            out = root / 'GalaxyData'
            (out / 'saves' / 'slot1').mkdir(parents=True)
            (out / 'saves' / 'slot1' / 'GameData.bin').write_bytes(b'save')
            runner = root / 'run_mobile_cook.py'
            runner.write_text(
                'import sys\n'
                'sys.dont_write_bytecode = True\n'
                'sys.path.insert(0, %r)\n'
                'import cook\n'
                'raise SystemExit(cook.convert(data_dir=%r, out_dir=%r, jobs=1))\n'
                % (str(MOBILE_SCRIPTS), str(data), str(out)),
                encoding='utf-8')
            result = subprocess.run([sys.executable, str(runner)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((out / 'saves' / 'slot1' / 'GameData.bin').read_bytes(), b'save')
            self.assertEqual((out / 'sys' / 'fst.bin').read_bytes(), b'fst-fixture')
            self.assertEqual((out / 'sys' / 'ErrorMessageArchive.arc').read_bytes()[:4], b'CRAR')
            self.assertEqual((out / 'files' / 'ObjectData' / 'Readme.txt').read_bytes(), b'plain text')
            self.assertEqual(list(root.glob('.galaxy-cook-*')), [])

    def test_parallel_cook_notes_are_per_thread(self):
        import threading
        import j3d

        del j3d.warnings[:]
        barrier = threading.Barrier(2)
        seen = {}

        def worker(name):
            del j3d.warnings[:]
            barrier.wait()
            j3d.warnings.append(name)
            barrier.wait()
            seen[name] = list(j3d.warnings)

        threads = [threading.Thread(target=worker, args=(name,))
                   for name in ('left', 'right')]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=10)
        self.assertEqual(seen, {'left': ['left'], 'right': ['right']})
        self.assertEqual(['note'] + j3d.warnings, ['note'],
                         'worker notes leaked into the parent thread')

    def test_shared_cook_modules_stay_identical(self):
        if not MOBILE_SCRIPTS.is_dir():
            self.skipTest('mobile scripts are not present')
        for name in ('cook_archive.py', 'cook_output.py', 'cook_warnings.py',
                     'j3d.py', 'lyt.py', 'jpa.py', 'jaudio.py'):
            self.assertEqual((COOK / name).read_bytes(), (MOBILE_SCRIPTS / name).read_bytes(), name)


if __name__ == '__main__':
    unittest.main()
