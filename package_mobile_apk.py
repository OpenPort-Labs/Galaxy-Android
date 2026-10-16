#!/usr/bin/env python3
"""Build and package the unified GalaxyAndroid.apk (Game + In-App Disc Loader)."""
import glob
import os
import shutil
import subprocess
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))

# 1. Resolve Android SDK
SDK = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
if not SDK or not os.path.isdir(SDK):
    if os.path.isdir(r"C:\Android\Sdk"):
        SDK = r"C:\Android\Sdk"
    else:
        SDK = os.path.expandvars(r"%LOCALAPPDATA%\Android\Sdk")

# 2. Resolve Build Tools (pick highest version)
bt_parent = os.path.join(SDK, "build-tools") if SDK else ""
BUILD_TOOLS_DIR = ""
if os.path.isdir(bt_parent):
    try:
        versions = sorted([v for v in os.listdir(bt_parent) if os.path.isdir(os.path.join(bt_parent, v))],
                          key=lambda s: [int(x) if x.isdigit() else x for x in s.split('.')])
        if versions:
            BUILD_TOOLS_DIR = os.path.join(bt_parent, versions[-1])
    except Exception:
        pass

AAPT2 = os.path.join(BUILD_TOOLS_DIR, "aapt2.exe") if os.name == "nt" else os.path.join(BUILD_TOOLS_DIR, "aapt2")
ZIPALIGN = os.path.join(BUILD_TOOLS_DIR, "zipalign.exe") if os.name == "nt" else os.path.join(BUILD_TOOLS_DIR, "zipalign")
APKSIGNER = os.path.join(BUILD_TOOLS_DIR, "apksigner.bat") if os.name == "nt" else os.path.join(BUILD_TOOLS_DIR, "apksigner")
D8 = os.path.join(BUILD_TOOLS_DIR, "d8.bat") if os.name == "nt" else os.path.join(BUILD_TOOLS_DIR, "d8")

# 3. Resolve android.jar (pick highest available API platform)
ANDROID_JAR = ""
platforms_dir = os.path.join(SDK, "platforms") if SDK else ""
if os.path.isdir(platforms_dir):
    candidates = sorted([p for p in os.listdir(platforms_dir) if os.path.exists(os.path.join(platforms_dir, p, "android.jar"))], reverse=True)
    if candidates:
        ANDROID_JAR = os.path.join(platforms_dir, candidates[0], "android.jar")

# 4. Resolve NDK & Strip
NDK_DIR = os.environ.get("ANDROID_NDK_HOME") or ""
if not NDK_DIR and SDK:
    ndk_parent = os.path.join(SDK, "ndk")
    if os.path.isdir(ndk_parent):
        ndk_candidates = sorted([v for v in os.listdir(ndk_parent) if os.path.isdir(os.path.join(ndk_parent, v))], reverse=True)
        if ndk_candidates:
            NDK_DIR = os.path.join(ndk_parent, ndk_candidates[0])

STRIP = ""
if NDK_DIR:
    strip_pattern = os.path.join(NDK_DIR, "toolchains", "llvm", "prebuilt", "*", "bin", "llvm-strip" + (".exe" if os.name == "nt" else ""))
    matches = glob.glob(strip_pattern)
    if matches:
        STRIP = matches[0]

# 5. Resolve JAVAC
JAVAC = shutil.which("javac")
if not JAVAC:
    for candidate in [r"C:\Program Files\Java\jdk-17\bin\javac.exe", r"C:\Program Files\Java\latest\bin\javac.exe"]:
        if os.path.exists(candidate):
            JAVAC = candidate
            break
if not JAVAC:
    JAVAC = "javac"

BUILD_MOBILE = os.path.join(ROOT, "build-mobile")
OUT = os.path.join(ROOT, "out-mobile")
KEYSTORE = os.path.join(ROOT, "platform", "mobile", "release.keystore")
INSTALLER_DIR = os.path.join(ROOT, "installer-mobile")

def run(cmd):
    print("[RUN]", " ".join(cmd))
    subprocess.run(cmd, check=True)

