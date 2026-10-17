#!/usr/bin/env python3
"""Build and package the unified GalaxyAndroid.apk (Game + In-App Disc Loader)."""
import base64
import glob
import os
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET
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
# Signing key.  No keystore is committed to the repository: set the GALAXY_*
# environment variables for a release build, otherwise a throwaway local key is
# generated under out/ (git-ignored).
KEYSTORE_PASS = os.environ.get("GALAXY_KEYSTORE_PASS", "android")
KEY_ALIAS = os.environ.get("GALAXY_KEY_ALIAS", "androiddebugkey")
KEY_PASS = os.environ.get("GALAXY_KEY_PASS", KEYSTORE_PASS)
KEYTOOL = shutil.which("keytool") or "keytool"

# An APK can only replace an already installed one when both carry the same
# signing certificate; anything else makes Android refuse the update with
# INSTALL_FAILED_UPDATE_INCOMPATIBLE and forces the player to uninstall first
# (issues #35 and #36).  The certificate is therefore pinned: the build signs
# with the project key and then proves, from the finished APK, that it did.
# The digest is a public value; it is what tells a packaged APK apart from one
# signed with a freshly generated (and therefore incompatible) key.
PROJECT_KEY_SHA256 = "1883cd600aacd5628513e23ce646f4c8ed19c040a14575fbc922d33f6686feff"
EXPECTED_SIGNER = os.environ.get("GALAXY_EXPECTED_SIGNER_SHA256", PROJECT_KEY_SHA256)
# Where the project key may live.  out/ is git-ignored, so the key stays out of
# the repository; CI passes it in through GALAXY_KEYSTORE_B64 instead.
LOCAL_KEYSTORE = os.path.join(ROOT, "out", "release.keystore")
KEYSTORE_CANDIDATES = [LOCAL_KEYSTORE, os.path.join(ROOT, "out", "debug.keystore"),
                       os.path.join(ROOT, "platform", "mobile", "release.keystore")]
DEV_KEYSTORE = os.path.join(ROOT, "out", "dev.keystore")
KEYSTORE = ""  # resolved by prepare_signing_key()
INSTALLER_DIR = os.path.join(ROOT, "installer-mobile")
MANIFEST = os.path.join(ROOT, "platform", "mobile", "AndroidManifest.xml")
ANDROID_NS = "{http://schemas.android.com/apk/res/android}"

def run(cmd):
    print("[RUN]", " ".join(cmd))
    subprocess.run(cmd, check=True)


def manifest_is_debuggable(path=MANIFEST):
    """The application element's android:debuggable attribute, read as XML so a
    mention of it in a comment cannot be mistaken for the attribute itself."""
    application = ET.parse(path).getroot().find("application")
    return application is not None and application.get(ANDROID_NS + "debuggable") == "true"


def normalize_fingerprint(value):
    return re.sub(r"[^0-9a-f]", "", value.lower())


def keystore_fingerprint(keystore):
    """SHA-256 of the signing certificate in *keystore*, or None if unreadable."""
    try:
        proc = subprocess.run([KEYTOOL, "-list", "-v", "-keystore", keystore,
                               "-storepass", KEYSTORE_PASS],
                              capture_output=True, text=True)
    except OSError:
        return None
    match = re.search(r"SHA-?256:\s*([0-9A-Fa-f:]+)", proc.stdout)
    return normalize_fingerprint(match.group(1)) if match else None


