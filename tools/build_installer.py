#!/usr/bin/env python3
"""Build a desktop installer; users need no Python, Dolphin or adb.

    python -m pip install -r tools/installer-requirements.txt
    python tools/build_installer.py --apk out/GalaxyQuest.apk --dolphin-tool PATH

Downloads Google's platform-tools unless --platform-tools points to an
existing folder. The APK is explicitly supplied so it matches the release.
"""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

REPO = Path(__file__).resolve().parent.parent
# Android platform-tools, pinned to one released archive per platform with the
# size and SHA-256 of the exact bytes Google serves (repository2-1.xml lists the
# same version, size and SHA-1).  The "latest" URLs are deliberately not used:
# they change under us, so a swapped or truncated download would be packaged into
# an installer unnoticed.  Bump the version, size and hash together.
PLATFORM_TOOLS = {
    'win32': ('platform-tools_r37.0.1-win.zip', 8044989,
              '45f4d63113e895ebde0c90f194099a4676b6ac653bd28d54314a9e022bbc1a99'),
    'darwin': ('platform-tools_r37.0.1-darwin.zip', 16110554,
               'ee39ad5967e95c2a07f04dbcbde96b1a0c916ba376096db5d2f498b7727a5d1d'),
    'linux': ('platform-tools_r37.0.1-linux.zip', 9054187,
              'd230f13842f60f782a8645f9c813f8f845bf36089ea7289f28c48f17979313f1'),
}
DOWNLOADS = {platform: 'https://dl.google.com/android/repository/' + tools[0]
             for platform, tools in PLATFORM_TOOLS.items()}


def verify_download(archive, expected_size, expected_sha256):
    """Refuse to unpack anything but the pinned platform-tools archive."""
    actual_size = archive.stat().st_size
    if actual_size != expected_size:
        raise RuntimeError('%s is %d bytes, expected %d' % (archive.name, actual_size, expected_size))
    digest = hashlib.sha256()
    with archive.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    if digest.hexdigest() != expected_sha256:
        raise RuntimeError('%s has SHA-256 %s, expected %s'
                           % (archive.name, digest.hexdigest(), expected_sha256))


