#!/usr/bin/env python3
"""Galaxy-Android offline shader-manifest cooker.

Parses J3D (BMD/BDL) MAT3 chunks, embeds them in flat model files or RARC
archives, derives the canonical ShaderUid + PipelineKey for every material and
writes the versioned ``galaxy_shader_manifest.bin`` consumed by the Android
engine.

Canonical input root
--------------------
``cooked/`` -- the asset tree produced by the project's cook step from the
user's own disc (see README).  It is intentionally *not* tracked by Git and is
never redistributed; only the derived manifest is committed.  Running this
tool without that tree is an error rather than a silent empty manifest.

The scan ignores every other directory, is independent of filesystem
enumeration order and de-duplicates identical pipeline keys, so the same input
tree always produces byte-identical output.  ``tools/tests/test_manifest_schema.py``
verifies that property against the committed artifact when the tree is present.
"""

import argparse
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS_DIR = os.path.dirname(HERE)
ROOT_DIR = os.path.dirname(TOOLS_DIR)
sys.path.insert(0, HERE)
sys.path.insert(0, TOOLS_DIR)
sys.path.insert(0, ROOT_DIR)

try:
    from manifest_schema import (
        MAGIC_PTSU, SCHEMA_VERSION, BUILD_VERSION, UID_WORDS, KEY_WORDS,
        MAX_SOURCE_BYTES, HEADER_SIZE, HEADER_CRC_BYTES,
    )
except ImportError:  # imported as tools.cook.generate_shader_manifest
    from tools.cook.manifest_schema import (
        MAGIC_PTSU, SCHEMA_VERSION, BUILD_VERSION, UID_WORDS, KEY_WORDS,
        MAX_SOURCE_BYTES, HEADER_SIZE, HEADER_CRC_BYTES,
    )

try:
    from tools.rarc import yaz0_decompress, rarc_list
except ImportError:
    try:
        from rarc import yaz0_decompress, rarc_list
    except ImportError:  # reported as a diagnostic, not a silent skip
        yaz0_decompress = None
        rarc_list = None

try:
    from tools.cook.j3d_mat3_parser import parse_j3d_mat3
except ImportError:
    from j3d_mat3_parser import parse_j3d_mat3

MODEL_EXTENSIONS = ('.bdl', '.bmd')


def fnv1a32(data: bytes, seed: int = 2166136261) -> int:
    h = seed
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def sort_key(entry):
    uid_words, key, src = entry
    return (tuple(uid_words), tuple(key), src)


def scan_directory_for_models(root_dir: str):
    """Parses every model under root_dir in a deterministic, recursive order."""
    if not os.path.isdir(root_dir):
        raise FileNotFoundError(f"asset root not found: {root_dir}")

    discovered = []
    seen_keys = set()
    report = {
        'root': os.path.abspath(root_dir),
        'archives_visited': 0,
        'models_discovered': 0,
        'models_parsed': 0,
        'mat3_blocks': 0,
        'material_records': 0,
        'skipped_inputs': 0,
        'gaps': [],
    }

    def absorb(mats, gaps, mat3_cnt):
        report['models_parsed'] += 1
        report['mat3_blocks'] += mat3_cnt
        report['material_records'] += len(mats)
        report['gaps'].extend(gaps)
        for uid, key, src in mats:
            if key not in seen_keys:
                seen_keys.add(key)
                discovered.append((uid, key, src))

    for dirpath, dirnames, filenames in os.walk(root_dir):
        dirnames.sort()
        for fn in sorted(filenames):
            ext = os.path.splitext(fn)[1].lower()
            fp = os.path.join(dirpath, fn)

            if ext in MODEL_EXTENSIONS:
                report['models_discovered'] += 1
                try:
                    with open(fp, 'rb') as f:
                        b = f.read()
                except Exception as e:
                    report['skipped_inputs'] += 1
                    report['gaps'].append(f"Failed reading model {fp}: {e}")
                    continue
                absorb(*parse_j3d_mat3(b, fn))
            elif ext == '.arc' and rarc_list is not None:
                report['archives_visited'] += 1
                try:
                    with open(fp, 'rb') as f:
                        data = f.read()
                    if yaz0_decompress is not None and data[:4] == b'Yaz0':
                        data = yaz0_decompress(data)
                    # Sorting makes record order independent of archive layout.
                    entries = sorted(rarc_list(data), key=lambda e: e[0])
                    for inner_path, inner_bytes in entries:
                        if os.path.splitext(inner_path)[1].lower() not in MODEL_EXTENSIONS:
                            continue
                        report['models_discovered'] += 1
                        absorb(*parse_j3d_mat3(inner_bytes, os.path.basename(inner_path), fn))
                except Exception as e:
                    report['skipped_inputs'] += 1
                    report['gaps'].append(f"Failed extracting archive {fp}: {e}")

    # Deterministic order regardless of directory traversal details.
    discovered.sort(key=sort_key)
    return discovered, report


