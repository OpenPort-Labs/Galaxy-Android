#!/usr/bin/env python3
"""Build and package GalaxyMobile.apk on Windows without bash dependencies."""
import os
import shutil
import subprocess
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.expandvars(r"%LOCALAPPDATA%\Android\Sdk")
BUILD_TOOLS_DIR = os.path.join(SDK, "build-tools", "37.0.0")
AAPT2 = os.path.join(BUILD_TOOLS_DIR, "aapt2.exe")
ZIPALIGN = os.path.join(BUILD_TOOLS_DIR, "zipalign.exe")
APKSIGNER = os.path.join(BUILD_TOOLS_DIR, "apksigner.bat")

ANDROID_JAR = os.path.join(SDK, "platforms", "android-34", "android.jar")
if not os.path.exists(ANDROID_JAR):
    ANDROID_JAR = os.path.join(SDK, "platforms", "android-37.0", "android.jar")

NDK_DIR = os.path.join(SDK, "ndk", "28.2.13676358")
if not os.path.exists(NDK_DIR):
    NDK_DIR = os.path.join(SDK, "ndk", "27.3.13750724")
STRIP = os.path.join(NDK_DIR, "toolchains", "llvm", "prebuilt", "windows-x86_64", "bin", "llvm-strip.exe")

BUILD = os.path.join(ROOT, "build-mobile")
OUT = os.path.join(ROOT, "out-mobile")
KEYSTORE = os.path.join(ROOT, "out", "debug.keystore")

def run(cmd):
    print("[RUN]", " ".join(cmd))
    subprocess.run(cmd, check=True)

def main():
    print("=== Packaging GalaxyMobile.apk (com.galaxy.mobile) ===")
    os.makedirs(OUT, exist_ok=True)
    apk_dir = os.path.join(OUT, "apk")
    shutil.rmtree(apk_dir, ignore_errors=True)
    arm64_dir = os.path.join(apk_dir, "lib", "arm64-v8a")
    os.makedirs(arm64_dir, exist_ok=True)

    # 1. Compile resources with AAPT2
    res_zip = os.path.join(OUT, "res.zip")
    if os.path.exists(res_zip):
        os.remove(res_zip)
    run([AAPT2, "compile", "--dir", os.path.join(ROOT, "platform", "android", "res"), "-o", res_zip])

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
        "--min-sdk-version", "30",
        "--target-sdk-version", "34"
    ])

    # 2. Copy and strip wrap.sh and native libraries
    wrap_src = os.path.join(ROOT, "platform", "android", "wrap.sh")
    with open(wrap_src, "rb") as f_in, open(os.path.join(arm64_dir, "wrap.sh"), "wb") as f_out:
        f_out.write(f_in.read().replace(b"\r\n", b"\n"))

    for lib in ["libgame.so", "libmain.so", "libreserve.so"]:
        src_lib = os.path.join(BUILD, lib)
        dst_lib = os.path.join(arm64_dir, lib)
        if os.path.exists(STRIP):
            run([STRIP, "--strip-unneeded", "-o", dst_lib, src_lib])
        else:
            shutil.copyfile(src_lib, dst_lib)

    # 3. Add lib folder into base.apk
    with zipfile.ZipFile(base_apk, "a", zipfile.ZIP_DEFLATED) as z:
        for root, _, files in os.walk(os.path.join(apk_dir, "lib")):
            for f in files:
                p = os.path.join(root, f)
                arc = os.path.relpath(p, apk_dir).replace("\\", "/")
                z.write(p, arc)

    # 4. Zipalign (16-byte alignment required for Android page size compatibility)
    aligned_apk = os.path.join(OUT, "aligned.apk")
    if os.path.exists(aligned_apk):
        os.remove(aligned_apk)
    run([ZIPALIGN, "-f", "-p", "16", base_apk, aligned_apk])

    # 5. Apksigner
    final_apk = os.path.join(OUT, "GalaxyMobile.apk")
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

    # Cleanup temp
    for temp in [base_apk, aligned_apk, res_zip]:
        if os.path.exists(temp):
            os.remove(temp)
    shutil.rmtree(apk_dir, ignore_errors=True)

    print(f"\n[SUCCESS] Built {final_apk} ({os.path.getsize(final_apk)} bytes)")

if __name__ == "__main__":
    main()