def prepare_signing_key():
    """Resolve the keystore to sign with and check it is the project identity.

    Resolution order:
      GALAXY_KEYSTORE_B64  base64 keystore, the form CI keeps as a secret
      GALAXY_KEYSTORE      path to a keystore
      out/release.keystore the project key, git-ignored
      out/debug.keystore, platform/mobile/release.keystore  earlier locations

    None of them present is an error, not a reason to invent a key: a key made
    up on the spot cannot update a single existing installation.  A development
    key is only generated when GALAXY_ALLOW_NEW_KEY=1 says so explicitly, and
    the certificate check below is then skipped because such a build is not an
    update of anything.
    """
    global KEYSTORE

    encoded = os.environ.get("GALAXY_KEYSTORE_B64")
    if encoded:
        try:
            data = base64.b64decode(encoded)
        except Exception as exc:  # noqa: BLE001 - report whatever base64 said
            raise SystemExit(f"GALAXY_KEYSTORE_B64 is not valid base64: {exc}")
        if len(data) < 128:
            raise SystemExit(f"GALAXY_KEYSTORE_B64 decoded to {len(data)} bytes, which is not a keystore")
        os.makedirs(os.path.dirname(LOCAL_KEYSTORE), exist_ok=True)
        with open(LOCAL_KEYSTORE, "wb") as handle:
            handle.write(data)
        try:
            os.chmod(LOCAL_KEYSTORE, 0o600)
        except OSError:
            pass
        print(f"[KEYSTORE] Decoded GALAXY_KEYSTORE_B64 into {LOCAL_KEYSTORE}")
        KEYSTORE = LOCAL_KEYSTORE
    else:
        explicit = os.environ.get("GALAXY_KEYSTORE")
        for candidate in ([explicit] if explicit else []) + KEYSTORE_CANDIDATES:
            if candidate and os.path.exists(candidate):
                KEYSTORE = candidate
                break
        if not KEYSTORE:
            if os.environ.get("GALAXY_ALLOW_NEW_KEY") == "1":
                print("[KEYSTORE] GALAXY_ALLOW_NEW_KEY=1: generating a development key at %s" % DEV_KEYSTORE)
                print("[KEYSTORE] WARNING: this key is not the project key, so the APK cannot update an")
                print("[KEYSTORE] WARNING: existing install of Galaxy Android.")
                subprocess.run([KEYTOOL, "-genkeypair", "-keystore", DEV_KEYSTORE,
                                "-storepass", KEYSTORE_PASS, "-keypass", KEY_PASS,
                                "-alias", KEY_ALIAS, "-keyalg", "RSA", "-keysize", "2048",
                                "-validity", "10000", "-dname", "CN=GalaxyAndroid Dev,O=Local,C=US"],
                               check=True)
                KEYSTORE = DEV_KEYSTORE
                return KEYSTORE
            raise SystemExit(
                "REFUSING TO PACKAGE: no signing key found.\n"
                "Updates only work when every APK is signed with the same certificate, so this\n"
                "build will not invent one.  Provide the project key as either\n"
                "  GALAXY_KEYSTORE_B64=<base64 of the keystore>   (CI: repository secret)\n"
                "  GALAXY_KEYSTORE=<path to the keystore>\n"
                f"or place it at {LOCAL_KEYSTORE} (git-ignored).\n"
                "Set GALAXY_ALLOW_NEW_KEY=1 to build a throwaway development APK instead;\n"
                "it will NOT update an existing installation.")

    fingerprint = keystore_fingerprint(KEYSTORE)
    if not fingerprint:
        print(f"[KEYSTORE] Could not read a SHA-256 from {KEYSTORE}; skipping the identity check")
        return KEYSTORE

    if EXPECTED_SIGNER and fingerprint != normalize_fingerprint(EXPECTED_SIGNER):
        raise SystemExit(
            f"REFUSING TO PACKAGE: {KEYSTORE} holds certificate\n"
            f"  {fingerprint}\n"
            f"but the project key is\n"
            f"  {normalize_fingerprint(EXPECTED_SIGNER)}\n"
            "APKs signed with a different key cannot update an installed copy of Galaxy\n"
            "Android (INSTALL_FAILED_UPDATE_INCOMPATIBLE).  Use the project key, or set\n"
            "GALAXY_EXPECTED_SIGNER_SHA256 to the fingerprint you intend to ship.")

    print(f"[KEYSTORE] {KEYSTORE} (certificate SHA-256 {fingerprint})")
    if KEYSTORE != LOCAL_KEYSTORE:
        print(f"[KEYSTORE] Move the key to {LOCAL_KEYSTORE} so it is not confused with a build artifact")
    return KEYSTORE


