#!/usr/bin/env python3
"""Desktop installer. Includes Python, DolphinTool, the APK and adb.

Run from source: python tools/install.py (APK in out/GalaxyQuest.apk).
Build: python tools/build_installer.py --apk out/GalaxyQuest.apk --dolphin-tool PATH
"""
import multiprocessing

# Must run before importing Tk or parsing arguments: frozen conversion workers
# re-enter this executable with multiprocessing's private command-line flags.
if __name__ == '__main__':
    multiprocessing.freeze_support()

import argparse
import concurrent.futures
import contextlib
import datetime
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import traceback
import webbrowser

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / 'cook'))
sys.path.insert(0, str(HERE))
import cook
import push_data

DEVELOPER_HELP = 'https://developers.meta.com/horizon/documentation/android-apps/enable-developer-mode/'
GIB = 1024 ** 3
DISC_EXTENSIONS = {'.iso', '.rvz', '.wbfs', '.gcz', '.wia', '.ciso'}


class InstallError(Exception):
    pass


def assets_dir():
    return Path(getattr(sys, '_MEIPASS', HERE.parent / 'out')) / 'assets'


def cache_dir():
    if sys.platform == 'win32':
        base = Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData' / 'Local'))
    elif sys.platform == 'darwin':
        base = Path.home() / 'Library' / 'Caches'
    else:
        base = Path(os.environ.get('XDG_CACHE_HOME', Path.home() / '.cache'))
    return base / 'GalaxyQuest'


def find_adb():
    name = 'adb.exe' if sys.platform == 'win32' else 'adb'
    beside = Path(sys.executable).parent if getattr(sys, 'frozen', False) else HERE.parent
    for candidate in (assets_dir() / 'platform-tools' / name,
                      beside / 'platform-tools' / name):
        if candidate.is_file():
            return str(candidate)
    try:
        return push_data.find_adb()
    except SystemExit:
        raise InstallError('Connection tools are missing. Use a packaged GalaxyQuest installer, '
                           'or put the Android platform-tools folder next to this launcher.') from None


def find_apk():
    bundled = assets_dir() / 'GalaxyQuest.apk'
    beside = Path(sys.executable).parent if getattr(sys, 'frozen', False) else HERE.parent / 'out'
    for candidate in (bundled, beside / 'GalaxyQuest.apk'):
        if candidate.is_file():
            return candidate
    raise InstallError('GalaxyQuest.apk is missing. Use a packaged installer, '
                       'or put GalaxyQuest.apk in the out folder when running from source.')


def find_dolphin_tool():
    names = ('DolphinTool.exe',) if sys.platform == 'win32' else ('dolphin-tool', 'DolphinTool')
    beside = Path(sys.executable).parent if getattr(sys, 'frozen', False) else HERE.parent
    for directory in (assets_dir() / 'dolphin', beside / 'dolphin'):
        for name in names:
            candidate = directory / name
            if candidate.is_file():
                return str(candidate)
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    raise InstallError('The disc extraction tool is missing. Use the complete GalaxyQuest installer '
                       'with DolphinTool included, or select an already extracted game folder.')


def game_copy(path):
    source = Path(path).expanduser().resolve()
    if source.is_dir():
        return game_partition(source), False
    if source.is_file() and source.suffix.lower() in DISC_EXTENSIONS:
        return source, True
    raise InstallError('Choose your Super Mario Galaxy ISO, RVZ or WBFS file, '
                       'or an already extracted game folder.')


