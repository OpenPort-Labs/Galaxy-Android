#!/usr/bin/env python3
"""
Galaxy-Android Offline Asset Material Shader Manifest Cooker
Extracts real material definitions (MAT3) from J3D models (BMD/BDL) and RARC archives,
generates canonical ShaderUid and PipelineKey values, deduplicates them, and writes
a versioned binary galaxy_shader_manifest.bin manifest consumed by the native Android engine.
"""

import os
import sys
import struct
import argparse

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS_DIR = os.path.dirname(HERE)
ROOT_DIR = os.path.dirname(TOOLS_DIR)
sys.path.insert(0, HERE)
sys.path.insert(0, TOOLS_DIR)
sys.path.insert(0, ROOT_DIR)

try:
    from tools.rarc import yaz0_decompress, rarc_list
except ImportError:
    try:
        from rarc import yaz0_decompress, rarc_list
    except ImportError:
        yaz0_decompress = None
        rarc_list = None

MAGIC_PTSU = 0x55535450
SCHEMA_VERSION = 2
BUILD_VERSION = 20261019

def fnv1a32(data: bytes, seed: int = 2166136261) -> int:
    h = seed
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h

try:
    from tools.cook.j3d_mat3_parser import (
        parse_j3d_mat3,
        encode_pipeline_key,
        CANONICAL_DEPTH_NEVER,
        CANONICAL_DEPTH_LESS,
        CANONICAL_DEPTH_EQUAL,
        CANONICAL_DEPTH_LEQUAL,
        CANONICAL_DEPTH_GREATER,
        CANONICAL_DEPTH_NEQUAL,
        CANONICAL_DEPTH_GEQUAL,
        CANONICAL_DEPTH_ALWAYS,
        CANONICAL_BLEND_ZERO,
        CANONICAL_BLEND_ONE,
        CANONICAL_BLEND_SRCCLR,
        CANONICAL_BLEND_INVSRCCLR,
        CANONICAL_BLEND_SRCATTR,
        CANONICAL_BLEND_INVSRCATTR,
        CANONICAL_BLEND_DSTATTR,
        CANONICAL_BLEND_INVDSTATTR,
        CANONICAL_BLEND_DSTCLR,
        CANONICAL_BLEND_INVDSTCLR,
        CANONICAL_BLEND_OP_ADD,
        CANONICAL_BLEND_OP_SUBTRACT,
        CANONICAL_BLEND_OP_REVERSE_SUBTRACT,
        CANONICAL_CULL_NONE,
        CANONICAL_CULL_FRONT,
        CANONICAL_CULL_BACK,
        CANONICAL_CULL_FRONT_AND_BACK,
        CANONICAL_TOPO_POINT_LIST,
        CANONICAL_TOPO_LINE_LIST,
        CANONICAL_TOPO_LINE_STRIP,
        CANONICAL_TOPO_TRIANGLE_LIST,
        CANONICAL_TOPO_TRIANGLE_STRIP,
        CANONICAL_TOPO_TRIANGLE_FAN,
    )
except ImportError:
    from j3d_mat3_parser import (
        parse_j3d_mat3,
        encode_pipeline_key,
        CANONICAL_DEPTH_NEVER,
        CANONICAL_DEPTH_LESS,
        CANONICAL_DEPTH_EQUAL,
        CANONICAL_DEPTH_LEQUAL,
        CANONICAL_DEPTH_GREATER,
        CANONICAL_DEPTH_NEQUAL,
        CANONICAL_DEPTH_GEQUAL,
        CANONICAL_DEPTH_ALWAYS,
        CANONICAL_BLEND_ZERO,
        CANONICAL_BLEND_ONE,
        CANONICAL_BLEND_SRCCLR,
        CANONICAL_BLEND_INVSRCCLR,
        CANONICAL_BLEND_SRCATTR,
        CANONICAL_BLEND_INVSRCATTR,
        CANONICAL_BLEND_DSTATTR,
        CANONICAL_BLEND_INVDSTATTR,
        CANONICAL_BLEND_DSTCLR,
        CANONICAL_BLEND_INVDSTCLR,
        CANONICAL_BLEND_OP_ADD,
        CANONICAL_BLEND_OP_SUBTRACT,
        CANONICAL_BLEND_OP_REVERSE_SUBTRACT,
        CANONICAL_CULL_NONE,
        CANONICAL_CULL_FRONT,
        CANONICAL_CULL_BACK,
        CANONICAL_CULL_FRONT_AND_BACK,
        CANONICAL_TOPO_POINT_LIST,
        CANONICAL_TOPO_LINE_LIST,
        CANONICAL_TOPO_LINE_STRIP,
        CANONICAL_TOPO_TRIANGLE_LIST,
        CANONICAL_TOPO_TRIANGLE_STRIP,
        CANONICAL_TOPO_TRIANGLE_FAN,
    )