def encode_record(uid_words, key, src: str) -> bytes:
    if len(uid_words) != UID_WORDS:
        raise ValueError(f"ShaderUid must have {UID_WORDS} words, got {len(uid_words)}")
    if len(key) != KEY_WORDS:
        raise ValueError(f"PipelineKey must have {KEY_WORDS} words, got {len(key)}")
    src_bytes = src.encode('utf-8')[:MAX_SOURCE_BYTES]
    payload = (
        struct.pack('<%dI' % UID_WORDS, *uid_words) +
        struct.pack('<%dI' % KEY_WORDS, *key) +
        struct.pack('<B', 0) +
        struct.pack('<H', len(src_bytes)) +
        src_bytes
    )
    return payload + struct.pack('<I', fnv1a32(payload))


def build_manifest_bytes(materials) -> bytes:
    """Serializes materials deterministically; input order does not matter."""
    ordered = sorted(materials, key=sort_key)
    count = len(ordered)
    header = struct.pack('<IIII', MAGIC_PTSU, SCHEMA_VERSION, BUILD_VERSION, count)
    header += struct.pack('<I', fnv1a32(header))
    body = b''.join(encode_record(uid, key, src) for uid, key, src in ordered)
    return header + body


def write_manifest(materials, out_path: str) -> str:
    data = build_manifest_bytes(materials)
    out_dir = os.path.dirname(os.path.abspath(out_path))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    tmp_path = out_path + ".tmp"
    with open(tmp_path, 'wb') as f:
        f.write(data)
    os.replace(tmp_path, out_path)
    digest = hashlib.sha256(data).hexdigest()
    print(f"Successfully generated shader manifest: {out_path} "
          f"({len(materials)} materials, {len(data)} bytes, schema {SCHEMA_VERSION}, "
          f"build {BUILD_VERSION}, sha256 {digest})")
    return digest


def main():
    parser = argparse.ArgumentParser(description="Cook Galaxy asset materials into the shader manifest")
    parser.add_argument("--assets-dir", default="cooked",
                        help="Canonical cooked asset root (default: cooked)")
    parser.add_argument("--output", default="assets/galaxy_shader_manifest.bin",
                        help="Output manifest path (default: assets/galaxy_shader_manifest.bin)")
    args = parser.parse_args()

    if rarc_list is None:
        raise SystemExit(f"ERROR: tools/rarc.py could not be imported; cannot scan '{args.assets_dir}'.")
    if not os.path.isdir(args.assets_dir):
        raise SystemExit(
            f"ERROR: asset root '{args.assets_dir}' does not exist.\n"
            "Cook the game assets first (see README) or pass --assets-dir <root>.")

    materials, report = scan_directory_for_models(args.assets_dir)
    print("=== Shader Manifest Coverage Report ===")
    print(f"  Asset root:             {report['root']}")
    print(f"  Archives Visited:       {report['archives_visited']}")
    print(f"  Models Discovered:      {report['models_discovered']}")
    print(f"  Models Parsed:          {report['models_parsed']}")
    print(f"  MAT3 Blocks:            {report['mat3_blocks']}")
    print(f"  Material Records:       {report['material_records']}")
    print(f"  Unique Pipeline Keys:   {len(materials)}")
    print(f"  Unique Shader UIDs:     {len({tuple(u) for u, _, _ in materials})}")
    print(f"  Skipped Inputs:         {report['skipped_inputs']}")
    print(f"  Parser Errors:          {len(report['gaps'])}")
    if report['gaps']:
        print(f"  Sample gaps:            {report['gaps'][:5]}")

    if not materials:
        raise SystemExit(f"ERROR: no materials found under '{args.assets_dir}'; refusing to write an empty manifest.")

    write_manifest(materials, args.output)


if __name__ == '__main__':
    main()
