#!/usr/bin/env python3
"""
Repository-Level Lines-of-Code (LOC) Compliance Checker.
Enforces that every maintained code file does not exceed 300 lines.
"""

import argparse
import os
import sys

MAINTAINED_EXTENSIONS = (
    '.c', '.cpp', '.cc', '.cxx',
    '.h', '.hpp', '.hxx', '.inl',
    '.java', '.kt',
    '.py',
    '.sh', '.bat', '.ps1',
    '.cmake',
)

EXCLUDED_DIR_NAMES = {
    '.git', '.gradle', '.idea', '.vscode',
    'decomp',           # Decompiled Wii GameCube source (500k+ lines)
    'third_party',      # Third-party dependencies
    'THPConv',          # Nintendo THP converter & Revolution SDK headers
    'vendor',           # Embedded Python 3.10 and nodtool vendored binaries
    'extracted',        # Extracted disc assets
    'shield_extracted', # Extracted shield assets
    'orig_wii_extracted',
    'roms',             # User ROM files
    'cooked',           # Cooked cache
}

def is_excluded(path: str) -> bool:
    norm = os.path.normpath(os.path.abspath(path))
    parts = norm.split(os.sep)
    for part in parts:
        if part in EXCLUDED_DIR_NAMES:
            return True
        if part.startswith('build') or part.startswith('out'):
            return True
    return False

def is_maintained_file(fn: str) -> bool:
    fn_lower = fn.lower()
    if fn_lower == 'cmakelists.txt':
        return True
    ext = os.path.splitext(fn_lower)[1]
    return ext in MAINTAINED_EXTENSIONS

def count_file_lines(filepath: str) -> int:
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as fp:
        return sum(1 for _ in fp)

def check_loc(root_dir: str, max_lines: int = 300):
    maintained_files = []
    violations = []

    for dirpath, dirs, filenames in os.walk(root_dir):
        # Prune excluded directories early
        dirs[:] = [d for d in dirs if not is_excluded(os.path.join(dirpath, d))]

        for fn in sorted(filenames):
            if is_maintained_file(fn):
                full_path = os.path.join(dirpath, fn)
                if not is_excluded(full_path):
                    maintained_files.append(full_path)

    maintained_files.sort()

    for path in maintained_files:
        lines = count_file_lines(path)
        if lines > max_lines:
            violations.append((lines, os.path.relpath(path, root_dir)))

    return maintained_files, violations

def main():
    parser = argparse.ArgumentParser(description="Check strict <= 300 LOC limit on maintained files")
    parser.add_argument("--root", default=".", help="Root directory to check")
    parser.add_argument("--max-lines", type=int, default=300, help="Maximum allowed LOC (default: 300)")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    files, violations = check_loc(root, args.max_lines)

    print(f"=== Maintained Code LOC Audit ===")
    print(f"Root: {root}")
    print(f"Total maintained code files scanned: {len(files)}")
    print(f"Maximum line limit: {args.max_lines}")

    if violations:
        print(f"\n[VIOLATIONS DETECTED] {len(violations)} file(s) exceed {args.max_lines} lines:")
        for lines, rel in violations:
            print(f"  {lines:4d} lines: {rel}")
        sys.exit(1)
    else:
        print(f"\n[SUCCESS] All {len(files)} maintained code files comply with <= {args.max_lines} lines!")
        sys.exit(0)

if __name__ == "__main__":
    main()
