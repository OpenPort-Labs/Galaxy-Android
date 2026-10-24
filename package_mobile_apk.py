import argparse
import glob
import os
import shutil
import subprocess
import sys
import zipfile

from tools.apk_env import (
    ROOT, SDK, BUILD_TOOLS_DIR, AAPT2, ZIPALIGN, APKSIGNER, D8,
    ANDROID_JAR, NDK_DIR, STRIP, JAVAC, KEY_ALIAS,
    prepare_signing_key, verify_signed_apk, manifest_is_debuggable,
    resolve_keystore_pass
)

BUILD_MOBILE = os.environ.get("BUILD_DIR", os.path.join(ROOT, "build-mobile"))
OUT = os.path.join(ROOT, "out-mobile")
INSTALLER_DIR = os.path.join(ROOT, "installer-mobile")
MANIFEST = os.path.join(ROOT, "platform", "mobile", "AndroidManifest.xml")

def safe_cmd_str(cmd):
    masked = []
    skip = False
    for a in cmd:
        if skip:
            masked.append("***")
            skip = False
        elif a in ("--ks-pass", "--key-pass") or a.startswith("-storepass"):
            masked.append(a)
            skip = True
        elif a.startswith("pass:"):
            masked.append("pass:***")
        else:
            masked.append(a)
    return " ".join(masked)

def run(cmd, env=None):
    print("[RUN]", safe_cmd_str(cmd))
    subprocess.run(cmd, check=True, env=env)

def require_deliberate_debuggable(channel=None):
    if channel is None:
        channel = os.environ.get("GALAXY_RELEASE_CHANNEL", "tester").lower()
    debuggable = manifest_is_debuggable(MANIFEST)
    if channel == "production":
        if debuggable:
            raise SystemExit(
                "REFUSING TO PACKAGE [production]: android:debuggable=\"true\" is set in AndroidManifest.xml. "
                "Production releases forbid debuggable builds. (Use --channel tester for sideload tester builds)."
            )
        return
    if not debuggable:
        return
    if os.environ.get("GALAXY_ALLOW_DEBUGGABLE") == "1":
        print("[MANIFEST] android:debuggable=\"true\" accepted: GALAXY_ALLOW_DEBUGGABLE=1 is set.")
        return
    raise SystemExit("REFUSING TO PACKAGE: android:debuggable=\"true\" is set. Set GALAXY_ALLOW_DEBUGGABLE=1 to build anyway.")

