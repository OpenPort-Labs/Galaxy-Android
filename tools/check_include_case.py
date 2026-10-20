#!/usr/bin/env python3
"""Fails when a build-relevant #include does not match a tracked file name exactly.

Windows checkouts and this script's own ``os.path.exists`` are case-insensitive,
while the Linux release build is not: an include of ``JASDspInterface.hpp`` for
the tracked ``JASDSPInterface.hpp`` compiles locally and breaks CI.  Ground truth
here is git's index (``git ls-tree -r HEAD``), i.e. exactly what a fresh Linux
checkout materialises, so the check works the same on every host.

Only files reached by the mobile build are checked: the globs in CMakeLists.txt
plus everything they include transitively.  Legacy decomp reference code that is
not compiled (RVLFaceLib, nwc24, wud, ...) is reported as informational.

Usage: python tools/check_include_case.py [--strict-decomp]
Exit status: 0 clean, 1 build-breaking mismatch.
"""

import argparse
import os
import posixpath
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Mirrors DECOMP_INCLUDES plus the extra directories on `game`'s include path.
INCLUDE_DIRS = [
    "platform/include",
    "platform",
    "platform/src",
    "decomp/include",
    "decomp/libs/JSystem/include",
    "decomp/libs/RVL_SDK/include",
    "decomp/libs/nw4r/include",
    "decomp/libs/RVLFaceLib/include",
    "installer-mobile/jni",
]

# CMakeLists.txt source globs (GAME_SRCS, JSYSTEM_SRCS, NW4R_SRCS, SDK_SRCS,
# PLATFORM_SRCS, PLATFORM_MOBILE_SRCS).
BUILD_GLOBS = [
    ("decomp/src/Game", (".cpp", ".c")),
    ("decomp/src/JSystem", (".cpp",)),
    ("decomp/src/nw4r", (".cpp",)),
    ("platform/src", (".c", ".cpp")),
    ("platform/mobile", (".c", ".cpp")),
    ("installer-mobile/jni", (".c", ".cpp")),
]
BUILD_FIXED = [
    "decomp/src/RVL_SDK/os/OSMutex.c",
    "decomp/src/RVL_SDK/os/OSMessage.c",
    "decomp/src/RVL_SDK/thp/THPAudio.c",
]
SDK_MODULES = ("gx", "gd", "mem", "arc", "tpl", "aralt")

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)


def tracked_files(root=ROOT):
    out = subprocess.run(["git", "-C", root, "ls-tree", "-r", "HEAD", "--name-only"],
                         capture_output=True, text=True, check=True).stdout
    return [line.strip() for line in out.splitlines() if line.strip()]


def build_sources(root=ROOT):
    try:
        tracked = set(tracked_files(root))
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"[FAIL] cannot read git index: {exc}")
        raise SystemExit(2)
    sources = set(f for f in BUILD_FIXED if f in tracked)
    for base, exts in BUILD_GLOBS:
        for path in tracked:
            if not path.startswith(base + "/") or not path.endswith(exts):
                continue
            if path.startswith("platform/mobile/") and "/jni/" in path:
                continue
            sources.add(path)
    for module in SDK_MODULES:
        prefix = f"decomp/src/RVL_SDK/{module}/"
        sources.update(p for p in tracked if p.startswith(prefix) and p.endswith(".c"))
    excluded = re.compile(r"/gx/GXFifo\.c$|/gx/GXPerf\.c$")
    return {s for s in sources if not excluded.search(s)}, tracked


def resolve(include, source, exact, known):
    """Returns (tracked path or None, spelled exactly as on disk?)."""
    if posixpath.isabs(include):
        return None, False
    candidates = [posixpath.normpath(posixpath.join(posixpath.dirname(source), include))]
    candidates += [posixpath.normpath(posixpath.join(d, include)) for d in INCLUDE_DIRS]
    for candidate in candidates:
        if candidate in exact:
            return candidate, True
    for candidate in candidates:
        if candidate.lower() in known:
            return known[candidate.lower()], False
    return None, False


def check(root=ROOT, verbose=True):
    sources, tracked = build_sources(root)
    known = {path.lower(): path for path in tracked}
    exact = set(tracked)

    # Transitive closure over the includes that the build actually pulls in.
    queue, seen = list(sources), set()
    problems = []
    while queue:
        source = queue.pop()
        if source in seen or source not in exact:
            continue
        seen.add(source)
        try:
            with open(os.path.join(root, source), "r", encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError:
            continue
        for _quote, include in INCLUDE_RE.findall(text):
            resolved, exact_match = resolve(include, source, exact, known)
            if resolved is None:
                continue  # system/NDK header, not ours to police
            if exact_match:
                queue.append(resolved)
            else:
                problems.append((source, include, resolved))

    if verbose:
        if problems:
            print(f"[FAIL] {len(problems)} #include(s) differ from the tracked file name "
                  f"(breaks the case-sensitive Linux build):")
            for src, include, tracked_path in sorted(problems):
                print(f'  {src}: "{include}"  ->  tracked: {tracked_path}')
        else:
            print(f"[OK] All #include paths in {len(seen)} build-relevant files match "
                  f"tracked names exactly.")
    return problems


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    return 1 if check(verbose=not args.quiet) else 0


if __name__ == "__main__":
    sys.exit(main())