def scan_directory_for_models(root_dir: str):
    """Scans root_dir recursively for .bdl, .bmd, and extracts .arc archives safely."""
    discovered = []
    seen_keys = set()
    total_models = 0
    models_parsed = 0
    total_archives = 0
    total_mats = 0
    total_mat3_blocks = 0
    skipped_inputs = 0
    total_gaps = []

    for dirpath, _, filenames in os.walk(root_dir):
        for fn in filenames:
            ext = os.path.splitext(fn)[1].lower()
            fp = os.path.join(dirpath, fn)

            if ext in ('.bdl', '.bmd'):
                total_models += 1
                try:
                    with open(fp, 'rb') as f:
                        b = f.read()
                    mats, gaps, mat3_cnt = parse_j3d_mat3(b, fn)
                    models_parsed += 1
                    total_mat3_blocks += mat3_cnt
                    total_mats += len(mats)
                    total_gaps.extend(gaps)
                    for uid, key, src in mats:
                        if key not in seen_keys:
                            seen_keys.add(key)
                            discovered.append((uid, key, src))
                except Exception as e:
                    total_gaps.append(f"Failed reading model {fp}: {e}")

            elif ext == '.arc' and rarc_list is not None:
                total_archives += 1
                try:
                    with open(fp, 'rb') as f:
                        data = f.read()
                    if yaz0_decompress is not None and data[:4] == b'Yaz0':
                        data = yaz0_decompress(data)
                    arc_entries = rarc_list(data)
                    for inner_path, inner_bytes in arc_entries:
                        inner_ext = os.path.splitext(inner_path)[1].lower()
                        if inner_ext in ('.bdl', '.bmd'):
                            total_models += 1
                            mats, gaps, mat3_cnt = parse_j3d_mat3(inner_bytes, os.path.basename(inner_path), fn)
                            models_parsed += 1
                            total_mat3_blocks += mat3_cnt
                            total_mats += len(mats)
                            total_gaps.extend(gaps)
                            for uid, key, src in mats:
                                if key not in seen_keys:
                                    seen_keys.add(key)
                                    discovered.append((uid, key, src))
                except Exception as e:
                    skipped_inputs += 1
                    total_gaps.append(f"Failed extracting archive {fp}: {e}")

    report = {
        'archives_visited': total_archives,
        'models_discovered': total_models,
        'models_parsed': models_parsed,
        'mat3_blocks': total_mat3_blocks,
        'material_records': total_mats,
        'unique_keys': len(discovered),
        'unique_uids': len({tuple(u) for u, _, _ in discovered}),
        'skipped_inputs': skipped_inputs,
        'parser_errors': len(total_gaps),
        'gaps': total_gaps[:10]
    }
    return discovered, report

def write_manifest(materials, out_path: str):
    """Writes the versioned galaxy_shader_manifest.bin according to C++ schema contract."""
    header = struct.pack('<IIIII', MAGIC_PTSU, SCHEMA_VERSION, BUILD_VERSION, len(materials), 0)
    hdr_crc = fnv1a32(header[:16])
    header = struct.pack('<IIIII', MAGIC_PTSU, SCHEMA_VERSION, BUILD_VERSION, len(materials), hdr_crc)

    records = bytearray()
    for uid_words, key, src in materials:
        uid_bytes = struct.pack('<90I', *uid_words)
        key_bytes = struct.pack('<5I', *key)
        flags_byte = 0  # not runtime
        src_bytes = src.encode('utf-8')[:512]
        str_len = len(src_bytes)

        rec_head = struct.pack('<B', flags_byte) + struct.pack('<H', str_len) + src_bytes
        rec_data = uid_bytes + key_bytes + rec_head
        rec_crc = fnv1a32(rec_data)

        records.extend(uid_bytes)
        records.extend(key_bytes)
        records.extend(struct.pack('<B', flags_byte))
        records.extend(struct.pack('<H', str_len))
        records.extend(src_bytes)
        records.extend(struct.pack('<I', rec_crc))

    out_dir = os.path.dirname(os.path.abspath(out_path))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    tmp_path = out_path + ".tmp"
    with open(tmp_path, 'wb') as f:
        f.write(header)
        f.write(records)
    if os.path.exists(out_path):
        os.remove(out_path)
    os.rename(tmp_path, out_path)
    print(f"Successfully generated shader manifest: {out_path} ({len(materials)} materials, {len(header) + len(records)} bytes)")

def main():
    parser = argparse.ArgumentParser(description="Cook Super Mario Galaxy asset materials into shader manifest")
    parser.add_argument("--assets-dir", default=".", help="Root directory containing extracted game assets or models")
    parser.add_argument("--output", default="galaxy_shader_manifest.bin", help="Output binary manifest path")
    args = parser.parse_args()

    materials, report = scan_directory_for_models(args.assets_dir)
    print(f"=== Shader Manifest Coverage Report ===")
    print(f"  Archives Visited:       {report['archives_visited']}")
    print(f"  Models Discovered:      {report['models_discovered']}")
    print(f"  Models Parsed:          {report['models_parsed']}")
    print(f"  MAT3 Blocks:            {report['mat3_blocks']}")
    print(f"  Material Records:       {report['material_records']}")
    print(f"  Unique Shader UIDs:     {report['unique_uids']}")
    print(f"  Unique Pipeline Keys:   {report['unique_keys']}")
    print(f"  Skipped Inputs:         {report['skipped_inputs']}")
    print(f"  Parser Errors:          {report['parser_errors']}")
    if report['gaps']:
        print(f"Sample gaps: {report['gaps']}")

    if not materials and os.path.exists("mario.bdl"):
        with open("mario.bdl", 'rb') as f:
            b = f.read()
        mats, _ = parse_j3d_mat3(b, "mario.bdl")
        for u, k, s in mats:
            materials.append((u, k, s))
        print(f"Cooked mario.bdl: discovered {len(mats)} materials")

    write_manifest(materials, args.output)

if __name__ == '__main__':
    main()