def dolphin_command(tool, *args, cwd=None):
    # Collected DolphinTool libraries need the frozen application's search path.
    # An external/source-mode tool uses the original system environment.
    bundled = getattr(sys, 'frozen', False) and Path(sys._MEIPASS) in Path(tool).resolve().parents
    env = os.environ.copy() if bundled else push_data.adb_environment()
    try:
        result = subprocess.run([tool] + list(args), cwd=cwd, env=env,
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors='replace',
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    except OSError as error:
        raise InstallError('Could not start the disc extractor: ' + str(error)) from error
    if result.returncode:
        raise InstallError('Dolphin could not extract this game copy. Check that the file is complete '
                           'and readable.\n' + result.stdout.strip()[-4000:])
    return result.stdout


def extract_disc(image, destination):
    destination = Path(destination)
    if destination.exists():
        raise InstallError('Disc extraction requires a new temporary folder.')
    tool = find_dolphin_tool()
    print('Extracting %s with %s' % (image, tool), flush=True)
    output = dolphin_command(tool, 'extract', '-i', str(image), '-o', str(destination),
                             '--gameonly', '--quiet', cwd=str(destination.parent))
    if output.strip():
        print(output.strip(), flush=True)
    # Dolphin may return success after an incomplete extraction; validate the
    # game partition before handing it to the converter.
    return game_partition(destination)


@contextlib.contextmanager
def temporary_workspace(source, movies):
    """Choose a writable disk with enough space, then clean only our own folder."""
    source = Path(source).resolve()
    required = (16 if movies else 8) * GIB if source.is_file() else (10 if movies else 4) * GIB
    candidates = (cache_dir(), source.parent, Path(tempfile.gettempdir()))
    attempted = set()
    scratch = None
    for candidate in candidates:
        base = candidate.expanduser().resolve()
        if base in attempted or (source.is_dir() and (base == source or source in base.parents)):
            continue
        attempted.add(base)
        try:
            base.mkdir(parents=True, exist_ok=True)
            if shutil.disk_usage(base).free < required:
                continue
            scratch = tempfile.TemporaryDirectory(prefix='.galaxyquest-install-', dir=base)
            break
        except OSError as error:
            print('Temporary storage unavailable at %s: %s' % (base, error), flush=True)
    if scratch is None:
        raise InstallError('The computer needs at least %d GB free for this installation. '
                           'Free space on the system disk or the disk holding your game and try again.' % (required // GIB))
    with scratch as folder:
        print('Temporary working folder: ' + folder, flush=True)
        yield Path(folder)


def run_adb(adb, *args, serial=None):
    command = [adb] + (['-s', serial] if serial else []) + list(args)
    # Redirect all streams: windowed frozen apps have no standard handles.
    result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors='replace',
                            env=push_data.adb_environment(),
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if result.returncode:
        raise InstallError(result.stdout.strip() or 'The headset connection command failed.')
    return result.stdout.strip()


def list_devices(adb):
    devices = []
    for line in run_adb(adb, 'devices', '-l').splitlines():
        words = line.split()
        if len(words) >= 2 and not line.startswith(('List ', '*')):
            devices.append((words[0], words[1], ' '.join(words[2:])))
    return devices


def choose_device(devices, selected=''):
    ready = [serial for serial, state, _ in devices if state == 'device']
    if selected and selected in ready:
        return selected
    if selected:
        raise InstallError('The selected headset is no longer connected. Reconnect it and click Refresh.')
    if len(ready) == 1:
        return ready[0]
    if len(ready) > 1:
        raise InstallError('Several devices are connected. Select your headset in the list.')
    if any(state == 'unauthorized' for _, state, _ in devices):
        raise InstallError('Put on the headset and accept Allow USB debugging, '
                           'then click Refresh. You can select Always allow from this computer.')
    if any(state == 'offline' for _, state, _ in devices):
        raise InstallError('The headset is offline. Reconnect the USB cable, wake the headset and click Refresh.')
    raise InstallError('No headset found. Enable developer mode, connect a USB data cable, '
                       'and accept USB debugging in the headset. On Linux, also check your USB device permissions.')


def game_partition(folder):
    root = Path(folder).expanduser().resolve()
    for candidate in (root, root / 'DATA'):
        if ((candidate / 'sys' / 'fst.bin').is_file()
                and (candidate / 'sys' / 'main.dol').is_file()
                and (candidate / 'files' / 'ObjectData').is_dir()
                and (candidate / 'files' / 'StageData').is_dir()):
            return candidate
    raise InstallError('Select the whole extracted disc folder, or its DATA folder. '
                       'It must contain sys/main.dol, sys/fst.bin and the game files. '
                       'In Dolphin, use Extract Entire Disc.')


def free_device_bytes(adb, serial):
    output = run_adb(adb, 'shell', 'df -k /sdcard', serial=serial)
    for line in reversed(output.splitlines()):
        fields = line.split()
        if len(fields) >= 4 and fields[1].isdigit() and fields[3].isdigit():
            return int(fields[3]) * 1024
    raise InstallError('Could not check the headset storage. Reconnect it and try again.\n' + output)


def install_game(game, selected, movies, status):
    """All heavy work runs off the UI thread; errors stop before copying bad data."""
    source, is_image = game_copy(game)
    if is_image:
        find_dolphin_tool()  # Fail early, before doing any heavy work.
    adb = find_adb()
    apk = find_apk()
    status('Checking headset and storage...', 2)
    serial = choose_device(list_devices(adb), selected)
    # Prevent accidentally installing to an Android phone listed by adb.
    manufacturer = run_adb(adb, 'shell', 'getprop ro.product.manufacturer', serial=serial).lower()
    model = run_adb(adb, 'shell', 'getprop ro.product.model', serial=serial)
    if manufacturer not in ('oculus', 'meta'):
        raise InstallError('The selected device is %s (%s), not a Meta headset. '
                           'Disconnect other Android devices and select your headset.' % (model, manufacturer))
    print('Headset: %s (%s)' % (model, serial), flush=True)
    if free_device_bytes(adb, serial) < (7 if movies else 3) * GIB:
        raise InstallError('Free at least %d GB on the headset and try again.' % (7 if movies else 3))
    with temporary_workspace(source, movies) as scratch:
        cooked = scratch / 'cooked'
        if is_image:
            status('Extracting your disc with Dolphin...', 5)
            source = extract_disc(source, scratch / 'extracted')
        status('Converting your game...', 20)

        def progress(done, total):
            status('Converting game files: %d / %d' % (done, total),
                   20 + 45 * done / max(total, 1))

        result = cook.convert(str(source), str(cooked), with_movies=movies,
                              jobs=min(os.cpu_count() or 1, 8), progress=progress)
        if result:
            raise InstallError('Some game files could not be converted. '
                               'See the log for the filenames; extract the whole disc again and retry.')
        size = sum(path.stat().st_size for path in cooked.rglob('*') if path.is_file())
        if free_device_bytes(adb, serial) < size * 2 + apk.stat().st_size + 512 * 1024 ** 2:
            raise InstallError('The headset needs more free storage to copy and unpack this game '
                               '(about %.1f GB). Free space and retry.' % ((size * 2 + 512 * 1024 ** 2) / GIB))
        if shutil.disk_usage(scratch).free < size + 512 * 1024 ** 2:
            raise InstallError('The computer needs another %.1f GB to pack the converted game.' % (size / GIB))
        status('Installing GalaxyQuest...', 67)
        print(run_adb(adb, 'install', '--no-incremental', '-r', str(apk), serial=serial), flush=True)

        def copying(message):
            if message.startswith('packing'):
                status('Preparing game files for copying...', 72)
            elif message.startswith('copying'):
                status('Copying the game to your headset...', 80)
            else:
                status('Finishing installation on your headset...', 95)

        push_data.copy_data(str(cooked), adb=adb, serial=serial,
                            progress=copying, temp_dir=str(scratch))
    status('Installed! Open GalaxyQuest in Library > Unknown Sources on your headset.', 100)


