#!/usr/bin/env python3
"""Copies the converted game files to the headset, where the app reads them.

  python tools/push_data.py [cooked dir]        (default: cooked)

Install the APK first.  The headset must be connected over USB with
developer mode on, and adb (Android platform-tools) on PATH or in ADB.
The files go to /sdcard/Android/data/com.galaxy.quest/files/game (about
3.3 GB).  adb cannot create folders under Android/data one by one, so they
go over as one tar file that the headset unpacks.
"""
import os
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile

PACKAGE = 'com.galaxy.mobile'
DEST = '/sdcard/Android/data/%s/files/game' % PACKAGE
TMP = '/data/local/tmp/petari_game_data.tar'


def find_adb():
    if os.environ.get('ADB'):
        return os.environ['ADB']
    adb = shutil.which('adb')
    if adb:
        return adb
    for sdk in (os.environ.get('ANDROID_HOME'), os.environ.get('ANDROID_SDK_ROOT'),
                os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Android', 'Sdk'),
                os.path.expanduser('~/Android/Sdk'), os.path.expanduser('~/Library/Android/sdk')):
        if sdk:
            for name in ('adb.exe', 'adb'):
                path = os.path.join(sdk, 'platform-tools', name)
                if os.path.isfile(path):
                    return path
    sys.exit('adb not found: install the Android platform-tools and put adb on PATH (or set ADB)')


def adb_environment():
    """Do not give Google's adb the frozen Python runtime's library path."""
    env = os.environ.copy()
    if getattr(sys, 'frozen', False) and sys.platform.startswith('linux'):
        if 'LD_LIBRARY_PATH_ORIG' in env:
            env['LD_LIBRARY_PATH'] = env['LD_LIBRARY_PATH_ORIG']
        else:
            env.pop('LD_LIBRARY_PATH', None)
    return env


def copy_data(src, adb=None, serial=None, progress=None, temp_dir=None):
    """Copy game data; serial pins every command to the chosen headset.

    progress(message) receives the packing/copying/unpacking stages.
    temp_dir lets the installer keep the large tar on the checked disk.
    """
    if not os.path.isfile(os.path.join(src, 'sys', 'fst.bin')):
        sys.exit('%s does not look like converted game files (no sys/fst.bin): run tools/cook/cook.py first' % src)
    adb = adb or find_adb()
    command = [adb] + (['-s', serial] if serial else [])

    def run(*args):
        result = subprocess.run(command + list(args), check=True, text=True,
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT,
                                env=adb_environment(),
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        if result.stdout.strip():
            print(result.stdout.strip(), flush=True)

    if not serial:
        devices = subprocess.run([adb, 'devices'], check=True, capture_output=True, text=True,
                                 stdin=subprocess.DEVNULL,
                                 env=adb_environment(),
                                 creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).stdout.split('\n')[1:]
        ready = [line.split()[0] for line in devices if line.strip().endswith('device')]
        if not ready:
            sys.exit('no headset found: connect it over USB, allow USB debugging in the headset, and try again')
        if len(ready) != 1:
            sys.exit('more than one device found: disconnect the others or use --serial SERIAL')
        command = [adb, '-s', ready[0]]

    def report(message):
        print(message, flush=True)
        if progress:
            progress(message)

    with tempfile.TemporaryDirectory(dir=temp_dir) as tmp:
        tar_path = os.path.join(tmp, 'game_data.tar')
        report('packing %s' % src)
        with tarfile.open(tar_path, 'w') as tar:
            for name in sorted(os.listdir(src)):
                tar.add(os.path.join(src, name), arcname=name)
        report('copying to the headset (%.1f GB)' % (os.path.getsize(tar_path) / 1e9))
        run('push', tar_path, TMP)
    report('unpacking in %s' % DEST)
    # The files belong to adb's shell user and the folder is group-only: the
    # app, under its own user, reads them through "other".
    run('shell', 'mkdir -p %s && cd %s && tar xf %s && rm %s && chmod -R a+rX %s' %
        tuple(shlex.quote(path) for path in (DEST, DEST, TMP, TMP, DEST)))
    print('done: the game files are in %s' % DEST)


def main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('src', nargs='?', default='cooked')
    ap.add_argument('--serial', help='headset serial, as listed by adb devices')
    args = ap.parse_args()
    copy_data(args.src, serial=args.serial)


if __name__ == '__main__':
    main()