def extract_tools(archive, destination):
    """Extract only regular platform-tools files, within the build directory."""
    destination = Path(destination).resolve()
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            name = entry.filename.replace('\\', '/')
            if not name.startswith('platform-tools/') or entry.is_dir():
                continue
            target = (destination / name).resolve()
            if destination not in target.parents:
                raise ValueError('Unsafe platform-tools archive path: ' + name)
            if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Unexpected symlink in platform-tools archive: ' + name)
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.open(entry) as src, target.open('wb') as dst:
                shutil.copyfileobj(src, dst)
            mode = (entry.external_attr >> 16) & 0o777
            if mode:
                target.chmod(mode)
    adb = destination / 'platform-tools' / ('adb.exe' if sys.platform == 'win32' else 'adb')
    if not adb.is_file():
        raise ValueError('The downloaded platform-tools archive contains no adb executable.')
    if sys.platform != 'win32':
        adb.chmod(0o755)
    return adb.parent


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--apk', type=Path, required=True, help='APK for this release')
    ap.add_argument('--dolphin-tool', type=Path, required=True, help='DolphinTool executable for the target OS')
    ap.add_argument('--dolphin-licenses', type=Path, help='Dolphin distribution or source directory holding COPYING/licenses')
    ap.add_argument('--dolphin-source-url', required=True, help='source tree for the exact DolphinTool build')
    ap.add_argument('--output-suffix', default='', help='optional filename suffix when an older installer is open')
    ap.add_argument('--platform-tools', type=Path, help='existing Google platform-tools directory')
    args = ap.parse_args()
    if any(character not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for character in args.output_suffix):
        ap.error('--output-suffix can only contain letters, numbers, hyphens or underscores')
    apk = args.apk.resolve()
    if not apk.is_file():
        ap.error('APK does not exist: ' + str(apk))
    dolphin = args.dolphin_tool.resolve()
    if not dolphin.is_file():
        ap.error('DolphinTool does not exist: ' + str(dolphin))
    license_dir = args.dolphin_licenses.resolve() if args.dolphin_licenses else dolphin.parent
    if not (license_dir / 'COPYING').is_file():
        ap.error('Supply the Dolphin distribution/source folder with --dolphin-licenses (COPYING is required)')
    if sys.platform not in DOWNLOADS:
        ap.error('Build on Windows, macOS or Linux.')
    try:
        import PyInstaller
    except ImportError:
        ap.error('Install the build requirements: python -m pip install -r tools/installer-requirements.txt')

    # No global packages or game files are included. Keep every generated file
    # under out; never write into the source converter or the selected disc.
    build = REPO / 'out' / ('installer-build-' + sys.platform)
    assets = build / 'assets'
    assets.mkdir(parents=True, exist_ok=True)
    dolphin_assets = assets / 'dolphin'
    dolphin_assets.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(license_dir / 'COPYING', dolphin_assets / 'COPYING')
    for licenses in ('Licenses', 'LICENSES'):
        if (license_dir / licenses).is_dir():
            shutil.copytree(license_dir / licenses, dolphin_assets / licenses, dirs_exist_ok=True)
    (dolphin_assets / 'SOURCE.txt').write_text(
        'DolphinTool is a separate program from the Dolphin Emulator Project.\n'
        'Source for this build: ' + args.dolphin_source_url + '\n'
        'See COPYING and the accompanying licenses.\n', encoding='utf-8')
    adb_name = 'adb.exe' if sys.platform == 'win32' else 'adb'
    if args.platform_tools:
        tools = args.platform_tools.resolve()
        if not (tools / adb_name).is_file():
            ap.error('--platform-tools must hold ' + adb_name)
        shutil.copytree(tools, assets / 'platform-tools', dirs_exist_ok=True)
    elif not (assets / 'platform-tools' / adb_name).is_file():
        archive = build / 'platform-tools.zip'
        print('Downloading Android platform-tools from Google...', flush=True)
        request = urllib.request.Request(DOWNLOADS[sys.platform], headers={'User-Agent': 'GalaxyQuest-Installer-Builder'})
        with urllib.request.urlopen(request, timeout=120) as src, archive.open('wb') as dst:
            shutil.copyfileobj(src, dst)
        verify_download(archive, *PLATFORM_TOOLS[sys.platform][1:])
        extract_tools(archive, assets)
    shutil.copyfile(apk, assets / 'GalaxyQuest.apk')
    shutil.copyfile(REPO / 'LICENSE', assets / 'GalaxyQuest-LICENSE.txt')
    shutil.copyfile(REPO / 'THIRD_PARTY_NOTICES.md', assets / 'THIRD_PARTY_NOTICES.md')
    for license_name in ('LICENSE_PYTHON.txt', 'LICENSE.txt', 'LICENSE'):
        python_license = Path(sys.base_prefix) / license_name
        if python_license.is_file():
            shutil.copyfile(python_license, assets / 'Python-LICENSE.txt')
            break

    dist = build / 'dist'
    name = 'GalaxyQuest-Installer'
    command = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--noupx',
               '--name', name, '--distpath', str(dist), '--workpath', str(build / 'work'),
               '--specpath', str(build), '--paths', str(REPO / 'tools'),
               '--paths', str(REPO / 'tools' / 'cook'), '--add-data', str(assets) + ':assets',
               '--add-binary', str(dolphin) + ':assets/dolphin']
    if sys.platform == 'darwin':
        # A normal .app avoids repeatedly unpacking a large onefile app on macOS.
        command += ['--onedir', '--windowed', '--osx-bundle-identifier', 'com.galaxy.quest.installer']
    else:
        command += ['--onefile']
        if sys.platform == 'win32':
            command += ['--windowed']
    command.append(str(REPO / 'tools' / 'install.py'))
    env = os.environ.copy()
    env['PYINSTALLER_CONFIG_DIR'] = str(build / 'cache')
    subprocess.run(command, cwd=REPO, env=env, check=True)

    executable = dist / (name + ('.exe' if sys.platform == 'win32' else ''))
    if sys.platform == 'darwin':
        executable = dist / (name + '.app') / 'Contents' / 'MacOS' / name
    report = build / 'self-test.txt'
    if report.exists():
        report.unlink()
    subprocess.run([str(executable), '--self-test', '--self-test-report', str(report)], check=True, timeout=120)
    if not report.is_file() or not report.read_text(encoding='utf-8').startswith('PASS:'):
        raise RuntimeError('The packaged installer did not pass its worker/asset smoke test.')
    print(report.read_text(encoding='utf-8').strip())

    system = {'win32': 'windows', 'darwin': 'macos', 'linux': 'linux'}[sys.platform]
    arch = {'AMD64': 'x86_64', 'aarch64': 'arm64'}.get(platform.machine(), platform.machine())
    release = REPO / 'out' / 'installers'
    release.mkdir(parents=True, exist_ok=True)
    suffix = '-' + args.output_suffix if args.output_suffix else ''
    basename = release / ('GalaxyQuest-Installer-%s-%s%s' % (system, arch, suffix))
    if sys.platform == 'win32':
        artifact = basename.with_suffix('.exe')
        shutil.copyfile(executable, artifact)
    elif sys.platform == 'darwin':
        # Preserve framework/resource symlinks in PyInstaller's .app bundle.
        artifact = basename.with_suffix('.zip')
        subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent',
                        str(dist / (name + '.app')), str(artifact)], check=True)
    else:
        artifact = basename.with_suffix('.tar.gz')
        with tarfile.open(artifact, 'w:gz') as archive:
            archive.add(executable, arcname=name)
    digest = hashlib.sha256()
    with artifact.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    artifact.with_name(artifact.name + '.sha256').write_text(
        digest.hexdigest() + '  ' + artifact.name + '\n', encoding='utf-8')
    print('Installer: %s (%.1f MB)' % (artifact, artifact.stat().st_size / 1e6))
    return 0


if __name__ == '__main__':
    sys.exit(main())
