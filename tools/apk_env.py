import base64
import glob
import os
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SDK = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
if not SDK or not os.path.isdir(SDK):
    if os.path.isdir(r"C:\Android\Sdk"):
        SDK = r"C:\Android\Sdk"
    else:
        SDK = os.path.expandvars(r"%LOCALAPPDATA%\Android\Sdk")

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

ANDROID_JAR = ""
platforms_dir = os.path.join(SDK, "platforms") if SDK else ""
if os.path.isdir(platforms_dir):
    candidates = sorted([p for p in os.listdir(platforms_dir) if os.path.exists(os.path.join(platforms_dir, p, "android.jar"))], reverse=True)
    if candidates:
        ANDROID_JAR = os.path.join(platforms_dir, candidates[0], "android.jar")

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

JAVAC = shutil.which("javac")
if not JAVAC:
    for candidate in [r"C:\Program Files\Java\jdk-17\bin\javac.exe", r"C:\Program Files\Java\latest\bin\javac.exe"]:
        if os.path.exists(candidate):
            JAVAC = candidate
            break
if not JAVAC:
    JAVAC = "javac"

def resolve_keystore_pass(channel="tester"):
    val = os.environ.get("GALAXY_KEYSTORE_PASS")
    if val:
        return val
    if channel == "production":
        raise SystemExit("REFUSING TO PACKAGE [production]: GALAXY_KEYSTORE_PASS environment variable is required.")
    return "android"

KEYSTORE_PASS = os.environ.get("GALAXY_KEYSTORE_PASS", "")
KEY_ALIAS = os.environ.get("GALAXY_KEY_ALIAS", "androiddebugkey")
KEY_PASS = os.environ.get("GALAXY_KEY_PASS", "")
KEYTOOL = shutil.which("keytool") or "keytool"

# Published in the repository history together with its password: anyone can
# reproduce this certificate, so it must never sign another artifact.
COMPROMISED_KEY_SHA256 = "1883cd600aacd5628513e23ce646f4c8ed19c040a14575fbc922d33f6686feff"

def normalize_fingerprint(value):
    return re.sub(r"[^0-9a-f]", "", value.lower())

EXPECTED_SIGNER = normalize_fingerprint(os.environ.get("GALAXY_EXPECTED_SIGNER_SHA256", ""))
LOCAL_KEYSTORE = os.path.join(ROOT, "out", "release.keystore")
KEYSTORE_CANDIDATES = [LOCAL_KEYSTORE, os.path.join(ROOT, "out", "debug.keystore"),
                       os.path.join(ROOT, "platform", "mobile", "release.keystore")]
DEV_KEYSTORE = os.path.join(ROOT, "out", "dev.keystore")
KEYSTORE = ""
ANDROID_NS = "{http://schemas.android.com/apk/res/android}"


def check_signer(fingerprint, channel="tester"):
    """Why this certificate must not sign an artifact, or None if it may."""
    fingerprint = normalize_fingerprint(fingerprint or "")
    if not fingerprint:
        return "could not determine the signer certificate"
    if fingerprint == COMPROMISED_KEY_SHA256:
        return ("certificate %s is the signing key published in the repository history; "
                "rotate the signing identity and never use it again" % fingerprint)
    expected = normalize_fingerprint(os.environ.get("GALAXY_EXPECTED_SIGNER_SHA256", ""))
    if expected and fingerprint != expected:
        return "certificate %s does not match the pinned signer %s" % (fingerprint, expected)
    if channel == "production" and not expected:
        return "production requires GALAXY_EXPECTED_SIGNER_SHA256 naming the rotated release key"
    return None

MANIFEST = os.path.join(ROOT, "platform", "mobile", "AndroidManifest.xml")

def manifest_is_debuggable(path=MANIFEST):
    application = ET.parse(path).getroot().find("application")
    return application is not None and application.get(ANDROID_NS + "debuggable") == "true"

def keystore_fingerprint(keystore, storepass=None):
    sp = storepass or resolve_keystore_pass(channel="tester")
    env = dict(os.environ)
    env["_GALAXY_KT_PASS"] = sp
    try:
        proc = subprocess.run([KEYTOOL, "-list", "-v", "-keystore", keystore,
                               "-storepass:env", "_GALAXY_KT_PASS"],
                              capture_output=True, text=True, env=env)
    except OSError:
        return None
    match = re.search(r"SHA-?256:\s*([0-9A-Fa-f:]+)", proc.stdout)
    return normalize_fingerprint(match.group(1)) if match else None

def prepare_signing_key(channel="tester"):
    global KEYSTORE
    storepass = resolve_keystore_pass(channel=channel)
    encoded = os.environ.get("GALAXY_KEYSTORE_B64")
    if encoded:
        try:
            data = base64.b64decode(encoded)
        except Exception as exc:
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
        KEYSTORE = LOCAL_KEYSTORE
    else:
        explicit = os.environ.get("GALAXY_KEYSTORE")
        for candidate in ([explicit] if explicit else []) + KEYSTORE_CANDIDATES:
            if candidate and os.path.exists(candidate):
                KEYSTORE = candidate
                break
        if not KEYSTORE:
            if channel == "production":
                raise SystemExit("REFUSING TO PACKAGE [production]: no signing keystore found.")
            if os.environ.get("GALAXY_ALLOW_NEW_KEY") == "1":
                env = dict(os.environ)
                env["_GALAXY_KT_PASS"] = storepass
                env["_GALAXY_KT_KEYPASS"] = os.environ.get("GALAXY_KEY_PASS", storepass)
                subprocess.run([KEYTOOL, "-genkeypair", "-keystore", DEV_KEYSTORE,
                                "-storepass:env", "_GALAXY_KT_PASS", "-keypass:env", "_GALAXY_KT_KEYPASS",
                                "-alias", KEY_ALIAS, "-keyalg", "RSA", "-keysize", "2048",
                                "-validity", "10000", "-dname", "CN=GalaxyAndroid Dev,O=Local,C=US"],
                               check=True, env=env)
                KEYSTORE = DEV_KEYSTORE
            else:
                raise SystemExit("REFUSING TO PACKAGE: no signing key found.")

    fingerprint = keystore_fingerprint(KEYSTORE, storepass=storepass)
    problem = check_signer(fingerprint, channel=channel)
    if problem:
        raise SystemExit(f"REFUSING TO PACKAGE {KEYSTORE}: {problem}")
    if channel != "production" and not os.environ.get("GALAXY_EXPECTED_SIGNER_SHA256"):
        print("[SIGNING] Tester certificate %s is not pinned; "
              "set GALAXY_EXPECTED_SIGNER_SHA256 before publishing." % fingerprint)
    return KEYSTORE

def verify_signed_apk(path, channel="tester"):
    proc = subprocess.run([APKSIGNER, "verify", "--print-certs", path], capture_output=True, text=True)
    text = proc.stdout + proc.stderr
    match = re.search(r"certificate SHA-256 digest:\s*([0-9A-Fa-f]+)", text)
    if proc.returncode != 0 or not match:
        raise SystemExit(f"REFUSING TO RELEASE: {path} did not verify:\n{text.strip()}")
    fingerprint = normalize_fingerprint(match.group(1))
    problem = check_signer(fingerprint, channel=channel)
    if problem:
        raise SystemExit(f"REFUSING TO RELEASE {path}: {problem}")
    return fingerprint
