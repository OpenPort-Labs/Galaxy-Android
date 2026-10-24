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
     libgame/libmain/libreserve/libinstaller, wrap.sh, classes.dex and the
     shader manifest;
  4. no other ABI or unexpected native payload slipped in, the required
     payloads are non-empty, the libraries are ELF64/AArch64 and wrap.sh is a
     LF-only shebang script;
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

from tools.apk_env import APKSIGNER, AAPT2, ROOT, check_signer, normalize_fingerprint

ABI = "lib/arm64-v8a"
NATIVE_ENTRIES = [f"{ABI}/libgame.so", f"{ABI}/libmain.so", f"{ABI}/libreserve.so", f"{ABI}/libinstaller.so"]
REQUIRED_ENTRIES = NATIVE_ENTRIES + [f"{ABI}/wrap.sh", "assets/galaxy_shader_manifest.bin", "classes.dex"]
# The engine ships one ABI; a second one means an unintended build flavour.
ALLOWED_ABIS = {"arm64-v8a"}
ELF_MACHINE_AARCH64 = 183


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


def check_contents(archive):
    """Payload sanity: an entry that exists but is empty or foreign cannot run."""
    problems = []
    names = set(archive.namelist())
    for entry in REQUIRED_ENTRIES:
        if entry in names and archive.getinfo(entry).file_size == 0:
            problems.append(f"{entry} is empty")

    for entry in NATIVE_ENTRIES:
        if entry not in names:
            continue
        with archive.open(entry) as handle:
            header = handle.read(20)
        if len(header) < 20 or header[:4] != b"\x7fELF" or header[4] != 2 or header[5] != 1:
            problems.append(f"{entry} is not a little-endian 64-bit ELF")
        elif int.from_bytes(header[18:20], "little") != ELF_MACHINE_AARCH64:
            problems.append(f"{entry} is not an AArch64 library")

    wrap = f"{ABI}/wrap.sh"
    if wrap in names:
        with archive.open(wrap) as handle:
            script = handle.read()
        if not script.startswith(b"#!"):
            problems.append(f"{wrap} is not a shebang script")
        if b"\r\n" in script:
            problems.append(f"{wrap} has CRLF line endings")

    if "classes.dex" in names:
        with archive.open("classes.dex") as handle:
            magic = handle.read(4)
        if magic != b"dex\n":
            problems.append("classes.dex is not a Dalvik executable")
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


def tester_debuggable_opt_in():
    return os.environ.get("GALAXY_ALLOW_DEBUGGABLE") == "1"


def inspect(apk, channel="production", allow_debuggable=False):
    problems = []
    with zipfile.ZipFile(apk) as zf:
        problems += check_entries(zf.namelist())
        problems += check_contents(zf)

    signer = run([APKSIGNER, "verify", "--print-certs", apk])
    if signer.returncode != 0:
        problems.append(f"apksigner verify failed: {(signer.stdout + signer.stderr).strip()}")
    else:
        fingerprint = parse_signer(signer.stdout + signer.stderr)
        if not fingerprint:
            problems.append("no signer certificate found")
        else:
            problem = check_signer(fingerprint, channel=channel)
            if problem:
                problems.append(problem)

    tree = run([AAPT2, "dump", "xmltree", "--file", "AndroidManifest.xml", apk])
    if tree.returncode != 0:
        problems.append(f"aapt2 could not read AndroidManifest.xml: {tree.stderr.strip()}")
    else:
        debuggable = parse_debuggable(tree.stdout)
        if channel == "production":
            if debuggable:
                problems.append(
                    'manifest is android:debuggable="true" which is forbidden in '
                    'production release channel (use --channel tester for sideload tester builds)'
                )
        else:
            if debuggable and not allow_debuggable:
                problems.append(
                    'manifest is android:debuggable="true" but tester opt-in is missing '
                    '(specify --channel tester or set GALAXY_ALLOW_DEBUGGABLE=1)'
                )
            if debuggable:
                print("[GATE] Verified sideload tester APK: sideload only, never a store upload")
        # Record the declared requirements: a required hardware feature decides
        # whether the app can even be installed on a given device.
        badging = run([AAPT2, "dump", "badging", apk])
        for line in badging.stdout.splitlines():
            if line.startswith(("package:", "uses-feature:", "native-code:")):
                print("[GATE] " + line.strip())
    return problems


def parse_args(argv):
    import argparse
    parser = argparse.ArgumentParser(description="Verify release APK against publication policy")
    parser.add_argument("--channel", choices=["production", "tester"],
                        default=os.environ.get("GALAXY_RELEASE_CHANNEL", "production").lower(),
                        help="Release channel: 'production' (default) or 'tester'")
    parser.add_argument("apks", nargs="*", help="APK file(s) to verify")
    return parser.parse_args(argv)


def main(argv):
    missing = [name for name, tool in (("apksigner", APKSIGNER), ("aapt2", AAPT2))
               if not tool or not os.path.exists(tool)]
    if missing:
        print(f"[GATE] REFUSING TO RELEASE: {', '.join(missing)} not found in the Android "
              f"build-tools (set ANDROID_HOME or ANDROID_SDK_ROOT)")
        return 1

    args = parse_args(argv)
    channel = args.channel
    apks = args.apks or sorted(glob.glob(os.path.join(ROOT, "out-mobile", "*.apk")))
    if not apks:
        print("[GATE] REFUSING TO RELEASE: no APK found")
        return 1
    if len(apks) != 1:
        print(f"[GATE] REFUSING TO RELEASE: expected exactly one APK, found {len(apks)}: {apks}")
        return 1

    allow_debuggable = channel == "tester" and tester_debuggable_opt_in()

    problems = inspect(apks[0], channel=channel, allow_debuggable=allow_debuggable)
    if problems:
        print(f"[GATE] REFUSING TO RELEASE {apks[0]} [channel: {channel}]:")
        for problem in problems:
            print(f"  - {problem}")
        return 1
    print(f"[GATE] {apks[0]} is publishable on channel '{channel}' ({os.path.getsize(apks[0])} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