def main():
    print("=== Packaging Unified GalaxyAndroid.apk (com.openport.galaxyandroid) ===")
    print(f"  SDK: {SDK}")
    print(f"  Build Tools: {BUILD_TOOLS_DIR}")
    print(f"  Platform JAR: {ANDROID_JAR}")
    print(f"  D8: {D8}")
    print(f"  Javac: {JAVAC}")
    print(f"  NDK: {NDK_DIR}")
    print(f"  Strip: {STRIP}")

    os.makedirs(OUT, exist_ok=True)
    os.makedirs(os.path.join(ROOT, "out"), exist_ok=True)

    gen_dir = os.path.join(OUT, "gen")
    classes_dir = os.path.join(OUT, "classes")
    dex_dir = os.path.join(OUT, "dex")
    apk_dir = os.path.join(OUT, "apk")

    for d in [gen_dir, classes_dir, dex_dir, apk_dir]:
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d, exist_ok=True)

    arm64_dir = os.path.join(apk_dir, "lib", "arm64-v8a")
    os.makedirs(arm64_dir, exist_ok=True)

    # 0. Ensure debug keystore exists
    if not os.path.exists(KEYSTORE):
        print("[KEYSTORE] Generating debug keystore...")
        keytool = shutil.which("keytool") or "keytool"
        subprocess.run([
            keytool, "-genkeypair", "-keystore", KEYSTORE,
            "-storepass", "android", "-keypass", "android",
            "-alias", "androiddebugkey", "-keyalg", "RSA", "-keysize", "2048",
            "-validity", "10000", "-dname", "CN=GalaxyAndroid Debug,O=Local,C=US"
        ], check=True)

    # 1. Compile resources with AAPT2 and generate R.java
    res_zip = os.path.join(OUT, "res.zip")
    if os.path.exists(res_zip):
        os.remove(res_zip)
    run([AAPT2, "compile", "--dir", os.path.join(ROOT, "platform", "mobile", "res"), "-o", res_zip])

    base_apk = os.path.join(OUT, "base.apk")
    if os.path.exists(base_apk):
        os.remove(base_apk)
    run([
        AAPT2, "link",
        "-o", base_apk,
        "--manifest", os.path.join(ROOT, "platform", "mobile", "AndroidManifest.xml"),
        "-R", res_zip,
        "--auto-add-overlay",
        "-I", ANDROID_JAR,
        "--java", gen_dir,
        "--min-sdk-version", "30",
        "--target-sdk-version", "34"
    ])

    # 2. Compile Java sources (MainActivity, CookerEngine, GameActivity, R.java)
    src_files = glob.glob(os.path.join(ROOT, "platform", "mobile", "src", "com", "openport", "galaxyandroid", "*.java"))
    r_files = glob.glob(os.path.join(gen_dir, "**", "*.java"), recursive=True)
    all_java = src_files + r_files
    print(f"[JAVAC] Compiling {len(all_java)} Java sources...")
    run([JAVAC, "-encoding", "UTF-8", "-cp", ANDROID_JAR, "-d", classes_dir, "-source", "1.8", "-target", "1.8"] + all_java)

    # 3. Desugar and Dex with D8
    class_files = glob.glob(os.path.join(classes_dir, "**", "*.class"), recursive=True)
    print(f"[D8] Converting {len(class_files)} classes to classes.dex...")
    run([D8, "--output", dex_dir, "--lib", ANDROID_JAR, "--min-api", "30"] + class_files)

    # 4. Copy and strip wrap.sh and native libraries into arm64-v8a
    wrap_src = os.path.join(ROOT, "platform", "android", "wrap.sh")
    with open(wrap_src, "rb") as f_in, open(os.path.join(arm64_dir, "wrap.sh"), "wb") as f_out:
        f_out.write(f_in.read().replace(b"\r\n", b"\n"))

    # Game engine libraries
    game_libs = ["libgame.so", "libmain.so", "libreserve.so"]
    for lib in game_libs:
        src_lib = os.path.join(BUILD_MOBILE, lib)
        dst_lib = os.path.join(arm64_dir, lib)
        if os.path.exists(src_lib):
            if STRIP and os.path.exists(STRIP):
                run([STRIP, "--strip-unneeded", "-o", dst_lib, src_lib])
            else:
                shutil.copyfile(src_lib, dst_lib)
        else:
            print(f"[WARN] Native library {src_lib} not found in build directory.")

    # Cooker and disc extractor libraries
    nodtool_src = os.path.join(INSTALLER_DIR, "vendor", "nodtool", "libnodtool.so")
    if os.path.exists(nodtool_src):
        shutil.copyfile(nodtool_src, os.path.join(arm64_dir, "libnodtool.so"))

    installer_candidates = [
        os.path.join(BUILD_MOBILE, "libinstaller.so"),
        os.path.join(ROOT, "build-installer", "lib", "arm64-v8a", "libinstaller.so"),
        os.path.join(ROOT, "build-installer", "native", "libinstaller.so"),
    ]
    installer_found = False
    for cand in installer_candidates:
        if os.path.exists(cand):
            dst = os.path.join(arm64_dir, "libinstaller.so")
            if STRIP and os.path.exists(STRIP):
                run([STRIP, "--strip-unneeded", "-o", dst, cand])
            else:
                shutil.copyfile(cand, dst)
            installer_found = True
            break
    if not installer_found:
        print("[WARN] libinstaller.so not found in any build candidate path!")

    # Python runtime libraries
    py_lib_dir = os.path.join(INSTALLER_DIR, "vendor", "python", "lib", "arm64-v8a")
    if os.path.isdir(py_lib_dir):
        for lib in os.listdir(py_lib_dir):
            if lib.endswith(".so"):
                shutil.copyfile(os.path.join(py_lib_dir, lib), os.path.join(arm64_dir, lib))

    # Extra prebuilt libraries (e.g. Vulkan validation layers)
    extra_libs_dir = os.path.join(ROOT, "platform", "mobile", "libs", "arm64-v8a")
    if os.path.isdir(extra_libs_dir):
        for lib in os.listdir(extra_libs_dir):
            if lib.endswith(".so"):
                print(f"[EXTRA LIB] Packaging {lib}...")
                shutil.copyfile(os.path.join(extra_libs_dir, lib), os.path.join(arm64_dir, lib))

    # 5. Add dex, assets, and lib into base.apk
    dex_file = os.path.join(dex_dir, "classes.dex")
    with zipfile.ZipFile(base_apk, "a") as z:
        # Add classes.dex
        if os.path.exists(dex_file):
            z.write(dex_file, "classes.dex", compress_type=zipfile.ZIP_DEFLATED)

        # Add embedded python and scripts assets
        assets_dir = os.path.join(INSTALLER_DIR, "assets")
        pyhome_target = os.path.join(assets_dir, "pythonhome.zip")
        if not os.path.exists(pyhome_target):
            vendor_zip = os.path.join(INSTALLER_DIR, "vendor", "python", "stdlib", "pythonhome.zip")
            if os.path.exists(vendor_zip):
                shutil.copyfile(vendor_zip, pyhome_target)
        if not os.path.exists(pyhome_target):
            raise RuntimeError("CRITICAL ERROR: pythonhome.zip is missing! Cannot build APK without Python environment.")

        if os.path.isdir(assets_dir):
            for root, _, files in os.walk(assets_dir):
                for f in files:
                    p = os.path.join(root, f)
                    arc = os.path.relpath(p, INSTALLER_DIR).replace("\\", "/")
                    z.write(p, arc, compress_type=zipfile.ZIP_DEFLATED)

        # Add native libraries and wrap.sh (store uncompressed with 0755 execute permissions)
        for root, _, files in os.walk(os.path.join(apk_dir, "lib")):
            for f in files:
                p = os.path.join(root, f)
                arc = os.path.relpath(p, apk_dir).replace("\\", "/")
                compress_type = zipfile.ZIP_STORED if (f.endswith(".so") or f.endswith(".sh")) else zipfile.ZIP_DEFLATED
                info = zipfile.ZipInfo(arc)
                info.compress_type = compress_type
                info.external_attr = (0o100755 << 16) if (f.endswith(".so") or f.endswith(".sh")) else (0o100644 << 16)
                with open(p, "rb") as f_data:
                    z.writestr(info, f_data.read())

    # 6. Zipalign (16KB page alignment required for Android 14/15 Snapdragon 8 Gen 2/3 / Adreno 740/840)
    aligned_apk = os.path.join(OUT, "aligned.apk")
    if os.path.exists(aligned_apk):
        os.remove(aligned_apk)
    run([ZIPALIGN, "-f", "-P", "16", "4", base_apk, aligned_apk])

    # 7. Apksigner (v1 + v2 + v3 signing)
    final_apk = os.path.join(OUT, "GalaxyAndroid.apk")
    if os.path.exists(final_apk):
        os.remove(final_apk)
    run([
        APKSIGNER, "sign",
        "--ks", KEYSTORE,
        "--ks-pass", "pass:android",
        "--key-pass", "pass:android",
        "--out", final_apk,
        aligned_apk
    ])

    # Cleanup temp build files
    for temp in [base_apk, aligned_apk, res_zip]:
        if os.path.exists(temp):
            os.remove(temp)
    for d in [gen_dir, classes_dir, dex_dir, apk_dir]:
        shutil.rmtree(d, ignore_errors=True)

    print(f"\n[SUCCESS] Built unified package: {final_apk} ({os.path.getsize(final_apk)} bytes)")

if __name__ == "__main__":
    main()
