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
sys.path.insert(0, HERE)
sys.path.insert(0, TOOLS_DIR)

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

# Canonical Enums matching platform/src/gx/vk/vk_types.h
CANONICAL_DEPTH_NEVER = 0
CANONICAL_DEPTH_LESS = 1
CANONICAL_DEPTH_EQUAL = 2
CANONICAL_DEPTH_LEQUAL = 3
CANONICAL_DEPTH_GREATER = 4
CANONICAL_DEPTH_NEQUAL = 5
CANONICAL_DEPTH_GEQUAL = 6
CANONICAL_DEPTH_ALWAYS = 7

CANONICAL_BLEND_ZERO = 0
CANONICAL_BLEND_ONE = 1
CANONICAL_BLEND_SRCCLR = 2
CANONICAL_BLEND_INVSRCCLR = 3
CANONICAL_BLEND_SRCATTR = 4
CANONICAL_BLEND_INVSRCATTR = 5
CANONICAL_BLEND_DSTATTR = 6
CANONICAL_BLEND_INVDSTATTR = 7
CANONICAL_BLEND_DSTCLR = 8
CANONICAL_BLEND_INVDSTCLR = 9

CANONICAL_BLEND_OP_ADD = 0
CANONICAL_BLEND_OP_SUBTRACT = 1
CANONICAL_BLEND_OP_REVERSE_SUBTRACT = 2

CANONICAL_CULL_NONE = 0
CANONICAL_CULL_FRONT = 1
CANONICAL_CULL_BACK = 2
CANONICAL_CULL_FRONT_AND_BACK = 3

CANONICAL_TOPO_POINT_LIST = 0
CANONICAL_TOPO_LINE_LIST = 1
CANONICAL_TOPO_LINE_STRIP = 2
CANONICAL_TOPO_TRIANGLE_LIST = 3
CANONICAL_TOPO_TRIANGLE_STRIP = 4
CANONICAL_TOPO_TRIANGLE_FAN = 5

def encode_pipeline_key(uid_words, vtx_flags, depth_test, depth_write, depth_func,
                        blend_enable, src_color, dst_color, src_alpha, dst_alpha,
                        color_blend_op=0, alpha_blend_op=0, color_mask=15,
                        cull_mode=2, topology=3, front_face_cw=0, const_alpha=0):
    """Encodes canonical pipeline key matching platform/src/gx/vk/vk_types.h exactly."""
    flags = vtx_flags
    depth_state = (1 if depth_test else 0) | ((1 if (depth_test and depth_write) else 0) << 1) | ((depth_func & 0x0F) << 4)
    blend_state = ((1 if blend_enable else 0) |
                   ((src_color & 0x1F) << 1) |
                   ((dst_color & 0x1F) << 6) |
                   ((src_alpha & 0x1F) << 11) |
                   ((dst_alpha & 0x1F) << 16))
    blend_state2 = ((color_blend_op & 7) |
                    ((alpha_blend_op & 7) << 3) |
                    ((color_mask & 0x0F) << 6) |
                    ((const_alpha & 0xFF) << 10))
    cull_val = cull_mode & 3
    prim_val = topology & 7
    front_bit = 1 if front_face_cw else 0
    raster_state = cull_val | (prim_val << 2) | (front_bit << 5)
    return (flags, depth_state, blend_state, blend_state2, raster_state)

