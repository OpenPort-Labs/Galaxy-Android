"""Installer regression tests; never touch a real headset or game disc."""
import collections
import contextlib
import hashlib
import io
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import install
import build_installer
import package_mobile_apk
import push_data


class InstallerTests(unittest.TestCase):
    def test_folder_picker_accepts_root_and_data_with_spaces(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'My extracted game'
            data = root / 'DATA'
            (data / 'sys').mkdir(parents=True)
            (data / 'files' / 'ObjectData').mkdir(parents=True)
            (data / 'files' / 'StageData').mkdir()
            (data / 'sys' / 'fst.bin').write_bytes(b'fst')
            with self.assertRaisesRegex(install.InstallError, 'whole extracted disc'):
                install.game_partition(str(root))
            (data / 'sys' / 'main.dol').write_bytes(b'dol')
            self.assertEqual(install.game_partition(str(root)), data.resolve())
            self.assertEqual(install.game_partition(str(data)), data.resolve())

    def test_device_errors_and_explicit_selection(self):
        for devices, message in (([], 'No headset'), ([('one', 'unauthorized', '')], 'USB debugging'),
                                 ([('one', 'offline', '')], 'offline'),
                                 ([('one', 'device', ''), ('two', 'device', '')], 'Several')):
            with self.assertRaisesRegex(install.InstallError, message):
                install.choose_device(devices)
        self.assertEqual(install.choose_device([('one', 'device', '')]), 'one')
        self.assertEqual(install.choose_device([('one', 'device', ''), ('two', 'device', '')], 'two'), 'two')
        with self.assertRaisesRegex(install.InstallError, 'no longer'):
            install.choose_device([('one', 'device', '')], 'two')

    def test_device_list_ignores_daemon_messages(self):
        output = '* daemon started successfully *\nList of devices attached\nabc device product:hollywood model:Quest_3\ndef unauthorized\n'
        with mock.patch.object(install, 'run_adb', return_value=output):
            self.assertEqual(install.list_devices('adb'),
                             [('abc', 'device', 'product:hollywood model:Quest_3'), ('def', 'unauthorized', '')])

    def test_device_storage_is_parsed_in_kibibytes(self):
        with mock.patch.object(install, 'run_adb', return_value='Filesystem 1K-blocks Used Available Use% Mounted on\n/dev/fuse 10000000 2000000 8000000 20% /storage/emulated\n'):
            self.assertEqual(install.free_device_bytes('adb', 'serial'), 8000000 * 1024)
        with mock.patch.object(install, 'run_adb', return_value='Permission denied'):
            with self.assertRaisesRegex(install.InstallError, 'storage'):
                install.free_device_bytes('adb', 'serial')

    def test_frozen_linux_adb_does_not_load_bundled_python_libraries(self):
        with mock.patch.object(sys, 'frozen', True, create=True), mock.patch.object(sys, 'platform', 'linux'):
            with mock.patch.dict(install.os.environ, {'LD_LIBRARY_PATH': '/bundle', 'LD_LIBRARY_PATH_ORIG': '/system'}):
                self.assertEqual(push_data.adb_environment()['LD_LIBRARY_PATH'], '/system')
            with mock.patch.dict(install.os.environ, {'LD_LIBRARY_PATH': '/bundle'}, clear=True):
                self.assertNotIn('LD_LIBRARY_PATH', push_data.adb_environment())

    def test_conversion_failure_never_installs_or_copies(self):
        self.exercise_install(fail=True)

    def test_success_installs_then_copies_and_preserves_source(self):
        self.exercise_install(fail=False)

    def test_automatic_workspace_skips_disc_and_cleans_up(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp).resolve() / 'disc'
            source.mkdir()
            disk = mock.Mock(free=100 * install.GIB)
            with mock.patch.object(install, 'cache_dir', return_value=source / 'files'), \
                    mock.patch.object(install.shutil, 'disk_usage', return_value=disk), \
                    contextlib.redirect_stdout(io.StringIO()):
                with install.temporary_workspace(source, True) as workspace:
                    self.assertEqual(workspace.parent, source.parent)
                    (workspace / 'test.txt').write_text('temporary')
                self.assertFalse(workspace.exists())
            self.assertFalse((source / 'files').exists())

    def test_automatic_workspace_falls_back_when_cache_disk_is_full(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp).resolve() / 'disc.rvz'
            source.write_bytes(b'image')
            cache = source.parent / 'cache'
            def disk(path):
                return mock.Mock(free=0 if Path(path) == cache else 100 * install.GIB)
            with mock.patch.object(install, 'cache_dir', return_value=cache), \
                    mock.patch.object(install.shutil, 'disk_usage', side_effect=disk), \
                    contextlib.redirect_stdout(io.StringIO()):
                with install.temporary_workspace(source, True) as workspace:
                    self.assertEqual(workspace.parent, source.parent)
                self.assertFalse(workspace.exists())
            self.assertEqual(source.read_bytes(), b'image')

    def test_disc_image_suffixes_and_missing_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            for extension in ('.iso', '.RVZ', '.wbfs', '.gcz', '.wia', '.ciso'):
                image = Path(tmp) / ('My game' + extension)
                image.write_bytes(b'image')
                self.assertEqual(install.game_copy(image), (image.resolve(), True))
            with self.assertRaisesRegex(install.InstallError, 'ISO, RVZ or WBFS'):
                install.game_copy(Path(tmp) / 'missing.rvz')

    def test_extraction_arguments_and_validates_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / 'My game.rvz'
            destination = Path(tmp) / 'new extracted folder'
            def extract(tool, *args, **kwargs):
                self.assertEqual(args, ('extract', '-i', str(image), '-o', str(destination), '--gameonly', '--quiet'))
                self.assertEqual(kwargs['cwd'], str(destination.parent))
                # A success return with missing main.dol must be rejected.
                (destination / 'DATA' / 'sys').mkdir(parents=True)
                (destination / 'DATA' / 'sys' / 'fst.bin').write_bytes(b'fst')
                return ''
            with mock.patch.object(install, 'find_dolphin_tool', return_value='DolphinTool'), \
                    mock.patch.object(install, 'dolphin_command', side_effect=extract), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(install.InstallError, 'whole extracted disc'):
                    install.extract_disc(image, destination)

    def test_image_is_extracted_before_conversion(self):
        self.exercise_install(fail=False, is_image=True)

    def test_extraction_failure_never_converts_installs_or_copies(self):
        self.exercise_install(fail=False, is_image=True, extraction_fails=True)

    def exercise_install(self, fail, is_image=False, extraction_fails=False):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            original = root / 'original'
            original.mkdir()
            sentinel = original / 'keep.bin'
            sentinel.write_bytes(b'original game')
            game = root / 'My game.rvz' if is_image else original
            if is_image:
                game.write_bytes(b'disc image')
            apk = root / 'GalaxyQuest.apk'
            apk.write_bytes(b'apk')
            calls, progress = [], []

            def extract(image, destination):
                calls.append('extract')
                self.assertEqual(image, game)
                self.assertEqual(Path(destination).name, 'extracted')
                if extraction_fails:
                    raise install.InstallError('Extraction failed')
                return original

            def convert(source, output, **kwargs):
                calls.append('convert')
                self.assertEqual(source, str(original))
                self.assertTrue(kwargs['with_movies'])
                (Path(output) / 'sys').mkdir(parents=True)
                (Path(output) / 'sys' / 'fst.bin').write_bytes(b'converted')
                kwargs['progress'](16, 16)
                return 1 if fail else 0

            def command(adb, *args, **kwargs):
                self.assertEqual(kwargs['serial'], 'quest')
                if args == ('shell', 'getprop ro.product.manufacturer'):
                    return 'Oculus'
                if args == ('shell', 'getprop ro.product.model'):
                    return 'Quest 3'
                self.assertEqual(args[:3], ('install', '--no-incremental', '-r'))
                calls.append('install')
                return 'Success'

            def copy(source, **kwargs):
                calls.append('copy')
                self.assertEqual(kwargs['serial'], 'quest')
                self.assertEqual(kwargs['adb'], 'bundled-adb')
                self.assertEqual((Path(source) / 'sys' / 'fst.bin').read_bytes(), b'converted')

            disk = collections.namedtuple('Disk', 'total used free')(100 * install.GIB, 0, 100 * install.GIB)
            with contextlib.ExitStack() as stack:
                for name, value in (('game_partition', original), ('find_adb', 'bundled-adb'),
                                    ('find_apk', apk), ('list_devices', [('quest', 'device', '')]),
                                    ('free_device_bytes', 100 * install.GIB)):
                    stack.enter_context(mock.patch.object(install, name, return_value=value))
                stack.enter_context(mock.patch.object(install, 'run_adb', side_effect=command))
                stack.enter_context(mock.patch.object(install, 'find_dolphin_tool', return_value='DolphinTool'))
                stack.enter_context(mock.patch.object(install, 'extract_disc', side_effect=extract))
                stack.enter_context(mock.patch.object(install, 'cache_dir', return_value=root / 'work'))
                stack.enter_context(mock.patch.object(install.cook, 'convert', side_effect=convert))
                stack.enter_context(mock.patch.object(push_data, 'copy_data', side_effect=copy))
                stack.enter_context(mock.patch.object(install.shutil, 'disk_usage', return_value=disk))
                stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
                if extraction_fails:
                    with self.assertRaisesRegex(install.InstallError, 'Extraction failed'):
                        install.install_game(str(game), 'quest', True,
                                             lambda *event: progress.append(event))
                    self.assertEqual(calls, ['extract'])
                elif fail:
                    with self.assertRaisesRegex(install.InstallError, 'could not be converted'):
                        install.install_game(str(game), 'quest', True,
                                             lambda *event: progress.append(event))
                    self.assertEqual(calls, ['convert'])
                else:
                    install.install_game(str(game), 'quest', True,
                                         lambda *event: progress.append(event))
                    self.assertEqual(calls, (['extract'] if is_image else []) + ['convert', 'install', 'copy'])
                    self.assertEqual(progress[-1][1], 100)
            self.assertEqual(sentinel.read_bytes(), b'original game')
            self.assertEqual(list((root / 'work').iterdir()), [])

    def test_copy_pins_all_commands_and_tar_holds_game_root(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'cooked'
            (source / 'sys').mkdir(parents=True)
            (source / 'sys' / 'fst.bin').write_bytes(b'fst')
            calls = []

            def run(command, **kwargs):
                self.assertEqual(command[:3], ['bundled-adb', '-s', 'quest'])
                calls.append(command)
                if command[3] == 'push':
                    import tarfile
                    with tarfile.open(command[4]) as archive:
                        self.assertIn('sys/fst.bin', archive.getnames())
                        self.assertNotIn('cooked/sys/fst.bin', archive.getnames())
                return mock.Mock(stdout='OK')

            with mock.patch.object(push_data.subprocess, 'run', side_effect=run), contextlib.redirect_stdout(io.StringIO()):
                push_data.copy_data(str(source), adb='bundled-adb', serial='quest', temp_dir=tmp)
            self.assertEqual([command[3] for command in calls], ['push', 'shell'])
            self.assertIn('chmod -R a+rX', calls[-1][-1])

    def test_zip_extraction_rejects_path_traversal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = root / 'bad.zip'
            with zipfile.ZipFile(archive, 'w') as source:
                source.writestr('platform-tools/../../escape.txt', 'bad')
            with self.assertRaisesRegex(ValueError, 'Unsafe'):
                build_installer.extract_tools(archive, root / 'assets')
            self.assertFalse((root / 'escape.txt').exists())

    def test_packager_refuses_a_debuggable_release(self):
        # The attribute is read as XML, so the manifest's own comment about
        # android:debuggable="true" does not count as setting it.
        self.assertTrue(package_mobile_apk.manifest_is_debuggable())
        with tempfile.TemporaryDirectory() as tmp:
            clean = Path(tmp) / 'AndroidManifest.xml'
            clean.write_text(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<manifest xmlns:android="http://schemas.android.com/apk/res/android">\n'
                '  <!-- android:debuggable="true" is only mentioned here -->\n'
                '  <application android:debuggable="false" />\n'
                '</manifest>\n', encoding='utf-8')
            self.assertFalse(package_mobile_apk.manifest_is_debuggable(clean))
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop('GALAXY_ALLOW_DEBUGGABLE', None)
            with self.assertRaisesRegex(SystemExit, 'REFUSING TO PACKAGE'):
                package_mobile_apk.require_deliberate_debuggable()
        with mock.patch.dict(os.environ, {'GALAXY_ALLOW_DEBUGGABLE': '1'}):
            package_mobile_apk.require_deliberate_debuggable()

    def test_platform_tools_download_stays_pinned(self):
        for name, (archive, size, sha256) in build_installer.PLATFORM_TOOLS.items():
            self.assertTrue(archive.startswith('platform-tools_r'))
            self.assertTrue(archive.endswith('.zip'))
            self.assertEqual(len(sha256), 64)
            self.assertGreater(size, 1_000_000)
            url = build_installer.DOWNLOADS[name]
            self.assertTrue(url.endswith('/' + archive), url)
            self.assertNotIn('latest', url)

    def test_platform_tools_archive_is_verified_before_extraction(self):
        payload = b'not the pinned platform-tools archive'
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / 'platform-tools.zip'
            archive.write_bytes(payload)
            with self.assertRaisesRegex(RuntimeError, 'is %d bytes, expected' % len(payload)):
                build_installer.verify_download(archive, len(payload) + 1, hashlib.sha256(payload).hexdigest())
            with self.assertRaisesRegex(RuntimeError, 'SHA-256'):
                build_installer.verify_download(archive, len(payload), '0' * 64)
            build_installer.verify_download(archive, len(payload), hashlib.sha256(payload).hexdigest())


if __name__ == '__main__':
    unittest.main()
