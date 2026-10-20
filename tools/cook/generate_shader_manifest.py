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
    """Extracts authentic materials from a J3D (BMD/BDL) file supporting both big-endian and little-endian formats."""
    materials = []
    gaps = []
    mat3_count = 0
    if len(b) < 0x20:
        return materials, gaps, mat3_count

    magic = b[:4]
    if magic in (b'J3D2', b'J3D1'):
        is_be = True
        mat_tag = b'MAT3'
        fmt32 = '>I'
        fmt16 = '>H'
    elif magic in (b'2D3J', b'1D3J'):
        is_be = False
        mat_tag = b'3TAM'
        fmt32 = '<I'
        fmt16 = '<H'
    else:
        return materials, gaps, mat3_count

    nblocks = struct.unpack_from(fmt32, b, 0x0C)[0]
    if nblocks <= 0 or nblocks > 64:
        gaps.append(f"Invalid block count {nblocks} in {model_name}")
        return materials, gaps, mat3_count

    o = 0x20
    for block_idx in range(nblocks):
        if o + 8 > len(b):
            break
        tag = b[o:o+4]
        size = struct.unpack_from(fmt32, b, o+4)[0]
        if size <= 0 or o + size > len(b):
            break

        if tag in (b'MAT3', b'3TAM'):
            mat3_count += 1
            num_mats = struct.unpack_from(fmt16, b, o+8)[0]
            if num_mats == 0 or num_mats > 2048:
                gaps.append(f"Invalid material count {num_mats} in {model_name}")
                break

            offs = []
            for t in range(30):
                offs.append(struct.unpack_from(fmt32, b, o+0x0C+4*t)[0])

            tab0 = o + offs[0]
            if tab0 + num_mats * 0x14C > len(b):
                gaps.append(f"Truncated MAT3 Table 0 in {model_name}")
                break

            # Read material names from Table 2 (J3D String Table)
            names = []
            name_off = offs[2]
            if name_off and o + name_off + 4 <= len(b):
                ntab_o = o + name_off
                n_names = struct.unpack_from(fmt16, b, ntab_o)[0]
                for ni in range(min(num_mats, n_names)):
                    str_ent_o = ntab_o + 4 + ni * 4
                    if str_ent_o + 4 <= len(b):
                        str_off = struct.unpack_from(fmt16, b, str_ent_o + 2)[0]
                        s_start = ntab_o + str_off
                        s_end = b.find(b'\x00', s_start)
                        if s_end != -1 and s_end <= len(b):
                            names.append(b[s_start:s_end].decode('ascii', errors='ignore'))
                        else:
                            names.append(f"mat_{ni}")
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
                alpha_comp_idx = struct.unpack_from(fmt16, b, m_off + 0x146)[0]
                blend_idx = struct.unpack_from(fmt16, b, m_off + 0x148)[0]

                # 1. Cull mode (Table 4)
                cull_val = CANONICAL_CULL_BACK
                if offs[4] and o + offs[4] + cull_idx * 4 + 4 <= len(b):
                    gx_cull = struct.unpack_from(fmt32, b, o + offs[4] + cull_idx*4)[0]
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

                # 5. Alpha Compare (Table 24): [comp0, ref0, op, comp1, ref1, pad...]
                alpha_func = 0
                if offs[24] and alpha_comp_idx != 0xFFFF and o + offs[24] + alpha_comp_idx * 8 + 5 <= len(b):
                    ac_o = o + offs[24] + alpha_comp_idx * 8
                    comp0 = b[ac_o] & 7
                    op = b[ac_o + 2] & 3
                    comp1 = b[ac_o + 3] & 7
                    alpha_func = comp0 | (op << 3) | (comp1 << 5)

                # 6. Build authentic 90-word ShaderUid matching native C++ engine
                uid_words = [0] * 90
                vtx_flags = 0x4047  # Pos | PosMtx | Nrm | Tex0 | TexMtx
                uid_words[0] = vtx_flags
                uid_words[1] = num_texgens
                uid_words[2] = num_chans
                uid_words[3] = 0  # dualTex
                for c in range(min(num_chans, 2)):
                    uid_words[4 + c] = 0x0001  # colorCtrl[c]
                    uid_words[6 + c] = 0x0001  # alphaCtrl[c]
                for tg in range(min(num_texgens, 8)):
                    uid_words[8 + tg] = (5 + 3 * tg) << 7  # texMtxInfo[tg] (Tex0 row 5, Tex1 row 8...)
                uid_words[24] = num_tevstages
                uid_words[25] = 0  # numIndStages
                uid_words[74] = alpha_func  # alphaFunc BP 0xF3 bits 16-23

                # Read TEV order if available (Table 16)
                if offs[16]:
                    for s in range(min(num_tevstages, 16)):
                        ord_idx = struct.unpack_from(fmt16, b, m_off + 0xBC + s*2)[0]
                        if ord_idx != 0xFFFF and o + offs[16] + ord_idx * 4 + 4 <= len(b):
                            tord_o = o + offs[16] + ord_idx * 4
                            tc = b[tord_o] & 7
                            tm = b[tord_o + 1] & 7
                            en = 1 if (tm != 7) else 0
                            ch = b[tord_o + 2] & 7
                            order_val = (tm & 7) | ((tc & 7) << 3) | (en << 6) | ((ch & 7) << 7)
                            uid_words[26 + (s // 2)] |= (order_val << ((s % 2) * 12))

                # Read authentic TEV stage operations from Table 20 (TevStageInfo)
                if offs[20]:
                    for s in range(min(num_tevstages, 16)):
                        stage_idx = struct.unpack_from(fmt16, b, m_off + 0xE4 + s*2)[0]
                        if stage_idx != 0xFFFF and o + offs[20] + stage_idx * 20 + 20 <= len(b):
                            t20_o = o + offs[20] + stage_idx * 20
                            st = list(b[t20_o:t20_o+20])
                            # [color_a, color_b, color_c, color_d, color_op, color_bias, color_scale, color_clamp, color_out_reg,
                            #  alpha_a, alpha_b, alpha_c, alpha_d, alpha_op, alpha_bias, alpha_scale, alpha_clamp, alpha_out_reg]
                            ca, cb, cc, cd, cop, cbias, cscale, cclamp, cout = st[0:9]
                            aa, ab, ac, ad, aop, abias, ascale, aclamp, aout = st[9:18]
                            tev_color = ((cd & 0xF) | ((cc & 0xF) << 4) | ((cb & 0xF) << 8) | ((ca & 0xF) << 12) |
                                         ((cbias & 3) << 16) | ((1 if cop else 0) << 18) | ((1 if cclamp else 0) << 19) |
                                         ((cscale & 3) << 20) | ((cout & 3) << 22))
                            tev_alpha = ((ad & 7) | ((ac & 7) << 4) | ((ab & 7) << 7) | ((aa & 7) << 10) |
                                         ((abias & 3) << 16) | ((1 if aop else 0) << 18) | ((1 if aclamp else 0) << 19) |
                                         ((ascale & 3) << 20) | ((aout & 3) << 22))
                            uid_words[34 + s] = tev_color
                            uid_words[50 + s] = tev_alpha
                        else:
                            uid_words[34 + s] = 0x080A00
                            uid_words[50 + s] = 0x080A00
                else:
                    for s in range(num_tevstages):
                        uid_words[34 + s] = 0x080A00
                        uid_words[50 + s] = 0x080A00

                key = encode_pipeline_key(uid_words, vtx_flags, depth_test, depth_write, depth_op,
                                          blend_enable, src_color, dst_color, src_alpha, dst_alpha,
                                          color_blend_op=color_blend_op, alpha_blend_op=alpha_blend_op,
                                          color_mask=15, cull_mode=cull_val, topology=CANONICAL_TOPO_TRIANGLE_LIST)
                source_desc = f"{archive_name}/{model_name}:{mat_name}" if archive_name else f"{model_name}:{mat_name}"
                materials.append((uid_words, key, source_desc))

        o += size
    return materials, gaps, mat3_count

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

