#!/usr/bin/env python3
"""Release gate: inspect the APK that is about to be published.

The packager already verifies what it just built.  This runs on the artifact the
release job actually uploads, so a wrong file, a lost signer or a stripped
library fails the release instead of reaching players.

Checks
  1. exactly one APK is present;
  2. `apksigner verify --print-certs` succeeds and the signer is the pinned
     project certificate (an APK signed with anything else cannot update an
     installed copy - issues #35/#36);
  3. every required arm64-v8a entry is inside the archive, exactly once:
     libgame/libmain/libreserve/libinstaller, wrap.sh and the shader manifest;
  4. no other ABI or unexpected native payload slipped in;
  5. the debuggable flag matches policy: `android:debuggable="true"` is allowed
     only for the sideload tester channel (GALAXY_ALLOW_DEBUGGABLE=1).

Usage: python tools/verify_release_apk.py [apk ...]
       defaults to out-mobile/*.apk
Exit status: 0 publishable, 1 refused.
"""

import glob
import os
import re
import subprocess
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.apk_env import APKSIGNER, AAPT2, EXPECTED_SIGNER, ROOT, normalize_fingerprint

ABI = "lib/arm64-v8a"
REQUIRED_ENTRIES = [
    f"{ABI}/libgame.so",
    f"{ABI}/libmain.so",
    f"{ABI}/libreserve.so",
    f"{ABI}/libinstaller.so",
    f"{ABI}/wrap.sh",
    "assets/galaxy_shader_manifest.bin",
]
# The engine ships one ABI; a second one means an unintended build flavour.
ALLOWED_ABIS = {"arm64-v8a"}


def check_entries(names):
    """Structural problems in an APK's archive listing."""
    problems = []
    for entry in REQUIRED_ENTRIES:
        count = names.count(entry)
        if count == 0:
            problems.append(f"missing {entry}")
        elif count > 1:
            problems.append(f"{entry} appears {count} times")
    abis = {n.split("/")[1] for n in names if n.startswith("lib/") and n.count("/") >= 2}
    for abi in sorted(abis - ALLOWED_ABIS):
        problems.append(f"unexpected ABI directory lib/{abi}")
    return problems


def parse_signer(output):
    # apksigner prints a bare hex digest; keytool style output separates the
    # bytes with colons.  Normalisation strips anything that is not hex.
    match = re.search(r"certificate SHA-256 digest:\s*([0-9A-Fa-f:]+)", output)
    return normalize_fingerprint(match.group(1)) if match else ""


def parse_debuggable(xmltree):
    """Reads android:debuggable out of `aapt2 dump xmltree` output.

    Returns True/False, or None when the attribute is absent (the platform
    default, which is not debuggable)."""
    for line in xmltree.splitlines():
        if "android:debuggable" in line:
            value = line.rsplit("=", 1)[-1].strip().strip('"').lower()
            return value in ("true", "0xffffffff", "1")
    return None


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def inspect(apk, allow_debuggable):
    problems = []
    with zipfile.ZipFile(apk) as zf:
        problems += check_entries(zf.namelist())

    signer = run([APKSIGNER, "verify", "--print-certs", apk])
    if signer.returncode != 0:
        problems.append(f"apksigner verify failed: {(signer.stdout + signer.stderr).strip()}")
    else:
        fingerprint = parse_signer(signer.stdout + signer.stderr)
        if not fingerprint:
            problems.append("no signer certificate found")
        elif fingerprint != normalize_fingerprint(EXPECTED_SIGNER):
            problems.append(f"signed with {fingerprint}, expected {EXPECTED_SIGNER}")

    tree = run([AAPT2, "dump", "xmltree", "--file", "AndroidManifest.xml", apk])
    if tree.returncode != 0:
        problems.append(f"aapt2 could not read AndroidManifest.xml: {tree.stderr.strip()}")
    else:
        debuggable = parse_debuggable(tree.stdout)
        if debuggable and not allow_debuggable:
            problems.append('manifest is android:debuggable="true" but this is not the '
                            "tester channel (set GALAXY_ALLOW_DEBUGGABLE=1 to allow it)")
        if debuggable:
            print("[GATE] debuggable tester APK: sideload only, never a store upload")
        # Record the declared requirements: a required hardware feature decides
        # whether the app can even be installed on a given device.
        badging = run([AAPT2, "dump", "badging", apk])
        for line in badging.stdout.splitlines():
            if line.startswith(("package:", "uses-feature:", "native-code:")):
                print("[GATE] " + line.strip())
    return problems


def main(argv):
    missing = [name for name, tool in (("apksigner", APKSIGNER), ("aapt2", AAPT2))
               if not tool or not os.path.exists(tool)]
    if missing:
        print(f"[GATE] REFUSING TO RELEASE: {', '.join(missing)} not found in the Android "
              f"build-tools (set ANDROID_HOME or ANDROID_SDK_ROOT)")
        return 1

    apks = argv or sorted(glob.glob(os.path.join(ROOT, "out-mobile", "*.apk")))
    if not apks:
        print("[GATE] REFUSING TO RELEASE: no APK found")
        return 1
    if len(apks) != 1:
        print(f"[GATE] REFUSING TO RELEASE: expected exactly one APK, found {len(apks)}: {apks}")
        return 1

    allow_debuggable = os.environ.get("GALAXY_ALLOW_DEBUGGABLE") == "1"
    problems = inspect(apks[0], allow_debuggable)
    if problems:
        print(f"[GATE] REFUSING TO RELEASE {apks[0]}:")
        for problem in problems:
            print(f"  - {problem}")
        return 1
    print(f"[GATE] {apks[0]} is publishable ({os.path.getsize(apks[0])} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