def main():
    parser = argparse.ArgumentParser(description="Package GalaxyAndroid.apk")
    parser.add_argument("--channel", choices=["tester", "production"],
                        default=os.environ.get("GALAXY_RELEASE_CHANNEL", "tester").lower(),
                        help="Build channel: 'tester' or 'production'")
    args, unknown = parser.parse_known_args()
    channel = args.channel

    print(f"=== Packaging Unified GalaxyAndroid.apk [channel: {channel}] ===")
    print(f"  SDK: {SDK}")
    print(f"  Build Tools: {BUILD_TOOLS_DIR}")
    print(f"  Platform JAR: {ANDROID_JAR}")
    print(f"  D8: {D8}")
    print(f"  Javac: {JAVAC}")
    print(f"  NDK: {NDK_DIR}")
    print(f"  Strip: {STRIP}")
    require_deliberate_debuggable(channel)
    keystore = prepare_signing_key(channel=channel)

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
        "--manifest", MANIFEST,
        "-R", res_zip,
        "--auto-add-overlay",
        "-I", ANDROID_JAR,
        "--java", gen_dir,
        "--min-sdk-version", "28",
        "--target-sdk-version", "34"
    ])

    # 2. Compile Java sources
    src_files = glob.glob(os.path.join(ROOT, "platform", "mobile", "src", "com", "openport", "galaxyandroid", "*.java"))
    r_files = glob.glob(os.path.join(gen_dir, "**", "*.java"), recursive=True)
    all_java = src_files + r_files
    print(f"[JAVAC] Compiling {len(all_java)} Java sources...")
    run([JAVAC, "-encoding", "UTF-8", "-cp", ANDROID_JAR, "-d", classes_dir, "-source", "1.8", "-target", "1.8"] + all_java)

    # 3. Desugar and Dex with D8
    class_files = glob.glob(os.path.join(classes_dir, "**", "*.class"), recursive=True)
    print(f"[D8] Converting {len(class_files)} classes to classes.dex...")
    run([D8, "--output", dex_dir, "--lib", ANDROID_JAR, "--min-api", "28"] + class_files)

    # 4. Copy wrap.sh and native libraries into arm64-v8a
    wrap_src = os.path.join(ROOT, "platform", "android", "wrap.sh")
    with open(wrap_src, "rb") as f_in, open(os.path.join(arm64_dir, "wrap.sh"), "wb") as f_out:
        f_out.write(f_in.read().replace(b"\r\n", b"\n"))

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

    nodtool_src = os.path.join(INSTALLER_DIR, "vendor", "nodtool", "libnodtool.so")
    if os.path.exists(nodtool_src):
        shutil.copyfile(nodtool_src, os.path.join(arm64_dir, "libnodtool.so"))

    installer_candidates = [
        os.path.join(BUILD_MOBILE, "libinstaller.so"),
        os.path.join(ROOT, "build-installer", "lib", "arm64-v8a", "libinstaller.so"),
        os.path.join(ROOT, "build-installer", "native", "libinstaller.so"),
    ]
    for cand in installer_candidates:
        if os.path.exists(cand):
            dst = os.path.join(arm64_dir, "libinstaller.so")
            if STRIP and os.path.exists(STRIP):
                run([STRIP, "--strip-unneeded", "-o", dst, cand])
            else:
                shutil.copyfile(cand, dst)
            break

    py_lib_dir = os.path.join(INSTALLER_DIR, "vendor", "python", "lib", "arm64-v8a")
    if os.path.isdir(py_lib_dir):
        for f in os.listdir(py_lib_dir):
            if f.endswith(".so"):
                shutil.copyfile(os.path.join(py_lib_dir, f), os.path.join(arm64_dir, f))

    # 5. Package unaligned APK
    unaligned_apk = os.path.join(OUT, "unaligned.apk")
    if os.path.exists(unaligned_apk):
        os.remove(unaligned_apk)
    shutil.copyfile(base_apk, unaligned_apk)

    with zipfile.ZipFile(unaligned_apk, "a", compression=zipfile.ZIP_DEFLATED) as z:
        added_entries = set()
        def add_file(src_path, arc_name):
            arc_name = arc_name.replace("\\", "/")
            if arc_name not in added_entries:
                z.write(src_path, arc_name)
                added_entries.add(arc_name)

        add_file(os.path.join(dex_dir, "classes.dex"), "classes.dex")

        assets_dir = os.path.join(INSTALLER_DIR, "assets")
        if os.path.isdir(assets_dir):
            for root, _, files in os.walk(assets_dir):
                for file in files:
                    p = os.path.join(root, file)
                    add_file(p, os.path.relpath(p, INSTALLER_DIR))

        vendor_zip = os.path.join(INSTALLER_DIR, "vendor", "python", "stdlib", "pythonhome.zip")
        if os.path.exists(vendor_zip):
            add_file(vendor_zip, "assets/pythonhome.zip")

        bundled_manifest = os.path.join(ROOT, "assets", "galaxy_shader_manifest.bin")
        if not os.path.exists(bundled_manifest):
            bundled_manifest = os.path.join(ROOT, "galaxy_shader_manifest.bin")
        if os.path.exists(bundled_manifest):
            add_file(bundled_manifest, "assets/galaxy_shader_manifest.bin")

        for root, _, files in os.walk(os.path.join(apk_dir, "lib")):
            for file in files:
                p = os.path.join(root, file)
                add_file(p, os.path.relpath(p, apk_dir))

    # 6. Zipalign (4-byte alignment, 16KB for shared libraries)
    aligned_apk = os.path.join(OUT, "GalaxyAndroid-unsigned.apk")
    if os.path.exists(aligned_apk):
        os.remove(aligned_apk)
    run([ZIPALIGN, "-f", "-p", "4", unaligned_apk, aligned_apk])

    # 7. Sign APK (out-mobile is what the release workflow uploads and docs name)
    final_apk = os.path.join(OUT, "GalaxyAndroid.apk")
    if os.path.exists(final_apk):
        os.remove(final_apk)
    storepass = resolve_keystore_pass(channel=channel)
    sign_env = dict(os.environ)
    sign_env["GALAXY_KEYSTORE_PASS"] = storepass
    run([
        APKSIGNER, "sign",
        "--ks", keystore,
        "--ks-pass", "env:GALAXY_KEYSTORE_PASS",
        "--ks-key-alias", KEY_ALIAS,
        "--out", final_apk,
        aligned_apk
    ], env=sign_env)

    verify_signed_apk(final_apk, channel=channel)
    print(f"=== Successfully packaged and signed [{channel}]: {final_apk} ===")

if __name__ == "__main__":
    main()