def verify_signed_apk(path):
    """Prove the finished APK carries the expected certificate, and say so."""
    proc = subprocess.run([APKSIGNER, "verify", "--print-certs", path], capture_output=True, text=True)
    text = proc.stdout + proc.stderr
    match = re.search(r"certificate SHA-256 digest:\s*([0-9A-Fa-f]+)", text)
    if proc.returncode != 0 or not match:
        raise SystemExit(f"REFUSING TO RELEASE: {path} did not verify:\n{text.strip()}")
    fingerprint = normalize_fingerprint(match.group(1))
    if os.environ.get("GALAXY_ALLOW_NEW_KEY") == "1":
        print(f"[SIGNED] {os.path.basename(path)}: {fingerprint} (development key, not an update path)")
        return fingerprint
    if fingerprint != normalize_fingerprint(EXPECTED_SIGNER):
        raise SystemExit(
            f"REFUSING TO RELEASE: {path} is signed with\n  {fingerprint}\n"
            f"but the project key is\n  {normalize_fingerprint(EXPECTED_SIGNER)}\n"
            "Shipping it would force every existing player to uninstall first.")
    print(f"[SIGNED] {os.path.basename(path)}: {fingerprint} (project key; updates in place)")
    return fingerprint


def require_deliberate_debuggable():
    """Refuse to package a debuggable APK unless the maintainer asked for one.

    android:debuggable="true" is deliberate here - Android only honours
    lib/<abi>/wrap.sh in a debuggable app, and wrap.sh preloads libreserve.so,
    which reserves the game's 32-bit window before ART maps its heap - but the
    resulting APK is debuggable and must not be published to a store.
    """
    if not manifest_is_debuggable():
        return
    if os.environ.get("GALAXY_ALLOW_DEBUGGABLE") == "1":
        print("[MANIFEST] android:debuggable=\"true\" accepted: GALAXY_ALLOW_DEBUGGABLE=1 is set.")
        print("[MANIFEST] wrap.sh needs it for the reserved window; sideload only, never a store.")
        return
    raise SystemExit(
        "REFUSING TO PACKAGE: android:debuggable=\"true\" is set in "
        "platform/mobile/AndroidManifest.xml.\n"
        "It is load-bearing (wrap.sh -> libreserve.so reserves the 32-bit window), so it is\n"
        "deliberate for sideload builds, but a debuggable APK must not be published.\n"
        "Set GALAXY_ALLOW_DEBUGGABLE=1 to build this APK anyway.")

def main():
    print("=== Packaging Unified GalaxyAndroid.apk (com.openport.galaxyandroid) ===")
    print(f"  SDK: {SDK}")
    print(f"  Build Tools: {BUILD_TOOLS_DIR}")
    print(f"  Platform JAR: {ANDROID_JAR}")
    print(f"  D8: {D8}")
    print(f"  Javac: {JAVAC}")
    print(f"  NDK: {NDK_DIR}")
    print(f"  Strip: {STRIP}")
    require_deliberate_debuggable()
    prepare_signing_key()

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
        "--manifest", os.path.join(ROOT, "platform", "mobile", "AndroidManifest.xml"),
        "-R", res_zip,
        "--auto-add-overlay",
        "-I", ANDROID_JAR,
        "--java", gen_dir,
        "--min-sdk-version", "28",
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
    run([D8, "--output", dex_dir, "--lib", ANDROID_JAR, "--min-api", "28"] + class_files)

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
            for root, dirs, files in os.walk(assets_dir):
                dirs[:] = [d for d in dirs if d != "__pycache__"]
                for f in files:
                    if f.endswith(".pyc") or f == ".DS_Store":
                        continue
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
        "--ks-pass", "pass:" + KEYSTORE_PASS,
        "--key-pass", "pass:" + KEY_PASS,
        "--out", final_apk,
        aligned_apk
    ])
    verify_signed_apk(final_apk)

    # Cleanup temp build files
    for temp in [base_apk, aligned_apk, res_zip]:
        if os.path.exists(temp):
            os.remove(temp)
    for d in [gen_dir, classes_dir, dex_dir, apk_dir]:
        shutil.rmtree(d, ignore_errors=True)

    print(f"\n[SUCCESS] Built unified package: {final_apk} ({os.path.getsize(final_apk)} bytes)")

if __name__ == "__main__":
    main()
