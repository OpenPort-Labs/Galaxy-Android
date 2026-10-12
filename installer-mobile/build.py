import os
import shutil
import subprocess
import sys
import zipfile

def run(cmd, cwd=None):
    print('[RUN]', ' '.join(cmd) if isinstance(cmd, list) else cmd)
    res = subprocess.run(cmd, cwd=cwd, shell=True if isinstance(cmd, str) else False)
    if res.returncode != 0:
        sys.exit(f'Command failed with exit code {res.returncode}')

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
INSTALLER_DIR = os.path.join(ROOT, 'installer-mobile')
BUILD_DIR = os.path.join(ROOT, 'build-installer')
OUT_DIR = os.path.join(ROOT, 'out-mobile')

SDK = os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT') or r'C:\Users\carlo\AppData\Local\Android\Sdk'

def find_latest(subpath):
    p = os.path.join(SDK, subpath)
    if os.path.exists(p):
        dirs = [d for d in os.listdir(p) if os.path.isdir(os.path.join(p, d))]
        if dirs:
            dirs.sort()
            return os.path.join(p, dirs[-1])
    return p

NDK = os.environ.get('ANDROID_NDK_ROOT') or os.environ.get('NDK') or find_latest('ndk')
cmake_dir = find_latest('cmake')
CMAKE = os.path.join(cmake_dir, 'bin', 'cmake.exe' if sys.platform == 'win32' else 'cmake')
NINJA = os.path.join(cmake_dir, 'bin', 'ninja.exe' if sys.platform == 'win32' else 'ninja')
BT = find_latest('build-tools')
AAPT2 = os.path.join(BT, 'aapt2.exe' if sys.platform == 'win32' else 'aapt2')
D8 = os.path.join(BT, 'd8.bat' if sys.platform == 'win32' else 'd8')
APKSIGNER = os.path.join(BT, 'apksigner.bat' if sys.platform == 'win32' else 'apksigner')
ZIPALIGN = os.path.join(BT, 'zipalign.exe' if sys.platform == 'win32' else 'zipalign')

# Find platform android.jar
platforms_dir = os.path.join(SDK, 'platforms')
android_jar_candidates = []
if os.path.exists(platforms_dir):
    for p in os.listdir(platforms_dir):
        j = os.path.join(platforms_dir, p, 'android.jar')
        if os.path.exists(j):
            android_jar_candidates.append(j)
android_jar_candidates.sort()
ANDROID_JAR = android_jar_candidates[-1] if android_jar_candidates else os.path.join(SDK, 'platforms', 'android-34', 'android.jar')

# JAVAC detection
JAVAC = None
if 'JAVA_HOME' in os.environ:
    cand = os.path.join(os.environ['JAVA_HOME'], 'bin', 'javac.exe' if sys.platform == 'win32' else 'javac')
    if os.path.exists(cand):
        JAVAC = cand

if not JAVAC:
    as_jbr = r'C:\Program Files\Android\Android Studio\jbr\bin\javac.exe'
    if os.path.exists(as_jbr):
        JAVAC = as_jbr
    else:
        JAVAC = shutil.which('javac') or 'javac'

os.makedirs(BUILD_DIR, exist_ok=True)
os.makedirs(OUT_DIR, exist_ok=True)

print('=== 1. Building Native Library (libinstaller.so) ===')
native_build = os.path.join(BUILD_DIR, 'native')
os.makedirs(native_build, exist_ok=True)

toolchain = os.path.join(NDK, 'build', 'cmake', 'android.toolchain.cmake')
cmake_cmd = [
    CMAKE,
    '-G', 'Ninja',
    f'-DCMAKE_MAKE_PROGRAM={NINJA}',
    f'-DCMAKE_TOOLCHAIN_FILE={toolchain}',
    '-DANDROID_ABI=arm64-v8a',
    '-DANDROID_PLATFORM=android-26',
    os.path.join(INSTALLER_DIR, 'jni')
]
run(cmake_cmd, cwd=native_build)
run([NINJA], cwd=native_build)

print('=== 2. Compiling Resources (AAPT2) ===')
res_zip = os.path.join(BUILD_DIR, 'res.zip')
run([AAPT2, 'compile', '--dir', os.path.join(INSTALLER_DIR, 'res'), '-o', res_zip])