def parse_j3d_mat3(b: bytes, model_name: str, archive_name: str = ""):
    """Extracts authentic materials from a J3D (BMD/BDL) file."""
    materials = []
    gaps = []
    if len(b) < 0x20:
        return materials, gaps

    magic = b[:4]
    if magic not in (b'J3D2', b'J3D1'):
        return materials, gaps

    nblocks = struct.unpack('>I', b[0x0C:0x10])[0] if b[4:8] != b'bmd3' else struct.unpack('<I', b[0x0C:0x10])[0]
    is_be = (magic == b'J3D2' and struct.unpack('>I', b[0x0C:0x10])[0] < 100)
    fmt32 = '>I' if is_be else '<I'
    fmt16 = '>H' if is_be else '<H'

    o = 0x20
    for _ in range(nblocks):
        if o + 8 > len(b):
            break
        tag = b[o:o+4]
        size = struct.unpack(fmt32, b[o+4:o+8])[0]
        if size <= 0 or o + size > len(b):
            break

        if tag == b'MAT3':
            num_mats = struct.unpack(fmt16, b[o+8:o+10])[0]
            if num_mats == 0 or num_mats > 2048:
                gaps.append(f"Invalid material count {num_mats} in {model_name}")
                break

            offs = []
            for t in range(30):
                offs.append(struct.unpack(fmt32, b[o+0x0C+4*t:o+0x10+4*t])[0])

            tab0 = o + offs[0]
            if tab0 + num_mats * 0x14C > len(b):
                gaps.append(f"Truncated MAT3 Table 0 in {model_name}")
                break

            # Read material names from Table 2
            names = []
            name_off = offs[2]
            if name_off and o + name_off + 4 <= len(b):
                ntab_o = o + name_off
                n_names = struct.unpack(fmt16, b[ntab_o:ntab_o+2])[0]
                cur_str = ntab_o + 4 + n_names * 4
                for ni in range(min(num_mats, n_names)):
                    str_end = b.find(b'\x00', cur_str)
                    if str_end != -1 and str_end < len(b):
                        names.append(b[cur_str:str_end].decode('ascii', errors='ignore'))
                        cur_str = str_end + 1
                    else:
                        names.append(f"mat_{ni}")

            for m in range(num_mats):
                m_off = tab0 + m * 0x14C
                mat_name = names[m] if m < len(names) else f"mat_{m}"

                # Material fields from Table 0 indices
                cull_idx = b[m_off + 0x01]
                num_chans_idx = b[m_off + 0x02]
                num_texgens_idx = b[m_off + 0x03]
                num_tevstages_idx = b[m_off + 0x04]
                zmode_idx = b[m_off + 0x06]
                alpha_comp_idx = struct.unpack(fmt16, b[m_off + 0x146:m_off + 0x148])[0]
                blend_idx = struct.unpack(fmt16, b[m_off + 0x148:m_off + 0x14A])[0]

                # 1. Cull mode (Table 4)
                cull_val = CANONICAL_CULL_BACK
                if offs[4] and o + offs[4] + cull_idx * 4 + 4 <= len(b):
                    gx_cull = struct.unpack(fmt32, b[o + offs[4] + cull_idx*4 : o + offs[4] + cull_idx*4 + 4])[0]
                    cull_val = min(3, gx_cull)

                # 2. Channel & texgen counts
                num_chans = 1
                if offs[6] and o + offs[6] + num_chans_idx < len(b):
                    num_chans = max(1, min(2, b[o + offs[6] + num_chans_idx]))

                num_texgens = 1
                if offs[10] and o + offs[10] + num_texgens_idx < len(b):
                    num_texgens = max(1, min(8, b[o + offs[10] + num_texgens_idx]))

                num_tevstages = 1
                if offs[19] and o + offs[19] + num_tevstages_idx < len(b):
                    num_tevstages = max(1, min(16, b[o + offs[19] + num_tevstages_idx]))

                # 3. ZMode (Table 26): [enable, func, write_enable, pad]
                depth_test = True
                depth_write = True
                depth_op = CANONICAL_DEPTH_LEQUAL
                if offs[26] and o + offs[26] + zmode_idx * 4 + 3 <= len(b):
                    zm_o = o + offs[26] + zmode_idx * 4
                    depth_test = (b[zm_o] != 0)
                    depth_op = min(7, b[zm_o + 1])
                    depth_write = (b[zm_o + 2] != 0)

                # 4. BlendMode (Table 25): [type, srcFactor, dstFactor, op]
                blend_enable = False
                src_color = CANONICAL_BLEND_ONE
                dst_color = CANONICAL_BLEND_ZERO
                src_alpha = CANONICAL_BLEND_ONE
                dst_alpha = CANONICAL_BLEND_ZERO
                color_blend_op = CANONICAL_BLEND_OP_ADD
                alpha_blend_op = CANONICAL_BLEND_OP_ADD
                if offs[25] and blend_idx != 0xFFFF and o + offs[25] + blend_idx * 4 + 4 <= len(b):
                    bl_o = o + offs[25] + blend_idx * 4
                    blend_type = b[bl_o]
                    blend_enable = (blend_type != 0)
                    src_color = min(9, b[bl_o + 1])
                    dst_color = min(9, b[bl_o + 2])
                    src_alpha = src_color
                    dst_alpha = dst_color
                    color_blend_op = min(2, b[bl_o + 3])
                    alpha_blend_op = color_blend_op

                # 5. Alpha Compare (Table 24)
                alpha_func = 0
                if offs[24] and alpha_comp_idx != 0xFFFF and o + offs[24] + alpha_comp_idx * 8 + 5 <= len(b):
                    ac_o = o + offs[24] + alpha_comp_idx * 8
                    comp0 = b[ac_o] & 7
                    op = b[ac_o + 2] & 3
                    comp1 = b[ac_o + 3] & 7
                    alpha_func = comp0 | (comp1 << 3) | (op << 6)

                # 6. Build authentic 90-word ShaderUid
                uid_words = [0] * 90
                vtx_flags = 0x4047  # Pos | PosMtx | Nrm | Tex0 | TexMtx
                uid_words[0] = vtx_flags
                uid_words[1] = num_texgens
                uid_words[2] = num_chans
                uid_words[4] = 0x0001  # colorCtrl[0]
                uid_words[6] = 5 << 7  # texMtxInfo[0]
                uid_words[24] = num_tevstages
                uid_words[29] = alpha_func  # BP 0xF3 bits 16-23

                # Read TEV order if available (Table 16)
                if offs[16]:
                    for s in range(min(num_tevstages, 16)):
                        ord_idx = struct.unpack(fmt16, b[m_off + 0xBC + s*2 : m_off + 0xBC + s*2 + 2])[0]
                        if ord_idx != 0xFFFF and o + offs[16] + ord_idx * 4 + 3 <= len(b):
                            tord_o = o + offs[16] + ord_idx * 4
                            tc = b[tord_o] & 7
                            tm = b[tord_o + 1] & 7
                            ch = b[tord_o + 2] & 7
                            entry = tc | (tm << 3) | (ch << 6)
                            uid_words[10 + (s // 2)] |= (entry << ((s % 2) * 12))

                # Standard tev stages
                for s in range(num_tevstages):
                    uid_words[12 + s] = 0x080A00  # tevColor[s]
                    uid_words[13 + s] = 0x080A00  # tevAlpha[s]

                key = encode_pipeline_key(uid_words, vtx_flags, depth_test, depth_write, depth_op,
                                          blend_enable, src_color, dst_color, src_alpha, dst_alpha,
                                          color_blend_op=color_blend_op, alpha_blend_op=alpha_blend_op,
                                          color_mask=15, cull_mode=cull_val, topology=CANONICAL_TOPO_TRIANGLE_LIST)
                source_desc = f"{archive_name}/{model_name}:{mat_name}" if archive_name else f"{model_name}:{mat_name}"
                materials.append((uid_words, key, source_desc))

        o += size
    return materials, gaps

def scan_directory_for_models(root_dir: str):
    """Scans root_dir recursively for .bdl, .bmd, and extracts .arc archives safely."""
    discovered = []
    seen_keys = set()
    total_models = 0
    total_archives = 0
    total_mats = 0
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
                    mats, gaps = parse_j3d_mat3(b, fn)
                    total_mats += len(mats)
                    total_gaps.extend(gaps)
                    for uid, key, src in mats:
                        if key not in seen_keys:
                            seen_keys.add(key)
                            discovered.append((uid, key, src))
                except Exception as e:
                    total_gaps.append(f"Failed reading model {fp}: {e}")

            elif ext == '.arc' and yaz0_decompress is not None and rarc_list is not None:
                total_archives += 1
                try:
                    with open(fp, 'rb') as f:
                        data = f.read()
                    raw = yaz0_decompress(data)
                    for inner_path, inner_bytes in rarc_list(raw):
                        inner_ext = os.path.splitext(inner_path)[1].lower()
                        if inner_ext in ('.bdl', '.bmd'):
                            total_models += 1
                            mats, gaps = parse_j3d_mat3(inner_bytes, os.path.basename(inner_path), fn)
                            total_mats += len(mats)
                            total_gaps.extend(gaps)
                            for uid, key, src in mats:
                                if key not in seen_keys:
                                    seen_keys.add(key)
                                    discovered.append((uid, key, src))
                except Exception as e:
                    total_gaps.append(f"Failed extracting archive {fp}: {e}")

    report = {
        'total_models': total_models,
        'total_archives': total_archives,
        'total_materials_parsed': total_mats,
        'unique_keys': len(discovered),
        'unique_uids': len({tuple(u) for u, _, _ in discovered}),
        'gaps_count': len(total_gaps),
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
    print(f"Archives Extracted: {report['total_archives']}")
    print(f"Models Scanned: {report['total_models']}")
    print(f"Material Records Parsed: {report['total_materials_parsed']}")
    print(f"Unique Shader UIDs: {report['unique_uids']}")
    print(f"Unique Pipeline Keys: {report['unique_keys']}")
    print(f"Gaps / Errors Encountered: {report['gaps_count']}")
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