base_apk = os.path.join(BUILD_DIR, 'base.apk')
if os.path.exists(base_apk):
    os.remove(base_apk)

run([
    AAPT2, 'link',
    '-o', base_apk,
    '--manifest', os.path.join(INSTALLER_DIR, 'AndroidManifest.xml'),
    '-R', res_zip,
    '-I', ANDROID_JAR,
    '-A', os.path.join(INSTALLER_DIR, 'assets'),
    '--auto-add-overlay',
    '--min-sdk-version', '26',
    '--target-sdk-version', '34'
])

print('=== 3. Compiling Java Sources ===')
obj_dir = os.path.join(BUILD_DIR, 'obj')
dex_dir = os.path.join(BUILD_DIR, 'dex')
shutil.rmtree(obj_dir, ignore_errors=True)
shutil.rmtree(dex_dir, ignore_errors=True)
os.makedirs(obj_dir, exist_ok=True)
os.makedirs(dex_dir, exist_ok=True)

java_files = [
    os.path.join(INSTALLER_DIR, 'src', 'com', 'galaxy', 'installer', 'MainActivity.java'),
    os.path.join(INSTALLER_DIR, 'src', 'com', 'galaxy', 'installer', 'CookerEngine.java'),
]

# Generate R.java
r_dir = os.path.join(BUILD_DIR, 'r_gen')
shutil.rmtree(r_dir, ignore_errors=True)
os.makedirs(r_dir, exist_ok=True)
run([
    AAPT2, 'link',
    '-o', os.path.join(BUILD_DIR, 'temp.apk'),
    '--manifest', os.path.join(INSTALLER_DIR, 'AndroidManifest.xml'),
    '-R', res_zip,
    '-I', ANDROID_JAR,
    '--java', r_dir,
    '--auto-add-overlay'
])

all_javas = java_files + [os.path.join(r_dir, 'com', 'galaxy', 'installer', 'R.java')]
run([JAVAC, '-source', '17', '-target', '17', '-cp', ANDROID_JAR, '-d', obj_dir] + all_javas)

# Run d8
class_files = []
for root, _, files in os.walk(obj_dir):
    for f in files:
        if f.endswith('.class'):
            class_files.append(os.path.join(root, f))

run([D8, '--min-api', '26', '--output', dex_dir] + class_files)

print('=== 4. Packaging Classes and Native Libraries into APK ===')
with zipfile.ZipFile(base_apk, 'a', zipfile.ZIP_DEFLATED) as z:
    # 1. Add classes.dex
    dex_path = os.path.join(dex_dir, 'classes.dex')
    z.write(dex_path, 'classes.dex')

    # 2. Add native libraries
    libs = [
        (os.path.join(native_build, 'libinstaller.so'), 'lib/arm64-v8a/libinstaller.so'),
        (os.path.join(INSTALLER_DIR, 'vendor', 'nodtool', 'libnodtool.so'), 'lib/arm64-v8a/libnodtool.so'),
    ]
    py_lib_dir = os.path.join(INSTALLER_DIR, 'vendor', 'python', 'lib', 'arm64-v8a')
    if os.path.exists(py_lib_dir):
        for f in os.listdir(py_lib_dir):
            if f.endswith('.so'):
                libs.append((os.path.join(py_lib_dir, f), f'lib/arm64-v8a/{f}'))

    for src, arc in libs:
        if os.path.exists(src):
            z.write(src, arc)

print('=== 5. Zipalign & Apksigner ===')
aligned_apk = os.path.join(BUILD_DIR, 'aligned.apk')
final_apk = os.path.join(OUT_DIR, 'GalaxyInstaller.apk')

if os.path.exists(aligned_apk):
    os.remove(aligned_apk)
if os.path.exists(final_apk):
    os.remove(final_apk)

run([ZIPALIGN, '-p', '-f', '4', base_apk, aligned_apk])

keystore = os.path.join(ROOT, 'out', 'debug.keystore')
if not os.path.exists(keystore):
    keystore = os.path.join(ROOT, 'out-mobile', 'debug.keystore')

run([
    APKSIGNER, 'sign',
    '--ks', keystore,
    '--ks-pass', 'pass:android',
    '--out', final_apk,
    aligned_apk
])

size = os.path.getsize(final_apk)
print(f'SUCCESS! Created {final_apk} ({size:,} bytes)')
